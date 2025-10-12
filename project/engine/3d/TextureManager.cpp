#include "TextureManager.h"
#include <cassert>
#include "engine/base/SRVManager.h"
#include "externals/DirectXTex/d3dx12.h"

/// <summary>
/// テクスチャデータをGPUにアップロードするためのヘルパー関数
/// </summary>
void UploadTextureData(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Resource** intermediateResource,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList)
{
    // アップロードに必要なサブリソースの情報を準備
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    HRESULT hr = DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
    assert(SUCCEEDED(hr));

    // 中間リソースに必要なサイズを計算
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));

    // --- 中間リソースの作成 ---
    D3D12_HEAP_PROPERTIES uploadHeapProperties{};
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = intermediateSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(intermediateResource));
    assert(SUCCEEDED(hr));

    // --- データの転送 ---
    UpdateSubresources(commandList, texture, *intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());

    // --- リソースバリア ---
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);
}


TextureManager* TextureManager::GetInstance() {
    static TextureManager instance;
    return &instance;
}

void TextureManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    device_ = dxCommon->GetDevice();
}

uint32_t TextureManager::Load(const std::string& filePath) {
    // 1. 多重読み込みチェック
    auto it = textureHandleMap_.find(filePath);
    if (it != textureHandleMap_.end()) {
        return it->second;
    }

    // 2. 新規読み込み
    // ハンドルを計算（vectorの現在の要素数がそのまま次のインデックスになる）
    uint32_t handle = static_cast<uint32_t>(textureDatas_.size());

    // 新しいテクスチャデータを格納
    textureDatas_.emplace_back();
    TextureData& newData = textureDatas_.back();
    newData.filePath = filePath;

    // テクスチャファイルを読み込み、ミップマップを生成
    DirectX::ScratchImage mipImages = dxCommon_->LoadTexture(filePath);
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    newData.metadata = metadata;

    // GPU上にテクスチャリソースを作成
    newData.resource = dxCommon_->CreateTextureResource(metadata);
    // 作成したリソースに、読み込んだテクスチャデータをアップロード
    UploadTextureData(
        newData.resource.Get(), mipImages, &newData.intermediateResource,
        device_.Get(), dxCommon_->GetCommandList());

    // 3. SRV (Shader Resource View) の作成
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

    // SRVManagerにSRVの作成を依頼し、返ってきたハンドルを保存
    newData.srvHandle = SRVManager::GetInstance()->CreateSRV(newData.resource.Get(), srvDesc);

    // ファイルパスとハンドルの対応をマップに記録
    textureHandleMap_[filePath] = handle;

    return handle;
}

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureHandle) {
    assert(textureHandle < textureDatas_.size());
    return textureDatas_[textureHandle].metadata;
}