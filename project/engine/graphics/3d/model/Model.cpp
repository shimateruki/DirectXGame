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
#include "SRVManager.h"
#include <DebugConsole.h>
#include <LightManager.h>

namespace {
constexpr std::array<char, 8> kModelCacheMagic = { 'G', 'E', '3', 'M', 'C', 'A', 'C', 'H' };
constexpr uint32_t kModelCacheVersion = 1;
constexpr uint32_t kMaxCacheStringSize = 1024 * 1024;
constexpr uint32_t kMaxCacheVectorCount = 10'000'000;

template <typename T>
bool WritePod(std::ostream& os, const T& value) {
    os.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(os);
}

template <typename T>
bool ReadPod(std::istream& is, T& value) {
    is.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(is);
}

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

void CollectNodes(const Model::Node& node, std::vector<Model::Node>& nodes) {
    nodes.push_back(node);
    for (const auto& child : node.children) {
        CollectNodes(child, nodes);
    }
}

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
        if (!WriteString(os, material.textureFilePath)) {
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
        if (!ReadString(is, material.textureFilePath)) {
            return false;
        }
        material.textureHandle = 0;
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
        material.textureHandle = TextureManager::GetInstance()->Load(material.textureFilePath);
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
        mesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
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
        mesh.indexResource->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
        std::memcpy(indexData, mesh.indices.data(), sizeof(uint32_t) * mesh.indices.size());
        mesh.indexResource->Unmap(0, nullptr);
    }

    // 4. 定数バッファ(Material)の作成と初期設定
    materialResource_ = dxCommon->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = true;
    materialData_->selectedLighting = 2; // 隊長の設定値を維持
    materialData_->shininess = 50;
    materialData_->materialType = 0;
    materialData_->roughness = 0.5f;
    materialData_->metallic = 0.0f;
    materialData_->enableNormalMap = 1;
    materialData_->uvTransform = math_.MakeIdentity4x4();

