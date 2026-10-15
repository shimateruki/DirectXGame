#define NOMINMAX
#include <fstream>
#include "Model.h"
#include "DirectXCommon.h"
#include "RenderStats.h"
#include "engine/utility/math/Math.h"
#include <sstream>
#include <cassert>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include "SRVManager.h"
#include <DebugConsole.h>
#include <LightManager.h>

namespace {
constexpr const char* kDefaultWhiteTexture = "Resources/sprite/common/white.png";

// 元の材質値を基準に倍率へ変換し、ゼロ付近では除算を避けます。
float CalculateMaterialMultiplier(float value, float reference) {
    constexpr float kEpsilon = 1.0e-5f;
    if (std::abs(reference) > kEpsilon) {
        return value / reference;
    }
    return std::abs(value) <= kEpsilon ? 1.0f : value;
}

// 描画中に同じフォールバック画像のファイル確認を繰り返さないよう、SRVを一度だけ解決します。
uint32_t GetDefaultWhiteTextureHandle() {
    static uint32_t handle = 0;
    if (handle == 0) {
        handle = TextureManager::GetInstance()->Load(kDefaultWhiteTexture);
    }
    return handle;
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
    InitializeFromCpuData(common, LoadFile(directoryPath, filename), filename);
}

Model::ModelData Model::LoadCpuData(const std::string& directoryPath, const std::string& filename) {
    return LoadFile(directoryPath, filename);
}

void Model::InitializeFromCpuData(
    ModelCommon* common,
    ModelData modelData,
    const std::string& sourceName) {
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // 1. ワーカースレッドまたは同期経路で読み込んだCPUデータを受け取る
    modelData_ = std::move(modelData);
    modelData_.meshes.erase(
        std::remove_if(modelData_.meshes.begin(), modelData_.meshes.end(),
            [](const Mesh& mesh) {
                return mesh.vertices.empty() || mesh.indices.empty();
            }),
        modelData_.meshes.end());

    // --- AABB（モデルのサイズ）計算 ---
    engine::Vector3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
    engine::Vector3 max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
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
            DebugConsole::GetInstance()->AddLog("Model vertex buffer creation failed: " + sourceName);
            continue;
        }
        mesh.vertexBufferView.BufferLocation = mesh.vertexResource->GetGPUVirtualAddress();
        mesh.vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * mesh.vertices.size());
        mesh.vertexBufferView.StrideInBytes = sizeof(VertexData);

        VertexData* vertexData = nullptr;
        HRESULT vertexMapResult = mesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
        if (FAILED(vertexMapResult) || !vertexData) {
            DebugConsole::GetInstance()->AddLog("Model vertex buffer map failed: " + sourceName);
            mesh.vertexResource.Reset();
            continue;
        }
        std::memcpy(vertexData, mesh.vertices.data(), sizeof(VertexData) * mesh.vertices.size());
        mesh.vertexResource->Unmap(0, nullptr);

        // インデックスバッファ
        mesh.indexResource = dxCommon->CreateBufferResource(sizeof(uint32_t) * mesh.indices.size());
        if (!mesh.indexResource) {
            DebugConsole::GetInstance()->AddLog("Model index buffer creation failed: " + sourceName);
            mesh.vertexResource.Reset();
            continue;
        }
        mesh.indexBufferView.BufferLocation = mesh.indexResource->GetGPUVirtualAddress();
        mesh.indexBufferView.SizeInBytes = UINT(sizeof(uint32_t) * mesh.indices.size());
        mesh.indexBufferView.Format = DXGI_FORMAT_R32_UINT;

        uint32_t* indexData = nullptr;
        HRESULT indexMapResult = mesh.indexResource->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
        if (FAILED(indexMapResult) || !indexData) {
            DebugConsole::GetInstance()->AddLog("Model index buffer map failed: " + sourceName);
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
        DebugConsole::GetInstance()->AddLog("Model material buffer creation failed: " + sourceName);
        return;
    }

    HRESULT materialMapResult = materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    if (FAILED(materialMapResult) || !materialData_) {
        DebugConsole::GetInstance()->AddLog("Model material buffer map failed: " + sourceName);
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
    CreateComputeSkinningResources();
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
    engine::Math math;
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
        computeSkinningDirty_ = true;
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
    computeSkinningDirty_ = true;
}

