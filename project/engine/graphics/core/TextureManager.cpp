#include "TextureManager.h"
#include <cassert>
#include "SRVManager.h"
#include "d3dx12.h"
#include "DirectXCommon.h" 
#include <filesystem>
#include <chrono>
#include <debugapi.h>
#include <format>
#include "DebugConsole.h"

/// <summary>
/// 繝・け繧ｹ繝√Ε繝・・繧ｿ繧竪PU縺ｫ繧｢繝・・繝ｭ繝ｼ繝峨☆繧九◆繧√・繝倥Ν繝代・髢｢謨ｰ
/// </summary>
void UploadTextureData(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Resource** intermediateResource,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList)
{
    // 繧｢繝・・繝ｭ繝ｼ繝峨↓蠢・ｦ√↑繧ｵ繝悶Μ繧ｽ繝ｼ繧ｹ縺ｮ諠・ｱ繧呈ｺ門ｙ
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    HRESULT hr = DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
    assert(SUCCEEDED(hr));

    // 荳ｭ髢薙Μ繧ｽ繝ｼ繧ｹ縺ｫ蠢・ｦ√↑繧ｵ繧､繧ｺ繧定ｨ育ｮ・
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));

    // --- 荳ｭ髢薙Μ繧ｽ繝ｼ繧ｹ縺ｮ菴懈・ ---
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

    // --- 繝・・繧ｿ縺ｮ霆｢騾・---
    UpdateSubresources(commandList, texture, *intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());

    // --- 繝ｪ繧ｽ繝ｼ繧ｹ繝舌Μ繧｢ ---
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
    // 蜆ｪ蜈育噪縺ｫ隱ｭ縺ｿ霎ｼ繧繝代せ繧呈ｱｺ螳夲ｼ・DS縺後≠繧後・縺昴▲縺｡繧剃ｽｿ縺・ｼ・
    std::string pathToLoad = filePath;
    
    std::filesystem::path p(filePath);
    if (p.extension() == ".png" || p.extension() == ".jpg") {
        std::filesystem::path ddsPath = p;
        ddsPath.replace_extension(".dds");
        if (std::filesystem::exists(ddsPath)) {
            pathToLoad = ddsPath.string();
            
        }
    }

    // 1. 縺吶〒縺ｫ隱ｭ縺ｿ霎ｼ縺ｿ貂医∩縺ｮ繝・け繧ｹ繝√Ε縺区､懃ｴ｢・亥､画鋤蠕後・繝代せ縺ｧ繝√ぉ繝・け・・
    std::replace(pathToLoad.begin(), pathToLoad.end(), '\\', '/');
    auto it = textureHandleMap_.find(pathToLoad);
    if (it != textureHandleMap_.end()) {
        return it->second;
    }

    // --- 險域ｸｬ髢句ｧ・---
    auto start = std::chrono::high_resolution_clock::now();

    // 2. 繝・け繧ｹ繝√Ε繝輔ぃ繧､繝ｫ繧定ｪｭ縺ｿ霎ｼ縺ｿ縲√Μ繧ｽ繝ｼ繧ｹ繧剃ｽ懈・
    DirectX::ScratchImage mipImages = dxCommon_->LoadTexture(pathToLoad);
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = dxCommon_->CreateTextureResource(metadata);
    if (!resource) {
        return 0;
    }
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;


    UploadTextureData(
        resource.Get(), mipImages, &intermediateResource,
        device_.Get(), dxCommon_->GetLoadCommandList()); //ロード用コマンドリスト

    // FlushCommandQueue(true) 繧貞他縺ｳ蜃ｺ縺・
    dxCommon_->ExecuteLoadCommands(); //専用実行関数


    // 3. SRV繧剃ｽ懈・縺励；PU荳翫・豁｣縺励＞繝上Φ繝峨Ν繧貞叙蠕・
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    // 繧ｭ繝･繝ｼ繝悶・繝・・蟇ｾ蠢・
    if (metadata.IsCubemap() || (metadata.arraySize == 6 && metadata.width == metadata.height)) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = UINT(metadata.mipLevels);
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    }
    else if (metadata.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    }


    // SRVManager縺ｫSRV縺ｮ菴懈・繧剃ｾ晞ｼ縺励∬ｿ斐▲縺ｦ縺阪◆縲梧悽迚ｩ縺ｮ繝上Φ繝峨Ν縲阪ｒ蜿門ｾ・
    uint32_t srvHandle = SRVManager::GetInstance()->CreateSRV(resource.Get(), srvDesc);

    // 4. 譁ｰ縺励＞繝・け繧ｹ繝√Ε繝・・繧ｿ繧知ap縺ｫ譬ｼ邏・
    TextureData& newData = textureDatas_[srvHandle];
    newData.filePath = filePath;
    newData.metadata = metadata;
    newData.resource = resource;
    newData.intermediateResource = intermediateResource;
    newData.srvHandle = srvHandle;

    // 5. 繝輔ぃ繧､繝ｫ繝代せ縺ｨ縲梧悽迚ｩ縺ｮ繝上Φ繝峨Ν縲阪・蟇ｾ蠢懊ｒ繝槭ャ繝励↓險倬鹸
    textureHandleMap_[pathToLoad] = srvHandle;

    // 繧ゅ＠PNG繧呈欠螳壹＠縺ｦDDS繧定ｪｭ繧薙□縺ｪ繧峨∝・縺ｮ繝代せ縺ｧ繧ょｼ輔￠繧九ｈ縺・↓縺励※縺翫￥
    if (pathToLoad != filePath) {
        textureHandleMap_[filePath] = srvHandle;
    }

    // --- 險域ｸｬ邨ゆｺ・---
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diff = end - start;

    // 蜃ｺ蜉・
    std::string log = std::format("[TextureManager] Load: {} (Format: {}, {:.2f} ms)\\n", filePath, (int)metadata.format, diff.count());
    DebugConsole::GetInstance()->AddLog(log);

    // 6. 縲梧悽迚ｩ縺ｮ繝上Φ繝峨Ν縲阪ｒ霑斐☆
    return srvHandle;
}

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureHandle) {
    auto it = textureDatas_.find(textureHandle);
    assert(it != textureDatas_.end());
    return it->second.metadata;
}