    // 5. ボーン用バッファの作成 
    CreateBoneBuffer();
}
// ==========================================
// ボーンバッファの作成とSRV登録 
// ==========================================
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

    // 2. マッピング
    boneResource_->Map(0, nullptr, reinterpret_cast<void**>(&boneMappedData_));

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
void Model::UpdateBoneBuffer() {
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
void Model::Draw(ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource, ID3D12Resource* cameraResource, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, ID3D12Resource* overrideMaterialResource, uint32_t normalMapHandle, uint32_t ormMapHandle, uint32_t overrideTextureHandle, uint32_t instanceCount, uint32_t startInstanceLocation)
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


    if (!modelData_.bones.empty()) {
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
    for (const auto& mesh : modelData_.meshes) {
        if (!mesh.vertexResource || !mesh.indexResource || mesh.indices.empty()) {
            continue;
        }
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

uint32_t Model::GetVertexCount() const {
    uint32_t count = 0;
    for (const auto& mesh : modelData_.meshes) count += static_cast<uint32_t>(mesh.vertices.size());
    return count;
}

uint32_t Model::GetPolygonCount() const {
    uint32_t count = 0;
    for (const auto& mesh : modelData_.meshes) count += static_cast<uint32_t>(mesh.indices.size() / 3);
    return count;
}
// ==========================================
// 読み込み: Assimpのメッシュごとにデータを分ける
// ==========================================
Model::ModelData Model::LoadFile(const std::string& directoryPath, const std::string& filename) {

    ModelData modelData;

    std::string sep = (directoryPath.back() == '/' || directoryPath.back() == '\\') ? "" : "/";
    std::string filePath = directoryPath + sep + filename;

    if (ReadModelCache(filePath, modelData)) {
        modelData.skeleton = CreateSkeleton(modelData.rootNode);
        return modelData;
    }

    Assimp::Importer importer;


    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_ConvertToLeftHanded |
        aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices | aiProcess_OptimizeMeshes);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        return modelData;
    }

    // 1. ノード読み込み
    modelData.rootNode = ReadNode(scene->mRootNode, modelData.nodes);

    // 2. マテリアルの読み込み
    modelData.materials.resize(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* aiMat = scene->mMaterials[i];
        aiString texPath;
        std::string textureFilePath;

        if (aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            textureFilePath = texPath.C_Str();
        }
        else if (aiMat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS) {
            textureFilePath = texPath.C_Str();
        }

        if (!textureFilePath.empty()) {
            std::string texFilename = std::filesystem::path(textureFilePath).filename().string();
            modelData.materials[i].textureFilePath = directoryPath + sep + texFilename;
        }
        else {
            modelData.materials[i].textureFilePath = "Resources/sprite/common/white.png";
        }
    }

    // 3. メッシュの解析
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* aiMesh = scene->mMeshes[i];
        Mesh mesh;
        mesh.materialIndex = aiMesh->mMaterialIndex;

        std::vector<VertexData> tempVertices;
        tempVertices.resize(aiMesh->mNumVertices);

        for (unsigned int v = 0; v < aiMesh->mNumVertices; ++v) {
            VertexData& vertex = tempVertices[v];
            vertex.position = { aiMesh->mVertices[v].x, aiMesh->mVertices[v].y, aiMesh->mVertices[v].z, 1.0f };

            // 法線の取得
            if (aiMesh->HasNormals()) {
                vertex.normal = { aiMesh->mNormals[v].x, aiMesh->mNormals[v].y, aiMesh->mNormals[v].z };
            }
            else {
                vertex.normal = { 0.0f, 1.0f, 0.0f };
            }


            if (aiMesh->HasTangentsAndBitangents()) {
                vertex.tangent = { aiMesh->mTangents[v].x, aiMesh->mTangents[v].y, aiMesh->mTangents[v].z };
            }
            else {
                vertex.tangent = { 1.0f, 0.0f, 0.0f }; // 計算できなかった場合の安全対策(仮のX軸)
            }
            // ==========================================

            // UVの取得
            if (aiMesh->HasTextureCoords(0)) {
                vertex.texcoord = { aiMesh->mTextureCoords[0][v].x, aiMesh->mTextureCoords[0][v].y };
            }
            else {
                vertex.texcoord = { 0.0f, 0.0f };
            }
            vertex.boneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
            vertex.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
        }

        // ボーン解析
        if (aiMesh->mNumBones > 0) {
            modelData.hasSkinning = true;
        }
        for (unsigned int b = 0; b < aiMesh->mNumBones; ++b) {
            aiBone* aiBone = aiMesh->mBones[b];
            std::string boneName = aiBone->mName.C_Str();
            int boneIndex = -1;
            for (int k = 0; k < modelData.bones.size(); ++k) {
                if (modelData.bones[k].name == boneName) {
                    boneIndex = k;
                    break;
                }
            }
            if (boneIndex == -1) {
                boneIndex = (int)modelData.bones.size();
                Bone newBone;
                newBone.name = boneName;
                aiMatrix4x4 offset = aiBone->mOffsetMatrix;
                newBone.inverseBindPoseMatrix.m[0][0] = offset.a1; newBone.inverseBindPoseMatrix.m[0][1] = offset.b1; newBone.inverseBindPoseMatrix.m[0][2] = offset.c1; newBone.inverseBindPoseMatrix.m[0][3] = offset.d1;
                newBone.inverseBindPoseMatrix.m[1][0] = offset.a2; newBone.inverseBindPoseMatrix.m[1][1] = offset.b2; newBone.inverseBindPoseMatrix.m[1][2] = offset.c2; newBone.inverseBindPoseMatrix.m[1][3] = offset.d2;
                newBone.inverseBindPoseMatrix.m[2][0] = offset.a3; newBone.inverseBindPoseMatrix.m[2][1] = offset.b3; newBone.inverseBindPoseMatrix.m[2][2] = offset.c3; newBone.inverseBindPoseMatrix.m[2][3] = offset.d3;
                newBone.inverseBindPoseMatrix.m[3][0] = offset.a4; newBone.inverseBindPoseMatrix.m[3][1] = offset.b4; newBone.inverseBindPoseMatrix.m[3][2] = offset.c4; newBone.inverseBindPoseMatrix.m[3][3] = offset.d4;
                modelData.bones.push_back(newBone);
            }

            for (unsigned int w = 0; w < aiBone->mNumWeights; ++w) {
                unsigned int vertexId = aiBone->mWeights[w].mVertexId;
                float weight = aiBone->mWeights[w].mWeight;
                if (vertexId < tempVertices.size()) {
                    auto& v = tempVertices[vertexId];
                    if (v.boneWeights.x == 0.0f) { v.boneWeights.x = weight; v.boneIndices.x = (float)boneIndex; }
                    else if (v.boneWeights.y == 0.0f) { v.boneWeights.y = weight; v.boneIndices.y = (float)boneIndex; }
                    else if (v.boneWeights.z == 0.0f) { v.boneWeights.z = weight; v.boneIndices.z = (float)boneIndex; }
                    else if (v.boneWeights.w == 0.0f) { v.boneWeights.w = weight; v.boneIndices.w = (float)boneIndex; }
                }
            }
        }

        mesh.vertices = tempVertices;

        for (unsigned int f = 0; f < aiMesh->mNumFaces; f++) {
            aiFace face = aiMesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                // 頂点をコピーするのではなく、頂点の「番号」だけを記録する！
                mesh.indices.push_back(face.mIndices[j]);
            }
        }
        modelData.meshes.push_back(mesh);
    }

    // =========================================================
    // : ボーンがない場合の対処 (ダミーボーン作戦)
    // =========================================================
    if (modelData.bones.empty()) {
        // 1. ダミーボーンを作る (単位行列)
        // アニメーションが適用されるNode名（多くの場合ルートの最初の子）と同じ名前にすることで、
        // ボーン無しの剛体アニメーションも正しく動作するようにする
        Bone dummyBone;
        if (scene->mNumAnimations > 0 && scene->mAnimations[0]->mNumChannels > 0) {
            dummyBone.name = scene->mAnimations[0]->mChannels[0]->mNodeName.C_Str();
            modelData.usesNodeAnimationProxy = true;
        } else if (scene->mRootNode && scene->mRootNode->mNumChildren > 0) {
            dummyBone.name = "__StaticMeshIdentityBone";
        } else {
            dummyBone.name = "__StaticMeshIdentityBone";
        }
        Math math;
        dummyBone.inverseBindPoseMatrix = math.MakeIdentity4x4();
        modelData.bones.push_back(dummyBone);

        // 2. すべての頂点にダミーボーンの影響(100%)を与える
        for (auto& mesh : modelData.meshes) {
            for (auto& v : mesh.vertices) {
                if (v.boneWeights.x == 0.0f && v.boneWeights.y == 0.0f &&
                    v.boneWeights.z == 0.0f && v.boneWeights.w == 0.0f) {

                    v.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
                    v.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
                }
            }
        }
    }

    // =========================================================
    // 4. アニメーションの読み込み 
    // =========================================================
    for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
        aiAnimation* aiAnim = scene->mAnimations[i];
        Animation animation;
        if (aiAnim->mName.length > 0) animation.name = aiAnim->mName.C_Str();
        else animation.name = "animation_" + std::to_string(i);
        float ticksPerSecond = (float)(aiAnim->mTicksPerSecond != 0 ? aiAnim->mTicksPerSecond : 25.0);

        animation.duration = (float)aiAnim->mDuration / ticksPerSecond;
        animation.ticksPerSecond = ticksPerSecond;

        for (unsigned int c = 0; c < aiAnim->mNumChannels; ++c) {
            aiNodeAnim* aiChannel = aiAnim->mChannels[c];
            NodeAnimation nodeAnim;
            nodeAnim.name = aiChannel->mNodeName.C_Str();

            // デバッグログ (必要に応じて残す)
            if (nodeAnim.name.find("Hips") != std::string::npos) {
                std::string log = "AnimNode: " + nodeAnim.name +
                    " | PosKeys: " + std::to_string(aiChannel->mNumPositionKeys) + "\n";
                DebugConsole::GetInstance()->AddLog(log.c_str());
            }



            // Position
            for (unsigned int k = 0; k < aiChannel->mNumPositionKeys; ++k) {
                aiVectorKey& key = aiChannel->mPositionKeys[k];
                KeyframeVector3 kf;
                kf.time = (float)key.mTime / ticksPerSecond;
                kf.value = { key.mValue.x, key.mValue.y, key.mValue.z };
                nodeAnim.translate.push_back(kf);
            }

            // Rotation
            for (unsigned int k = 0; k < aiChannel->mNumRotationKeys; ++k) {
                aiQuatKey& key = aiChannel->mRotationKeys[k];
                KeyframeQuaternion kf;
                kf.time = (float)key.mTime / ticksPerSecond;
                kf.value = { key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w };
                nodeAnim.rotate.push_back(kf);
            }

            // Scaling
            for (unsigned int k = 0; k < aiChannel->mNumScalingKeys; ++k) {
                aiVectorKey& key = aiChannel->mScalingKeys[k];
                KeyframeVector3 kf;
                kf.time = (float)key.mTime / ticksPerSecond;
                kf.value = { key.mValue.x, key.mValue.y, key.mValue.z };
                nodeAnim.scale.push_back(kf);
            }
            animation.nodeAnimations.push_back(nodeAnim);
        }
        modelData.animations.push_back(animation);
    }
    // ★ 追加: 読み込んだノードツリーからSkeleton(1次元配列)を作成する
    modelData.skeleton = CreateSkeleton(modelData.rootNode);

    WriteModelCache(filePath, modelData);

    return modelData;
}