// モデルの描画処理
void Model::CreateComputeSkinningResources() {
    // 再生成前に旧出力を捨て、未対応モデルではCPU側の頂点バッファを使える状態へ戻す。
    computeSkinningAvailable_ = false;
    computeSkinningDirty_ = true;
    computeSkinningOutputReady_ = false;

    for (auto& mesh : modelData_.meshes) {
        mesh.computeSkinnedVertexResource.Reset();
        mesh.computeSkinnedVertexBufferView = {};
        mesh.computeSkinnedVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (!common_ || !common_->IsComputeSkinningAvailable() ||
        !modelData_.hasSkinning || !boneResource_ || modelData_.bones.empty()) {
        return;
    }

    ID3D12Device* device = common_->GetDxCommon()->GetDevice();
    bool hasSkinnedMesh = false;

    for (auto& mesh : modelData_.meshes) {
        if (!mesh.vertexResource || mesh.vertices.empty()) {
            continue;
        }

        // スキニング結果を頂点数分だけGPUのUAVへ書き込めるように確保する。
        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = sizeof(VertexData) * mesh.vertices.size();
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        const HRESULT result = device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(mesh.computeSkinnedVertexResource.GetAddressOf()));
        if (FAILED(result)) {
            // 一部のMeshだけ有効になる状態を避け、全Meshを一括でフォールバックさせる。
            DebugConsole::GetInstance()->AddLog("Compute skinning output buffer creation failed.");
            for (auto& resetMesh : modelData_.meshes) {
                resetMesh.computeSkinnedVertexResource.Reset();
                resetMesh.computeSkinnedVertexBufferView = {};
                resetMesh.computeSkinnedVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }
            return;
        }

        mesh.computeSkinnedVertexBufferView.BufferLocation =
            mesh.computeSkinnedVertexResource->GetGPUVirtualAddress();
        mesh.computeSkinnedVertexBufferView.SizeInBytes =
            static_cast<UINT>(sizeof(VertexData) * mesh.vertices.size());
        mesh.computeSkinnedVertexBufferView.StrideInBytes = sizeof(VertexData);
        hasSkinnedMesh = true;
    }

    computeSkinningAvailable_ = hasSkinnedMesh;
}

bool Model::PrepareComputeSkinning() {
    // ボーン更新がないフレームは前回の計算結果を再利用する。
    if (!computeSkinningAvailable_ || !common_ ||
        !common_->IsComputeSkinningAvailable() || !boneResource_) {
        return false;
    }
    if (!computeSkinningDirty_ && computeSkinningOutputReady_) {
        return true;
    }

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetComputeRootSignature(common_->GetComputeSkinningRootSignature());
    commandList->SetPipelineState(common_->GetComputeSkinningPipelineState());
    commandList->SetComputeRootShaderResourceView(1, boneResource_->GetGPUVirtualAddress());

    bool dispatched = false;
    for (auto& mesh : modelData_.meshes) {
        if (!mesh.vertexResource || !mesh.computeSkinnedVertexResource || mesh.vertices.empty()) {
            continue;
        }

        if (mesh.computeSkinnedVertexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            // 前フレームの描画で頂点入力になっているため、書き込み可能な状態へ戻す。
            D3D12_RESOURCE_BARRIER toUnorderedAccess{};
            toUnorderedAccess.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toUnorderedAccess.Transition.pResource = mesh.computeSkinnedVertexResource.Get();
            toUnorderedAccess.Transition.StateBefore = mesh.computeSkinnedVertexState;
            toUnorderedAccess.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            toUnorderedAccess.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &toUnorderedAccess);
            mesh.computeSkinnedVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        commandList->SetComputeRootShaderResourceView(0, mesh.vertexResource->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(
            2,
            mesh.computeSkinnedVertexResource->GetGPUVirtualAddress());

        const uint32_t dispatchConstants[2] = {
            static_cast<uint32_t>(mesh.vertices.size()),
            static_cast<uint32_t>(modelData_.bones.size())
        };
        commandList->SetComputeRoot32BitConstants(
            3,
            static_cast<UINT>(std::size(dispatchConstants)),
            dispatchConstants,
            0);

        const uint32_t groupCount =
            (static_cast<uint32_t>(mesh.vertices.size()) + 63u) / 64u;
        // 1グループ64頂点で計算し、UAV完了を待ってから描画用頂点バッファへ遷移する。
        commandList->Dispatch(groupCount, 1, 1);
        engine::RenderStats::GetInstance()->RecordComputeDispatch(groupCount);

        D3D12_RESOURCE_BARRIER unorderedAccessBarrier{};
        unorderedAccessBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        unorderedAccessBarrier.UAV.pResource = mesh.computeSkinnedVertexResource.Get();
        commandList->ResourceBarrier(1, &unorderedAccessBarrier);

        D3D12_RESOURCE_BARRIER toVertexBuffer{};
        toVertexBuffer.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toVertexBuffer.Transition.pResource = mesh.computeSkinnedVertexResource.Get();
        toVertexBuffer.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toVertexBuffer.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        toVertexBuffer.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &toVertexBuffer);
        mesh.computeSkinnedVertexState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        dispatched = true;
    }

    computeSkinningOutputReady_ = dispatched;
    computeSkinningDirty_ = !dispatched;
    return dispatched;
}

