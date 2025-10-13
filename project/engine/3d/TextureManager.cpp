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
    // 1. 過去に読み込み済みのテクスチャか検索
    auto it = textureHandleMap_.find(filePath);
    if (it != textureHandleMap_.end()) {
        // 読み込み済みなら、そのハンドル（SRVハンドル）を返す
        return it->second;
    }

    // ★★★★★ ここからが新しいロジック ★★★★★

    // 2. テクスチャファイルを読み込み、リソースを作成
    DirectX::ScratchImage mipImages = dxCommon_->LoadTexture(filePath);
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = dxCommon_->CreateTextureResource(metadata);
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
    UploadTextureData(
        resource.Get(), mipImages, &intermediateResource,
        device_.Get(), dxCommon_->GetCommandList());

    // 3. SRVを作成し、GPU上の正しいハンドルを取得
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

    // SRVManagerにSRVの作成を依頼し、返ってきた「本物のハンドル」を取得
    uint32_t srvHandle = SRVManager::GetInstance()->CreateSRV(resource.Get(), srvDesc);

    // 4. 新しいテクスチャデータをmapに格納 (キーは本物のハンドル)
    TextureData& newData = textureDatas_[srvHandle];
    newData.filePath = filePath;
    newData.metadata = metadata;
    newData.resource = resource;
    newData.intermediateResource = intermediateResource;
    newData.srvHandle = srvHandle;

    // 5. ファイルパスと「本物のハンドル」の対応をマップに記録
    textureHandleMap_[filePath] = srvHandle;

    // 6. 「本物のハンドル」を返す
    return srvHandle;
}

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureHandle) {
    // ★★★ 修正点：mapから検索 ★★★
    auto it = textureDatas_.find(textureHandle);
    // ハンドルがマップ内に存在するかチェック
    assert(it != textureDatas_.end());
    return it->second.metadata;
}