// ノード読み込み
Model::Node Model::ReadNode(aiNode* node, std::vector<Node>& nodes) {
    Node result;
    result.name = node->mName.C_Str();
    aiMatrix4x4 transform = node->mTransformation;
    result.localMatrix.m[0][0] = transform.a1; result.localMatrix.m[0][1] = transform.b1; result.localMatrix.m[0][2] = transform.c1; result.localMatrix.m[0][3] = transform.d1;
    result.localMatrix.m[1][0] = transform.a2; result.localMatrix.m[1][1] = transform.b2; result.localMatrix.m[1][2] = transform.c2; result.localMatrix.m[1][3] = transform.d2;
    result.localMatrix.m[2][0] = transform.a3; result.localMatrix.m[2][1] = transform.b3; result.localMatrix.m[2][2] = transform.c3; result.localMatrix.m[2][3] = transform.d3;
    result.localMatrix.m[3][0] = transform.a4; result.localMatrix.m[3][1] = transform.b4; result.localMatrix.m[3][2] = transform.c4; result.localMatrix.m[3][3] = transform.d4;
    
    aiVector3D scale, translate;
    aiQuaternion rotate;
    transform.Decompose(scale, rotate, translate);
    result.transform.scale = {scale.x, scale.y, scale.z};
    result.transform.rotate = {rotate.x, rotate.y, rotate.z, rotate.w};
    result.transform.translate = {translate.x, translate.y, translate.z};

    nodes.push_back(result);
    result.children.resize(node->mNumChildren);
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        result.children[i] = ReadNode(node->mChildren[i], nodes);
    }
    return result;
}



