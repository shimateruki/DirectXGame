#define NOMINMAX
#include <fstream>
#include "Model.h"
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <sstream>
#include <cassert>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include "SRVManager.h"
#include <DebugConsole.h>
#include <LightManager.h>

namespace {
constexpr std::array<char, 8> kModelCacheMagic = { 'G', 'E', '3', 'M', 'C', 'A', 'C', 'H' };
constexpr uint32_t kModelCacheVersion = 2;
constexpr uint32_t kMaxCacheStringSize = 1024 * 1024;
constexpr uint32_t kMaxCacheVectorCount = 10'000'000;
constexpr const char* kDefaultWhiteTexture = "Resources/sprite/common/white.png";

template <typename T>
// POD値をバイナリへそのまま書き込み、モデルキャッシュ保存の基本単位にする。
bool WritePod(std::ostream& os, const T& value) {
    os.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(os);
}

template <typename T>
// バイナリからPOD値を読み戻し、モデルキャッシュ復元の基本単位にする。
bool ReadPod(std::istream& is, T& value) {
    is.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(is);
}

// 文字列長と本文をセットで書き込み、可変長文字列を安全にキャッシュへ保存する。
bool WriteString(std::ostream& os, const std::string& text) {
    const uint32_t size = static_cast<uint32_t>(text.size());
    if (!WritePod(os, size)) {
        return false;
    }
    if (size > 0) {
        os.write(text.data(), size);
    }
    return static_cast<bool>(os);
}

// 文字列長を検証しながら本文を読み戻し、壊れたキャッシュで異常確保しないようにする。
bool ReadString(std::istream& is, std::string& text) {
    uint32_t size = 0;
    if (!ReadPod(is, size) || size > kMaxCacheStringSize) {
        return false;
    }
    text.resize(size);
    if (size > 0) {
        is.read(text.data(), size);
    }
    return static_cast<bool>(is);
}

template <typename T>
// POD配列の要素数と中身をまとめて書き込み、頂点やインデックス配列を保存する。
bool WritePodVector(std::ostream& os, const std::vector<T>& values) {
    const uint32_t count = static_cast<uint32_t>(values.size());
    if (!WritePod(os, count)) {
        return false;
    }
    if (count > 0) {
        os.write(reinterpret_cast<const char*>(values.data()), sizeof(T) * values.size());
    }
    return static_cast<bool>(os);
}

template <typename T>
// POD配列の要素数を検証してから読み戻し、不正なキャッシュサイズを弾く。
bool ReadPodVector(std::istream& is, std::vector<T>& values) {
    uint32_t count = 0;
    if (!ReadPod(is, count) || count > kMaxCacheVectorCount) {
        return false;
    }
    values.resize(count);
    if (count > 0) {
        is.read(reinterpret_cast<char*>(values.data()), sizeof(T) * values.size());
    }
    return static_cast<bool>(is);
}

// 0から1の範囲に値を丸め、色や係数の異常値を抑える。
float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

// 文字列を小文字化したコピーに変換し、拡張子や名前の比較を安定させる。
std::string ToLowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

// 対象文字列に候補キーワードのいずれかが含まれるかを判定する。
bool ContainsAny(std::string_view text, std::initializer_list<std::string_view> words) {
    for (std::string_view word : words) {
        if (text.find(word) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

// マテリアルに書かれたテクスチャパスを、モデルフォルダ基準の実在パスへ解決する。
std::string ResolveMaterialTexturePath(const std::string& directoryPath, const std::string& rawPath) {
    if (rawPath.empty()) {
        return "";
    }

    std::filesystem::path path(rawPath);
    if (path.is_absolute() && std::filesystem::exists(path)) {
        return path.generic_string();
    }

    const std::filesystem::path relativeCandidate = std::filesystem::path(directoryPath) / path;
    if (std::filesystem::exists(relativeCandidate)) {
        return relativeCandidate.generic_string();
    }

    const std::filesystem::path filenameCandidate = std::filesystem::path(directoryPath) / path.filename();
    return filenameCandidate.generic_string();
}

// Assimpマテリアルから指定種別のテクスチャを取得し、解決済みパスとして返す。
bool TryGetMaterialTexture(aiMaterial* material, const std::vector<aiTextureType>& types, aiString& outPath) {
    if (!material) {
        return false;
    }
    for (aiTextureType type : types) {
        if (material->GetTexture(type, 0, &outPath) == AI_SUCCESS) {
            return true;
        }
    }
    return false;
}

// 種別情報が曖昧なテクスチャから用途に合う候補を探し、補助的に採用する。
bool TryFindUnknownTexture(aiMaterial* material, std::initializer_list<std::string_view> keywords, aiString& outPath) {
    if (!material) {
        return false;
    }
    const unsigned int count = material->GetTextureCount(aiTextureType_UNKNOWN);
    for (unsigned int i = 0; i < count; ++i) {
        aiString candidate;
        if (material->GetTexture(aiTextureType_UNKNOWN, i, &candidate) != AI_SUCCESS) {
            continue;
        }
        const std::string lower = ToLowerCopy(candidate.C_Str());
        if (ContainsAny(lower, keywords)) {
            outPath = candidate;
            return true;
        }
    }
    return false;
}

// モデルファイル名から対応する独自キャッシュファイルの保存先パスを作成する。
std::filesystem::path MakeModelCachePath(const std::filesystem::path& sourcePath) {
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::current_path(ec).lexically_normal();
    std::filesystem::path absoluteSource = std::filesystem::absolute(sourcePath, ec).lexically_normal();
    std::filesystem::path relativeSource = ec ? sourcePath : absoluteSource.lexically_relative(root);
    if (relativeSource.empty() || relativeSource.native().find(L"..") == 0) {
        relativeSource = sourcePath.filename();
    }

    std::filesystem::path cachePath = std::filesystem::path("Resources/.cache/model") / relativeSource;
    cachePath += ".meshcache";
    return cachePath.lexically_normal();
}

// モデル本体と依存ファイルの更新時刻を調べ、キャッシュの新しさ判定に使う。
std::filesystem::file_time_type GetLatestModelDependencyTime(const std::filesystem::path& sourcePath) {
    std::error_code ec;
    auto latest = std::filesystem::last_write_time(sourcePath, ec);
    if (ec) {
        return std::filesystem::file_time_type::min();
    }

    const std::filesystem::path parent = sourcePath.parent_path();
    if (!std::filesystem::exists(parent, ec)) {
        return latest;
    }

    for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (ext != ".bin" && ext != ".mtl" && ext != ".gltf" && ext != ".glb" && ext != ".obj") {
            continue;
        }

        auto time = std::filesystem::last_write_time(entry.path(), ec);
        if (!ec && time > latest) {
            latest = time;
        }
    }
    return latest;
}

// キャッシュファイルがモデルや依存ファイルより新しいかを確認し、再読み込みの要否を決める。
bool IsModelCacheUpToDate(const std::filesystem::path& sourcePath, const std::filesystem::path& cachePath) {
    std::error_code ec;
    if (!std::filesystem::exists(sourcePath, ec) || !std::filesystem::exists(cachePath, ec)) {
        return false;
    }

    const auto sourceTime = GetLatestModelDependencyTime(sourcePath);
    const auto cacheTime = std::filesystem::last_write_time(cachePath, ec);
    if (ec) {
        return false;
    }
    return cacheTime >= sourceTime;
}

// ノードの変換行列と子階層を再帰的に書き込み、階層構造をキャッシュへ保存する。
bool WriteNode(std::ostream& os, const Model::Node& node) {
    if (!WritePod(os, node.transform) ||
        !WritePod(os, node.localMatrix) ||
        !WriteString(os, node.name)) {
        return false;
    }

    const uint32_t childCount = static_cast<uint32_t>(node.children.size());
    if (!WritePod(os, childCount)) {
        return false;
    }
    for (const auto& child : node.children) {
        if (!WriteNode(os, child)) {
            return false;
        }
    }
    return true;
}

// キャッシュからノード階層を再帰的に読み戻し、モデルの親子構造を復元する。
bool ReadNode(std::istream& is, Model::Node& node) {
    if (!ReadPod(is, node.transform) ||
        !ReadPod(is, node.localMatrix) ||
        !ReadString(is, node.name)) {
        return false;
    }
    node.globalMatrix = node.localMatrix;

    uint32_t childCount = 0;
    if (!ReadPod(is, childCount) || childCount > kMaxCacheVectorCount) {
        return false;
    }
    node.children.resize(childCount);
    for (auto& child : node.children) {
        if (!ReadNode(is, child)) {
            return false;
        }
    }
    return true;
}

// ノード階層を走査して名前付きノードの参照表を作り、アニメーション適用先を探しやすくする。
void CollectNodes(const Model::Node& node, std::vector<Model::Node>& nodes) {
    nodes.push_back(node);
    for (const auto& child : node.children) {
        CollectNodes(child, nodes);
    }
}

// アニメーション名・時間・各ノードのキー情報をキャッシュへ保存する。
bool WriteAnimation(std::ostream& os, const Model::Animation& animation) {
    if (!WriteString(os, animation.name) ||
        !WritePod(os, animation.duration) ||
        !WritePod(os, animation.ticksPerSecond)) {
        return false;
    }

    const uint32_t nodeAnimCount = static_cast<uint32_t>(animation.nodeAnimations.size());
    if (!WritePod(os, nodeAnimCount)) {
        return false;
    }
    for (const auto& nodeAnim : animation.nodeAnimations) {
        if (!WriteString(os, nodeAnim.name) ||
            !WritePodVector(os, nodeAnim.scale) ||
            !WritePodVector(os, nodeAnim.rotate) ||
            !WritePodVector(os, nodeAnim.translate)) {
            return false;
        }
    }
    return true;
}

// キャッシュからアニメーション情報を読み戻し、再生に使える形へ復元する。
bool ReadAnimation(std::istream& is, Model::Animation& animation) {
    if (!ReadString(is, animation.name) ||
        !ReadPod(is, animation.duration) ||
        !ReadPod(is, animation.ticksPerSecond)) {
        return false;
    }

    uint32_t nodeAnimCount = 0;
    if (!ReadPod(is, nodeAnimCount) || nodeAnimCount > kMaxCacheVectorCount) {
        return false;
    }
    animation.nodeAnimations.resize(nodeAnimCount);
    for (auto& nodeAnim : animation.nodeAnimations) {
        if (!ReadString(is, nodeAnim.name) ||
            !ReadPodVector(is, nodeAnim.scale) ||
            !ReadPodVector(is, nodeAnim.rotate) ||
            !ReadPodVector(is, nodeAnim.translate)) {
            return false;
        }
    }
    return true;
}

// 読み込み済みModelData全体を独自キャッシュとして保存し、次回起動時の読み込みを軽くする。
bool WriteModelCache(const std::filesystem::path& sourcePath, const Model::ModelData& data) {
    const std::filesystem::path cachePath = MakeModelCachePath(sourcePath);
    std::error_code ec;
    std::filesystem::create_directories(cachePath.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream os(cachePath, std::ios::binary);
    if (!os) {
        return false;
    }

    os.write(kModelCacheMagic.data(), kModelCacheMagic.size());
    if (!WritePod(os, kModelCacheVersion)) {
        return false;
    }

    const uint8_t hasSkinning = data.hasSkinning ? 1 : 0;
    const uint8_t usesNodeAnimationProxy = data.usesNodeAnimationProxy ? 1 : 0;
    if (!WritePod(os, hasSkinning) || !WritePod(os, usesNodeAnimationProxy)) {
        return false;
    }

    const uint32_t materialCount = static_cast<uint32_t>(data.materials.size());
    if (!WritePod(os, materialCount)) {
        return false;
    }
    for (const auto& material : data.materials) {
        const uint8_t hasNormalMap = material.hasNormalMap ? 1 : 0;
        const uint8_t hasOrmMap = material.hasOrmMap ? 1 : 0;
        const uint8_t doubleSided = material.doubleSided ? 1 : 0;
        if (!WriteString(os, material.textureFilePath) ||
            !WriteString(os, material.normalMapPath) ||
            !WriteString(os, material.ormMapPath) ||
            !WritePod(os, material.baseColorFactor) ||
            !WritePod(os, material.roughness) ||
            !WritePod(os, material.metallic) ||
            !WritePod(os, hasNormalMap) ||
            !WritePod(os, hasOrmMap) ||
            !WritePod(os, doubleSided)) {
            return false;
        }
    }

    const uint32_t meshCount = static_cast<uint32_t>(data.meshes.size());
    if (!WritePod(os, meshCount)) {
        return false;
    }
    for (const auto& mesh : data.meshes) {
        if (!WritePod(os, mesh.materialIndex) ||
            !WritePodVector(os, mesh.vertices) ||
            !WritePodVector(os, mesh.indices)) {
            return false;
        }
    }

    const uint32_t boneCount = static_cast<uint32_t>(data.bones.size());
    if (!WritePod(os, boneCount)) {
        return false;
    }
    for (const auto& bone : data.bones) {
        if (!WriteString(os, bone.name) || !WritePod(os, bone.inverseBindPoseMatrix)) {
            return false;
        }
    }

    if (!WriteNode(os, data.rootNode)) {
        return false;
    }

    const uint32_t animationCount = static_cast<uint32_t>(data.animations.size());
    if (!WritePod(os, animationCount)) {
        return false;
    }
    for (const auto& animation : data.animations) {
        if (!WriteAnimation(os, animation)) {
            return false;
        }
    }

    return static_cast<bool>(os);
}

// 独自キャッシュからModelData全体を復元し、通常読み込みを省略できるか確認する。
bool ReadModelCache(const std::filesystem::path& sourcePath, Model::ModelData& data) {
    const std::filesystem::path cachePath = MakeModelCachePath(sourcePath);
    if (!IsModelCacheUpToDate(sourcePath, cachePath)) {
        return false;
    }

    std::ifstream is(cachePath, std::ios::binary);
    if (!is) {
        return false;
    }

    std::array<char, 8> magic{};
    is.read(magic.data(), magic.size());
    uint32_t version = 0;
    if (!is || magic != kModelCacheMagic || !ReadPod(is, version) || version != kModelCacheVersion) {
        return false;
    }

    uint8_t hasSkinning = 0;
    uint8_t usesNodeAnimationProxy = 0;
    if (!ReadPod(is, hasSkinning) || !ReadPod(is, usesNodeAnimationProxy)) {
        return false;
    }
    data.hasSkinning = hasSkinning != 0;
    data.usesNodeAnimationProxy = usesNodeAnimationProxy != 0;

    uint32_t materialCount = 0;
    if (!ReadPod(is, materialCount) || materialCount > kMaxCacheVectorCount) {
        return false;
    }
    data.materials.resize(materialCount);
    for (auto& material : data.materials) {
        uint8_t hasNormalMap = 0;
        uint8_t hasOrmMap = 0;
        uint8_t doubleSided = 0;
        if (!ReadString(is, material.textureFilePath) ||
            !ReadString(is, material.normalMapPath) ||
            !ReadString(is, material.ormMapPath) ||
            !ReadPod(is, material.baseColorFactor) ||
            !ReadPod(is, material.roughness) ||
            !ReadPod(is, material.metallic) ||
            !ReadPod(is, hasNormalMap) ||
            !ReadPod(is, hasOrmMap) ||
            !ReadPod(is, doubleSided)) {
            return false;
        }
        material.hasNormalMap = hasNormalMap != 0;
        material.hasOrmMap = hasOrmMap != 0;
        material.doubleSided = doubleSided != 0;
        material.textureHandle = 0;
        material.normalMapHandle = 0;
        material.ormMapHandle = 0;
        material.materialResource.Reset();
        material.materialData = nullptr;
    }

    uint32_t meshCount = 0;
    if (!ReadPod(is, meshCount) || meshCount > kMaxCacheVectorCount) {
        return false;
    }
    data.meshes.resize(meshCount);
    for (auto& mesh : data.meshes) {
        if (!ReadPod(is, mesh.materialIndex) ||
            !ReadPodVector(is, mesh.vertices) ||
            !ReadPodVector(is, mesh.indices)) {
            return false;
        }
        mesh.vertexResource.Reset();
        mesh.indexResource.Reset();
        mesh.vertexBufferView = {};
        mesh.indexBufferView = {};
    }

    uint32_t boneCount = 0;
    if (!ReadPod(is, boneCount) || boneCount > kMaxCacheVectorCount) {
        return false;
    }
    data.bones.resize(boneCount);
    for (auto& bone : data.bones) {
        if (!ReadString(is, bone.name) || !ReadPod(is, bone.inverseBindPoseMatrix)) {
            return false;
        }
    }

    if (!ReadNode(is, data.rootNode)) {
        return false;
    }
    data.nodes.clear();
    CollectNodes(data.rootNode, data.nodes);

    uint32_t animationCount = 0;
    if (!ReadPod(is, animationCount) || animationCount > kMaxCacheVectorCount) {
        return false;
    }
    data.animations.resize(animationCount);
    for (auto& animation : data.animations) {
        if (!ReadAnimation(is, animation)) {
            return false;
        }
    }

    return static_cast<bool>(is);
}
}


// ==========================================
// 初期化: メッシュごとにバッファを作る
// ==========================================

// ========================================================================
// Model 描画補助と動的メッシュ生成
// ------------------------------------------------------------------------
// 影用描画、メッシュのみの描画、プリミティブ生成など、読み込み以外の
// メッシュ利用処理を担当する。
// ========================================================================

// シャドウマップ生成用に、マテリアル描画を省いた深度向けメッシュ描画を行う。
void Model::DrawShadow(ID3D12Resource* wvpResource, int meshDrawIndex) {
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // [0] WVP
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource->GetGPUVirtualAddress());

    // [1] ボーン情報
    if (!modelData_.bones.empty()) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 1, boneSrvIndex_);
    }

