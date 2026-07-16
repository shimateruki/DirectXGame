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
// Model 基本初期化とGPUバッファ管理
// ------------------------------------------------------------------------
// モデルデータの読み込み結果をGPUへ渡すための初期化、ボーン行列用の
// バッファ更新、頂点数などの軽量な問い合わせを担当する。
// ========================================================================

// モデル共通情報とファイル名を受け取り、モデルデータ・GPUリソース・スケルトンを初期化する。
void Model::Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename) {
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // 1. ファイル読み込み (Mesh分けされたデータが返ってくる)
    modelData_ = LoadFile(directoryPath, filename);
    modelData_.meshes.erase(
        std::remove_if(modelData_.meshes.begin(), modelData_.meshes.end(),
            [](const Mesh& mesh) {
                return mesh.vertices.empty() || mesh.indices.empty();
            }),
        modelData_.meshes.end());

    // --- AABB（モデルのサイズ）計算 ---
    Vector3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
    Vector3 max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    bool hasVertices = false;

    for (const auto& mesh : modelData_.meshes) {
        for (const auto& vertex : mesh.vertices) {
            // 全メッシュの全頂点から最小・最大を割り出す
            min.x = (std::min)(min.x, vertex.position.x);
            min.y = (std::min)(min.y, vertex.position.y);
            min.z = (std::min)(min.z, vertex.position.z);
            max.x = (std::max)(max.x, vertex.position.x);
            max.y = (std::max)(max.y, vertex.position.y);
            max.z = (std::max)(max.z, vertex.position.z);
            hasVertices = true;
        }
    }

    if (hasVertices) {
        // カリング用のローカルAABBを保存
        localAabbMin_ = min;
        localAabbMax_ = max;
        // 中心座標とサイズも一応計算して保持
        center_ = { (min.x + max.x) / 2.0f, (min.y + max.y) / 2.0f, (min.z + max.z) / 2.0f };
        size_ = { max.x - min.x, max.y - min.y, max.z - min.z };
    }
    else {
        localAabbMin_ = { -0.5f, -0.5f, -0.5f };
        localAabbMax_ = { 0.5f, 0.5f, 0.5f };
        center_ = { 0.0f, 0.0f, 0.0f };
        size_ = { 1.0f, 1.0f, 1.0f };
    }

    // 2. マテリアルごとにテクスチャをロード
    for (auto& material : modelData_.materials) {
        material.textureHandle = TextureManager::GetInstance()->Load(
            material.textureFilePath.empty() ? kDefaultWhiteTexture : material.textureFilePath,
            TextureManager::TextureColorSpace::SRGB);
        if (material.hasNormalMap && !material.normalMapPath.empty()) {
            material.normalMapHandle = TextureManager::GetInstance()->Load(
                material.normalMapPath,
                TextureManager::TextureColorSpace::Linear);
        }
        if (material.hasOrmMap && !material.ormMapPath.empty()) {
            material.ormMapHandle = TextureManager::GetInstance()->Load(
                material.ormMapPath,
                TextureManager::TextureColorSpace::Linear);
        }
    }

    // 3. メッシュごとに頂点バッファ・インデックスバッファを作成
    for (auto& mesh : modelData_.meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            continue;
        }
        // 頂点バッファ
        mesh.vertexResource = dxCommon->CreateBufferResource(sizeof(VertexData) * mesh.vertices.size());
        if (!mesh.vertexResource) {
            DebugConsole::GetInstance()->AddLog("Model vertex buffer creation failed: " + filename);
            continue;
        }
        mesh.vertexBufferView.BufferLocation = mesh.vertexResource->GetGPUVirtualAddress();
        mesh.vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * mesh.vertices.size());
        mesh.vertexBufferView.StrideInBytes = sizeof(VertexData);

        VertexData* vertexData = nullptr;
        HRESULT vertexMapResult = mesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
        if (FAILED(vertexMapResult) || !vertexData) {
            DebugConsole::GetInstance()->AddLog("Model vertex buffer map failed: " + filename);
            mesh.vertexResource.Reset();
            continue;
        }
        std::memcpy(vertexData, mesh.vertices.data(), sizeof(VertexData) * mesh.vertices.size());
        mesh.vertexResource->Unmap(0, nullptr);

        // インデックスバッファ
        mesh.indexResource = dxCommon->CreateBufferResource(sizeof(uint32_t) * mesh.indices.size());
        if (!mesh.indexResource) {
            DebugConsole::GetInstance()->AddLog("Model index buffer creation failed: " + filename);
            mesh.vertexResource.Reset();
            continue;
        }
        mesh.indexBufferView.BufferLocation = mesh.indexResource->GetGPUVirtualAddress();
        mesh.indexBufferView.SizeInBytes = UINT(sizeof(uint32_t) * mesh.indices.size());
        mesh.indexBufferView.Format = DXGI_FORMAT_R32_UINT;

        uint32_t* indexData = nullptr;
        HRESULT indexMapResult = mesh.indexResource->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
        if (FAILED(indexMapResult) || !indexData) {
            DebugConsole::GetInstance()->AddLog("Model index buffer map failed: " + filename);
            mesh.vertexResource.Reset();
            mesh.indexResource.Reset();
            continue;
        }
        std::memcpy(indexData, mesh.indices.data(), sizeof(uint32_t) * mesh.indices.size());
        mesh.indexResource->Unmap(0, nullptr);
    }

    // 4. 定数バッファ(Material)の作成と初期設定
    materialData_ = nullptr;
    materialResource_ = dxCommon->CreateBufferResource(sizeof(Material));
    if (!materialResource_) {
        DebugConsole::GetInstance()->AddLog("Model material buffer creation failed: " + filename);
        return;
    }

    HRESULT materialMapResult = materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    if (FAILED(materialMapResult) || !materialData_) {
        DebugConsole::GetInstance()->AddLog("Model material buffer map failed: " + filename);
        materialResource_.Reset();
        materialData_ = nullptr;
        return;
    }
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = true;
    materialData_->selectedLighting = 2; // 隊長の設定値を維持
    materialData_->shininess = 50;
    materialData_->materialType = 0;
    materialData_->roughness = 0.5f;
    materialData_->metallic = 0.0f;
    materialData_->enableNormalMap = 1;
    materialData_->enableEnvMap = 0;
    materialData_->envIntensity = 1.0f;
    materialData_->emissive = 1.0f;
    materialData_->time = 0.0f;
    materialData_->portalClipEnabled = 0.0f;
    materialData_->portalClipProgress = 0.0f;
    materialData_->portalClipCenter = { 0.0f, 0.0f, 0.0f };
    materialData_->portalClipEdgeWidth = 0.12f;
    materialData_->portalClipNormal = { 0.0f, 0.0f, 1.0f };
    materialData_->portalClipDissolve = 0.18f;
    materialData_->portalClipColor = { 1.0f, 0.82f, 0.36f, 0.80f };
    materialData_->uvTransform = math_.MakeIdentity4x4();

    if (const MaterialData* primaryMaterial = GetPrimaryMaterialData()) {
        materialData_->color = primaryMaterial->baseColorFactor;
        materialData_->roughness = primaryMaterial->roughness;
        materialData_->metallic = primaryMaterial->metallic;
        materialData_->enableNormalMap = primaryMaterial->hasNormalMap ? 1 : 0;
    }

    // 5. ボーン用バッファの作成 
    CreateBoneBuffer();
}
// ==========================================
// ボーンバッファの作成とSRV登録 
// ==========================================
// スキニング用のボーン行列バッファを作成し、SRVとしてシェーダーから参照できる状態にする。
void Model::CreateBoneBuffer() {
    DirectXCommon* dxCommon = common_->GetDxCommon();

    if (modelData_.bones.empty()) {
        boneResource_.Reset();
        boneMappedData_ = nullptr;
        boneSrvIndex_ = 0;
        return;
    }

    // 1. リソース作成
    UINT sizeInBytes = sizeof(BoneForGPU) * static_cast<UINT>(modelData_.bones.size());
    boneResource_ = dxCommon->CreateBufferResource(sizeInBytes);
    if (!boneResource_) {
        DebugConsole::GetInstance()->AddLog("Model bone buffer creation failed.");
        boneMappedData_ = nullptr;
        boneSrvIndex_ = 0;
        return;
    }

    // 2. マッピング
    HRESULT boneMapResult = boneResource_->Map(0, nullptr, reinterpret_cast<void**>(&boneMappedData_));
    if (FAILED(boneMapResult) || !boneMappedData_) {
        DebugConsole::GetInstance()->AddLog("Model bone buffer map failed.");
        boneResource_.Reset();
        boneMappedData_ = nullptr;
        boneSrvIndex_ = 0;
        return;
    }

    // ★重要: 初期値を「単位行列」で埋めておく
    // これをしないと、アニメーション更新が走る前の1フレーム目にモデルが消えます
    Math math;
    for (size_t i = 0; i < modelData_.bones.size(); ++i) {
        boneMappedData_[i].finalMatrix = math.MakeIdentity4x4();
    }

    // 3. SRVを作成 (StructuredBuffer)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = UINT(modelData_.bones.size());
    srvDesc.Buffer.StructureByteStride = sizeof(BoneForGPU);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    // SRVManagerでディスクリプタを確保して作成
    boneSrvIndex_ = SRVManager::GetInstance()->Allocate();
    SRVManager::GetInstance()->CreateSRVforResource(boneSrvIndex_, boneResource_.Get(), srvDesc);
}