#include "DirectXCommon.h"

// 毎フレーム呼ぶ更新処理
void Model::Update() {
    Update(false);
}

void Model::Update(bool force) {
    uint32_t currentFrame = DirectXCommon::GetInstance()->GetFrameCount();
    if (!force && lastUpdateFrame_ == currentFrame) {
        return; // すでにこのフレームで更新済み
    }
    lastUpdateFrame_ = currentFrame;

    UpdateSkeleton(modelData_.skeleton);

    //  ボーン情報の更新
    UpdateBoneBuffer();
}

// --- アニメーション計算用ヘルパー ---
// =========================================================
// 座標・スケール用 (Vector3) の補間
// =========================================================
Vector3 Model::CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time) {
    // キーがない場合
    if (keyframes.empty()) {
        return { 0.0f, 0.0f, 0.0f };
    }
    // キーが1つだけ、または時間が最初のキーより前の場合
    if (keyframes.size() == 1 || time <= keyframes[0].time) {
        return keyframes[0].value;
    }

    // 時間の範囲を探す (線形探索)
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        // 現在の時間が、このキーと次のキーの間にあるか？
        if (time >= keyframes[i].time && time <= keyframes[i + 1].time) {
            // 0.0～1.0 の割合(t)を計算
            float t = (time - keyframes[i].time) / (keyframes[i + 1].time - keyframes[i].time);

            // 線形補間 (Lerp)
            Vector3 result;
            result.x = std::lerp(keyframes[i].value.x, keyframes[i + 1].value.x, t);
            result.y = std::lerp(keyframes[i].value.y, keyframes[i + 1].value.y, t);
            result.z = std::lerp(keyframes[i].value.z, keyframes[i + 1].value.z, t);
            return result;
        }
    }

    // 時間が最後のキーを超えている場合は、最後の値を返す
    return keyframes.back().value;
}

