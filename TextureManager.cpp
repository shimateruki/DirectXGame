#include "TextureManager.h"
#include <cassert>
#include "externals/DirectXTex/d3dx12.h"

void UploadTextureData(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Resource** intermediateResource,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList)
{
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    HRESULT hr = DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
    assert(SUCCEEDED(hr));

    uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));

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

    UpdateSubresources(commandList, texture, *intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;

    commandList->ResourceBarrier(1, &barrier);
}


// シングルトンインスタンス
TextureManager* TextureManager::instance_ = nullptr;

TextureManager* TextureManager::GetInstance() {
    if (instance_ == nullptr) {
        instance_ = new TextureManager();
    }
    return instance_;
}

void TextureManager::DestroyInstance() {
    delete instance_;
    instance_ = nullptr;
}

void TextureManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    device_ = dxCommon_->GetDevice();
    srvDescriptorHeap_ = dxCommon_->GetSrvDescriptorHeap();
    descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    textureDatas_.reserve(DirectXCommon::kMaxSRVCount);
}

uint32_t TextureManager::Load(const std::string& filePath) {
    auto it = textureHandleMap_.find(filePath);
    if (it != textureHandleMap_.end()) {
        return it->second;
    }

    // ★★★ 資料の「最大数チェック」を反映 ★★★
    assert(textureDatas_.size() + kSRVIndexTop < DirectXCommon::kMaxSRVCount);

    // ハンドルを計算（vectorの現在の要素数がそのまま次のインデックスになる）
    uint32_t handle = static_cast<uint32_t>(textureDatas_.size()) + kSRVIndexTop;

    textureDatas_.emplace_back();
    TextureData& newData = textureDatas_.back();
    newData.filePath = filePath;

    DirectX::ScratchImage mipImages = dxCommon_->LoadTexture(filePath);
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    newData.metadata = metadata; // 読み込んだメタデータを保存);
    newData.resource = dxCommon_->CreateTextureResource(metadata);
    UploadTextureData(
        newData.resource.Get(), mipImages, &newData.intermediateResource,
        device_.Get(), dxCommon_->GetCommandList());

    // ★★★ 資料の「SRVインデックス計算」を反映 ★★★
    uint32_t srvIndex = handle; // ハンドルがそのままSRVインデックスになる
    newData.srvHandleCPU = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    newData.srvHandleCPU.ptr += (descriptorSizeSRV_ * srvIndex);
    newData.srvHandleGPU = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    newData.srvHandleGPU.ptr += (descriptorSizeSRV_ * srvIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    device_->CreateShaderResourceView(newData.resource.Get(), &srvDesc, newData.srvHandleCPU);

    textureHandleMap_[filePath] = handle;

    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetGPUHandle(uint32_t textureHandle) {
    // ハンドルは1から始まるので、vectorのインデックスに合わせるために -kSRVIndexTop
    assert(textureHandle >= kSRVIndexTop && textureHandle < textureDatas_.size() + kSRVIndexTop);
    return textureDatas_[textureHandle - kSRVIndexTop].srvHandleGPU;
}

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureHandle) {
    assert(textureHandle >= kSRVIndexTop && textureHandle < textureDatas_.size() + kSRVIndexTop);
    return textureDatas_[textureHandle - kSRVIndexTop].metadata;
}