// ==========================================
// ボーン行列の更新 
// ==========================================
// 現在のスケルトン行列をGPU側のボーンバッファへ転送する。
void Model::UpdateBoneBuffer() {
    if (!boneMappedData_) {
        return;
    }

    if (!modelData_.hasSkinning && !modelData_.usesNodeAnimationProxy) {
        for (size_t i = 0; i < modelData_.bones.size(); ++i) {
            boneMappedData_[i].finalMatrix = math_.MakeIdentity4x4();
        }
        return;
    }
    // ボーンごとに計算
    for (size_t i = 0; i < modelData_.bones.size(); ++i) {
        // ボーン名に対応するJointを探す
        auto it = modelData_.skeleton.jointMap.find(modelData_.bones[i].name);

        // FinalMatrix = InverseBindPose * SkeletonSpaceMatrix
        if (it != modelData_.skeleton.jointMap.end()) {
            const Joint& joint = modelData_.skeleton.joints[it->second];
            boneMappedData_[i].finalMatrix =
                math_.Multiply(modelData_.bones[i].inverseBindPoseMatrix, joint.skeletonSpaceMatrix);
        }
        else {
            // ノードが見つからない場合は単位行列を入れる
            boneMappedData_[i].finalMatrix = math_.MakeIdentity4x4();
        }
    }
}