    // 各メッシュの頂点を描画
    for (size_t meshIndex = 0; meshIndex < modelData_.meshes.size(); ++meshIndex) {
        if (meshDrawIndex >= 0 && meshIndex != static_cast<size_t>(meshDrawIndex)) {
            continue;
        }
        auto& mesh = modelData_.meshes[meshIndex];
        if (!mesh.vertexResource || !mesh.indexResource || mesh.indices.empty()) {
            continue;
        }
        commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
        commandList->IASetIndexBuffer(&mesh.indexBufferView);
        commandList->DrawIndexedInstanced(UINT(mesh.indices.size()), 1, 0, 0, 0);
    }
}

// 既に外側で必要な描画状態を設定している前提で、メッシュ本体だけを描画する。
void Model::DrawMeshOnly(int meshDrawIndex) {
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // メッシュごとに頂点バッファだけをセットして描画
    for (size_t meshIndex = 0; meshIndex < modelData_.meshes.size(); ++meshIndex) {
        if (meshDrawIndex >= 0 && meshIndex != static_cast<size_t>(meshDrawIndex)) {
            continue;
        }
        const auto& mesh = modelData_.meshes[meshIndex];
        if (!mesh.vertexResource || !mesh.indexResource || mesh.indices.empty()) {
            continue;
        }
        // 頂点バッファをセット
        commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
        commandList->IASetIndexBuffer(&mesh.indexBufferView);
        commandList->DrawIndexedInstanced(UINT(mesh.indices.size()), 1, 0, 0, 0);
    }
}