void TextureManager::LoadAllTexture(const std::string& directoryPath) {
    if (std::filesystem::exists(directoryPath)) {
        // --- 蜈ｨ菴楢ｨ域ｸｬ髢句ｧ・---
        auto totalStart = std::chrono::high_resolution_clock::now();
        int count = 0;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(directoryPath)) {
            // 繝輔か繝ｫ繝縺ｧ縺ｯ縺ｪ縺上後ヵ繧｡繧､繝ｫ縲阪□縺｣縺溷ｴ蜷医・縺ｿ蜃ｦ逅・
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".png" || ext == ".jpg" || ext == ".dds") {
                    std::string path = entry.path().string();
                    std::replace(path.begin(), path.end(), '\\', '/');
                    Load(path);
                    count++;
                }
            }
        }

        // --- 蜈ｨ菴楢ｨ域ｸｬ邨ゆｺ・---
        auto totalEnd = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> totalDiff = totalEnd - totalStart;

        std::string totalLog = std::format("========== [TextureManager] Total Load Time: {:.3f} s ({} files) ==========\n",
            totalDiff.count(), count);
        DebugConsole::GetInstance()->AddLog(totalLog);
    }
}

std::vector<std::string> TextureManager::GetLoadedTexturePaths() const {
    std::vector<std::string> paths;
    // textureHandleMap_ 縺ｮ繧ｭ繝ｼ(繝代せ)繧貞・驛ｨ髮・ａ縺ｦ霑斐☆
    for (const auto& pair : textureHandleMap_) {
        paths.push_back(pair.first);
    }
    return paths;
}

uint32_t TextureManager::GetSrvHandle(const std::string& filePath) {
        std::string normalizedPath = filePath;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    auto it = textureHandleMap_.find(normalizedPath);
    if (it != textureHandleMap_.end()) {
        return it->second;
    }
    // 隕九▽縺九ｉ縺ｪ縺九▲縺溷ｴ蜷医・0繧定ｿ斐☆・育ｵｶ蟇ｾ縺ｫLoad繧貞他縺ｰ縺ｪ縺・ｼ・ｼ・
    return 0;
}