// =========================================================
// 回転用 (Quaternion) の補間
// =========================================================
Quaternion Model::CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time) {
    // キーがない場合
    if (keyframes.empty()) {
        return { 0.0f, 0.0f, 0.0f, 1.0f }; // 単位クォータニオン
    }
    // キーが1つ、または時間が最初より前
    if (keyframes.size() == 1 || time <= keyframes[0].time) {
        return keyframes[0].value;
    }

    // 時間の範囲を探す
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        if (time >= keyframes[i].time && time <= keyframes[i + 1].time) {
            float t = (time - keyframes[i].time) / (keyframes[i + 1].time - keyframes[i].time);

            // 球面線形補間 (Slerp)
            return Math::Slerp(keyframes[i].value, keyframes[i + 1].value, t);
        }
    }

    return keyframes.back().value;
}
Model::Node* Model::FindNode(Node& node, const std::string& name) {
    if (node.name == name) return &node;
    for (auto& child : node.children) {
        Node* result = FindNode(child, name);
        if (result) return result;
    }
    return nullptr;
}

void Model::ApplyAnimation(const Animation& animation, float time) {
    ApplyAnimationToSkeleton(modelData_.skeleton, animation, time);
}
const Model::Animation* Model::GetAnimation(const std::string& name) const {
    for (const auto& animation : modelData_.animations) {
        if (animation.name == name) {
            return &animation;
        }
    }
    // 見つからなければ nullptr
    return nullptr;
}

bool Model::HasSkeleton() const {
    return !modelData_.skeleton.joints.empty();
}

const std::vector<Model::Joint>& Model::GetJoints() const {
    return modelData_.skeleton.joints;
}

int Model::FindJointIndex(const std::string& name) const {
    auto it = modelData_.skeleton.jointMap.find(name);
    if (it == modelData_.skeleton.jointMap.end()) {
        return -1;
    }
    return it->second;
}

Model::QuaternionTransform Model::GetJointTransform(int jointIndex) const {
    if (jointIndex < 0 || jointIndex >= static_cast<int>(modelData_.skeleton.joints.size())) {
        return {};
    }
    return modelData_.skeleton.joints[jointIndex].transform;
}

bool Model::SetJointTransform(int jointIndex, const QuaternionTransform& transform) {
    if (jointIndex < 0 || jointIndex >= static_cast<int>(modelData_.skeleton.joints.size())) {
        return false;
    }

    Joint& joint = modelData_.skeleton.joints[jointIndex];
    joint.transform = transform;
    Matrix4x4 mS = math_.MakeScaleMatrix(joint.transform.scale);
    Matrix4x4 mR = math_.MakeRotateQuaternionMatrix(joint.transform.rotate);
    Matrix4x4 mT = math_.MakeTranslateMatrix(joint.transform.translate);
    joint.localMatrix = math_.Multiply(mS, math_.Multiply(mR, mT));
    RebuildSkeletonForEditor();
    return true;
}

void Model::ResetSkeletonPose() {
    modelData_.skeleton = CreateSkeleton(modelData_.rootNode);
    RebuildSkeletonForEditor();
}

void Model::RebuildSkeletonForEditor() {
    UpdateSkeleton(modelData_.skeleton);
    UpdateBoneBuffer();
}