// モデルの描画処理
void Model::Draw(ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource, ID3D12Resource* cameraResource, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, ID3D12Resource* overrideMaterialResource, uint32_t normalMapHandle, uint32_t ormMapHandle, uint32_t overrideTextureHandle, uint32_t instanceCount, uint32_t startInstanceLocation, int meshDrawIndex)
{
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    // 1. マテリアル設定
    if (overrideMaterialResource) {
        commandList->SetGraphicsRootConstantBufferView(0, overrideMaterialResource->GetGPUVirtualAddress());
    }
    else if (materialResource_) {
        commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    }

    // 2. 定数バッファ設定
    if (wvpResource) commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
    // (RootParam[2] はテクスチャ)
    if (directionalLightResource) commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
    if (cameraResource) commandList->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());
    if (pointLightResource) commandList->SetGraphicsRootConstantBufferView(5, pointLightResource->GetGPUVirtualAddress());
    if (spotLightResource) commandList->SetGraphicsRootConstantBufferView(6, spotLightResource->GetGPUVirtualAddress());


    if (!modelData_.bones.empty() && boneResource_ && boneSrvIndex_ != 0) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 7, boneSrvIndex_);
    }

    uint32_t envMapHandle = LightManager::GetInstance()->GetEnvironmentMapHandle();
    if (envMapHandle != 0) { // 念のため0（未読み込み）じゃないかチェック
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 8, envMapHandle);
    }
    uint32_t handleToBind = normalMapHandle;
    if (handleToBind == 0) {
        // 画像が設定されていない場合はダミーの白画像を使う
        handleToBind = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
    }
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 9, handleToBind);
    uint32_t ormHandleToBind = ormMapHandle;

    // 画像が未設定(0)、または異常な値の時は「white.png (RGBすべて1.0)」をセットする！
    if (ormHandleToBind <= 0 || ormHandleToBind >= DirectXCommon::kMaxSRVCount) {
        ormHandleToBind = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
    }

    // 次のインデックス(例: 10番)にORMマップをセット！
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 10, ormHandleToBind);
    // 3. メッシュごとの描画ループ
    for (size_t meshIndex = 0; meshIndex < modelData_.meshes.size(); ++meshIndex) {
        if (meshDrawIndex >= 0 && meshIndex != static_cast<size_t>(meshDrawIndex)) {
            continue;
        }
        const auto& mesh = modelData_.meshes[meshIndex];
        if (!mesh.vertexResource || !mesh.indexResource || mesh.indices.empty()) {
            continue;
        }

        const MaterialData* meshMaterial = nullptr;
        if (mesh.materialIndex < modelData_.materials.size()) {
            meshMaterial = &modelData_.materials[mesh.materialIndex];
        }

        uint32_t meshNormalHandle = normalMapHandle;
        if ((meshNormalHandle == 0 || meshNormalHandle >= DirectXCommon::kMaxSRVCount) &&
            meshMaterial && meshMaterial->hasNormalMap &&
            meshMaterial->normalMapHandle > 0 && meshMaterial->normalMapHandle < DirectXCommon::kMaxSRVCount) {
            meshNormalHandle = meshMaterial->normalMapHandle;
        }
        if (meshNormalHandle == 0 || meshNormalHandle >= DirectXCommon::kMaxSRVCount) {
            meshNormalHandle = TextureManager::GetInstance()->Load(kDefaultWhiteTexture);
        }
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 9, meshNormalHandle);

        uint32_t meshOrmHandle = ormMapHandle;
        if ((meshOrmHandle == 0 || meshOrmHandle >= DirectXCommon::kMaxSRVCount) &&
            meshMaterial && meshMaterial->hasOrmMap &&
            meshMaterial->ormMapHandle > 0 && meshMaterial->ormMapHandle < DirectXCommon::kMaxSRVCount) {
            meshOrmHandle = meshMaterial->ormMapHandle;
        }
        if (meshOrmHandle == 0 || meshOrmHandle >= DirectXCommon::kMaxSRVCount) {
            meshOrmHandle = TextureManager::GetInstance()->Load(kDefaultWhiteTexture);
        }
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 10, meshOrmHandle);

        commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
        commandList->IASetIndexBuffer(&mesh.indexBufferView);

        if (overrideTextureHandle > 0 && overrideTextureHandle < DirectXCommon::kMaxSRVCount) {
            // エディタで画像が選ばれていたら、そっちを優先して貼る！
            SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, overrideTextureHandle);
        }
        else if (mesh.materialIndex < modelData_.materials.size()) {
            // 選ばれていない場合は、モデル本来のテクスチャを貼る
            uint32_t handle = modelData_.materials[mesh.materialIndex].textureHandle;
            SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, handle);
        }

        commandList->DrawIndexedInstanced(UINT(mesh.indices.size()), instanceCount, 0, 0, startInstanceLocation);
    }
}

// モデル全体の頂点数を返し、統計表示や軽量化確認で使えるようにする。
uint32_t Model::GetVertexCount() const {
    uint32_t count = 0;
    for (const auto& mesh : modelData_.meshes) count += static_cast<uint32_t>(mesh.vertices.size());
    return count;
}

// インデックス数から概算ポリゴン数を返し、描画負荷の目安にする。
uint32_t Model::GetPolygonCount() const {
    uint32_t count = 0;
    for (const auto& mesh : modelData_.meshes) count += static_cast<uint32_t>(mesh.indices.size() / 3);
    return count;
}
// ==========================================
// 読み込み: Assimpのメッシュごとにデータを分ける
// ==========================================