const D3D12_VERTEX_BUFFER_VIEW& Model::GetActiveVertexBufferView(const Mesh& mesh) const {
    if (computeSkinningAvailable_ && computeSkinningOutputReady_ && !computeSkinningDirty_ &&
        mesh.computeSkinnedVertexResource) {
        return mesh.computeSkinnedVertexBufferView;
    }
    return mesh.vertexBufferView;
}

void Model::Draw(ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource, ID3D12Resource* cameraResource, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, ID3D12Resource* overrideMaterialResource, uint32_t normalMapHandle, uint32_t ormMapHandle, uint32_t overrideTextureHandle, uint32_t instanceCount, uint32_t startInstanceLocation, int meshDrawIndex)
{
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    engine::RenderStats::GetInstance()->RecordObjectDraw();
    // 1. マテリアル設定
    if (overrideMaterialResource) {
        commandList->SetGraphicsRootConstantBufferView(0, overrideMaterialResource->GetGPUVirtualAddress());
        commandList->SetGraphicsRootConstantBufferView(14, overrideMaterialResource->GetGPUVirtualAddress());
    }
    else if (materialResource_) {
        commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
        commandList->SetGraphicsRootConstantBufferView(14, materialResource_->GetGPUVirtualAddress());
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

        std::array<float, 6> meshMaterialFactors = {
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f,
            0.0f
        };
        const MaterialData* primaryMaterial = GetPrimaryMaterialData();
        const bool usesModelTexture =
            overrideTextureHandle == 0 || overrideTextureHandle >= DirectXCommon::kMaxSRVCount;
        if (usesModelTexture && meshMaterial && primaryMaterial) {
            meshMaterialFactors[0] = CalculateMaterialMultiplier(
                meshMaterial->baseColorFactor.x, primaryMaterial->baseColorFactor.x);
            meshMaterialFactors[1] = CalculateMaterialMultiplier(
                meshMaterial->baseColorFactor.y, primaryMaterial->baseColorFactor.y);
            meshMaterialFactors[2] = CalculateMaterialMultiplier(
                meshMaterial->baseColorFactor.z, primaryMaterial->baseColorFactor.z);
            meshMaterialFactors[3] = CalculateMaterialMultiplier(
                meshMaterial->baseColorFactor.w, primaryMaterial->baseColorFactor.w);
            meshMaterialFactors[4] = CalculateMaterialMultiplier(
                meshMaterial->roughness, primaryMaterial->roughness);
            meshMaterialFactors[5] = meshMaterial->metallic - primaryMaterial->metallic;
        }
        commandList->SetGraphicsRoot32BitConstants(
            13,
            static_cast<UINT>(meshMaterialFactors.size()),
            meshMaterialFactors.data(),
            0);

        uint32_t meshNormalHandle = normalMapHandle;
        if ((meshNormalHandle == 0 || meshNormalHandle >= DirectXCommon::kMaxSRVCount) &&
            meshMaterial && meshMaterial->hasNormalMap &&
            meshMaterial->normalMapHandle > 0 && meshMaterial->normalMapHandle < DirectXCommon::kMaxSRVCount) {
            meshNormalHandle = meshMaterial->normalMapHandle;
        }
        if (meshNormalHandle == 0 || meshNormalHandle >= DirectXCommon::kMaxSRVCount) {
            meshNormalHandle = GetDefaultWhiteTextureHandle();
        }
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 9, meshNormalHandle);

        uint32_t meshOrmHandle = ormMapHandle;
        if ((meshOrmHandle == 0 || meshOrmHandle >= DirectXCommon::kMaxSRVCount) &&
            meshMaterial && meshMaterial->hasOrmMap &&
            meshMaterial->ormMapHandle > 0 && meshMaterial->ormMapHandle < DirectXCommon::kMaxSRVCount) {
            meshOrmHandle = meshMaterial->ormMapHandle;
        }
        if (meshOrmHandle == 0 || meshOrmHandle >= DirectXCommon::kMaxSRVCount) {
            meshOrmHandle = GetDefaultWhiteTextureHandle();
        }
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 10, meshOrmHandle);

        const D3D12_VERTEX_BUFFER_VIEW& activeVertexBufferView = GetActiveVertexBufferView(mesh);
        commandList->IASetVertexBuffers(0, 1, &activeVertexBufferView);
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
        engine::RenderStats::GetInstance()->RecordIndexedDraw(
            static_cast<uint32_t>(mesh.indices.size()), instanceCount);
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