// 頂点配列から一時的なモデルデータを構築し、プリミティブやデバッグ表示に使える形へ変換する。
void Model::CreateFromVertices(ModelCommon* common, const std::vector<VertexData>& vertices, const std::vector<uint32_t>& indices) {
    if (!common || vertices.empty() || indices.empty()) {
        modelData_.meshes.clear();
        return;
    }
    assert(common);

    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();
    if (!dxCommon) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: DirectX device is null.");
        modelData_.meshes.clear();
        return;
    }
    ID3D12Device* device = dxCommon->GetDevice();
    if (!device) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: DirectX device is null.");
        modelData_.meshes.clear();
        return;
    }

    // 既存のメッシュデータがあればクリアする（エディタでのリアルタイム更新用）
    modelData_.meshes.clear();

    // 動的生成用のメッシュを1つ追加
    modelData_.meshes.emplace_back();
    auto& mesh = modelData_.meshes.back();

    // 頂点とインデックスのデータを保持
    mesh.vertices = vertices;
    mesh.indices = indices;
    mesh.materialIndex = 0; // ★マテリアルインデックスを初期化

    // ==========================================
    // 1. 頂点バッファ (Vertex Buffer) の作成
    // ==========================================
    UINT vbSize = static_cast<UINT>(sizeof(VertexData) * vertices.size());

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc{};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = vbSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // ★ 修正: vertexResource に生成
    HRESULT hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mesh.vertexResource)
    );
    if (FAILED(hr) || !mesh.vertexResource) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: failed to create vertex buffer.");
        modelData_.meshes.clear();
        return;
    }

    // ★ 修正: vertexResource に対してマップしてデータを流し込む
    void* mappedVerts = nullptr;
    hr = mesh.vertexResource->Map(0, nullptr, &mappedVerts);
    if (FAILED(hr) || !mappedVerts) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: failed to map vertex buffer.");
        modelData_.meshes.clear();
        return;
    }
    memcpy(mappedVerts, vertices.data(), vbSize);
    mesh.vertexResource->Unmap(0, nullptr);

    // ★ 修正: vertexResource からGPUアドレスを取得
    mesh.vertexBufferView.BufferLocation = mesh.vertexResource->GetGPUVirtualAddress();
    mesh.vertexBufferView.SizeInBytes = vbSize;
    mesh.vertexBufferView.StrideInBytes = sizeof(VertexData);

    // ==========================================
    // 2. インデックスバッファ (Index Buffer) の作成
    // ==========================================
    UINT ibSize = static_cast<UINT>(sizeof(uint32_t) * indices.size());

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = ibSize;

    // ★ 修正: indexResource に生成
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mesh.indexResource)
    );
    if (FAILED(hr) || !mesh.indexResource) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: failed to create index buffer.");
        modelData_.meshes.clear();
        return;
    }

    // ★ 修正: indexResource に対してマップしてデータを流し込む
    void* mappedIndices = nullptr;
    hr = mesh.indexResource->Map(0, nullptr, &mappedIndices);
    if (FAILED(hr) || !mappedIndices) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: failed to map index buffer.");
        modelData_.meshes.clear();
        return;
    }
    memcpy(mappedIndices, indices.data(), ibSize);
    mesh.indexResource->Unmap(0, nullptr);

    // ★ 修正: indexResource からGPUアドレスを取得
    mesh.indexBufferView.BufferLocation = mesh.indexResource->GetGPUVirtualAddress();
    mesh.indexBufferView.SizeInBytes = ibSize;
    mesh.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
}

// ==========================================
// Skeleton & Joint の実装 (深さ優先探索)
// ==========================================