void Model::DrawShadow(ID3D12Resource* wvpResource) {
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // [0] WVP
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource->GetGPUVirtualAddress());

    // [1] ボーン情報
    if (!modelData_.bones.empty()) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 1, boneSrvIndex_);
    }

    // 各メッシュの頂点を描画
    for (auto& mesh : modelData_.meshes) {
        if (!mesh.vertexResource || !mesh.indexResource || mesh.indices.empty()) {
            continue;
        }
        commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
        commandList->IASetIndexBuffer(&mesh.indexBufferView);
        commandList->DrawIndexedInstanced(UINT(mesh.indices.size()), 1, 0, 0, 0);
    }
}

void Model::DrawMeshOnly() {
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // メッシュごとに頂点バッファだけをセットして描画
    for (const auto& mesh : modelData_.meshes) {
        if (!mesh.vertexResource || !mesh.indexResource || mesh.indices.empty()) {
            continue;
        }
        // 頂点バッファをセット
        commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
        commandList->IASetIndexBuffer(&mesh.indexBufferView);
        commandList->DrawIndexedInstanced(UINT(mesh.indices.size()), 1, 0, 0, 0);
    }
}



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
int32_t Model::CreateJoint(const Node& node, const std::optional<int32_t>& parent, std::vector<Joint>& joints) {
    Joint joint;
    joint.name = node.name;
    joint.localMatrix = node.localMatrix;
    
    Math math;
    joint.skeletonSpaceMatrix = math.MakeIdentity4x4();
    joint.transform = node.transform;
    joint.index = int32_t(joints.size()); // 現在登録されている数をIndexに
    joint.parent = parent;

    joints.push_back(joint); // SkeletonのJoint列に追加

    for (const Node& child : node.children) {
        // 子Jointを作成し、そのIndexを登録
        int32_t childIndex = CreateJoint(child, joint.index, joints);
        joints[joint.index].children.push_back(childIndex);
    }
    
    // 自身のIndexを返す
    return joint.index;
}

Model::Skeleton Model::CreateSkeleton(const Node& rootNode) {
    Skeleton skeleton;
    skeleton.root = CreateJoint(rootNode, std::nullopt, skeleton.joints);
    
    // Joint名をキーにしてIndexを引けるMapを作る
    for (const Joint& joint : skeleton.joints) {
        skeleton.jointMap[joint.name] = joint.index;
    }
    
    return skeleton;
}

void Model::UpdateSkeleton(Skeleton& skeleton) {
    Math math;
    // すべてのJointを更新。親が若いので通常ループで処理可能になっている
    for (Joint& joint : skeleton.joints) {
        if (joint.parent) {
            // 親がいれば親の行列を掛ける
            joint.skeletonSpaceMatrix = math.Multiply(joint.localMatrix, skeleton.joints[*joint.parent].skeletonSpaceMatrix);
        } else {
            // 親がいないのでlocalMatrixとskeletonSpaceMatrixは一致する
            joint.skeletonSpaceMatrix = joint.localMatrix;
        }
    }
}

void Model::ApplyAnimationToSkeleton(Skeleton& skeleton, const Animation& animation, float time) {
    Math math;
    for (Joint& joint : skeleton.joints) {
        // 対象のJointのAnimationがあれば、値の適用を行う。
        auto it = std::find_if(animation.nodeAnimations.begin(), animation.nodeAnimations.end(),
            [&joint](const NodeAnimation& na) { return na.name == joint.name; });
            
        if (it != animation.nodeAnimations.end()) {
            const NodeAnimation& rootNodeAnimation = *it;
            joint.transform.translate = CalculateValue(rootNodeAnimation.translate, time);
            joint.transform.rotate = CalculateValue(rootNodeAnimation.rotate, time);
            joint.transform.scale = CalculateValue(rootNodeAnimation.scale, time);
            
            // アニメーションがある場合のみlocalMatrixを更新する
            Matrix4x4 mS = math.MakeScaleMatrix(joint.transform.scale);
            Matrix4x4 mR = math.MakeRotateQuaternionMatrix(joint.transform.rotate);
            Matrix4x4 mT = math.MakeTranslateMatrix(joint.transform.translate);
            joint.localMatrix = math.Multiply(mS, math.Multiply(mR, mT));
        }
    }
}
