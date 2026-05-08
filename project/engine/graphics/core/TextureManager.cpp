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
    // 優先的に読み込むパスを決定（DDSがあればそっちを使う）
    std::string pathToLoad = filePath;
    std::filesystem::path p(filePath);
    if (p.extension() == ".png" || p.extension() == ".jpg") {
        std::filesystem::path ddsPath = p;
        ddsPath.replace_extension(".dds");
        if (std::filesystem::exists(ddsPath)) {
            pathToLoad = ddsPath.string();
            std::replace(pathToLoad.begin(), pathToLoad.end(), '\\', '/');
        }
    }

    // 1. すでに読み込み済みのテクスチャか検索（変換後のパスでチェック）
    auto it = textureHandleMap_.find(pathToLoad);
    if (it != textureHandleMap_.end()) {
        return it->second;
    }

    // --- 計測開始 ---
    auto start = std::chrono::high_resolution_clock::now();

    // 2. テクスチャファイルを読み込み、リソースを作成
    DirectX::ScratchImage mipImages = dxCommon_->LoadTexture(pathToLoad);
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = dxCommon_->CreateTextureResource(metadata);
    if (!resource) {
        return 0;
    }
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;


    UploadTextureData(
        resource.Get(), mipImages, &intermediateResource,
        device_.Get(), dxCommon_->GetCommandList()); //コマンドが積まれる

    // FlushCommandQueue(true) を呼び出す
    dxCommon_->FlushCommandQueue(true);


    // 3. SRVを作成し、GPU上の正しいハンドルを取得
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    // キューブマップ対応
    if (metadata.IsCubemap()) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = UINT(metadata.mipLevels);
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    }
    else if (metadata.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    }


    // SRVManagerにSRVの作成を依頼し、返ってきた「本物のハンドル」を取得
    uint32_t srvHandle = SRVManager::GetInstance()->CreateSRV(resource.Get(), srvDesc);

    // 4. 新しいテクスチャデータをmapに格納
    TextureData& newData = textureDatas_[srvHandle];
    newData.filePath = filePath;
    newData.metadata = metadata;
    newData.resource = resource;
    newData.intermediateResource = intermediateResource;
    newData.srvHandle = srvHandle;

    // 5. ファイルパスと「本物のハンドル」の対応をマップに記録
    textureHandleMap_[pathToLoad] = srvHandle;

    // もしPNGを指定してDDSを読んだなら、元のパスでも引けるようにしておく
    if (pathToLoad != filePath) {
        textureHandleMap_[filePath] = srvHandle;
    }

    // --- 計測終了 ---
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diff = end - start;

    // 出力
    std::string log = std::format("[TextureManager] Load: {} ({:.2f} ms)\n", filePath, diff.count());
    DebugConsole::GetInstance()->AddLog(log);

    // 6. 「本物のハンドル」を返す
    return srvHandle;
}

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureHandle) {
    auto it = textureDatas_.find(textureHandle);
    assert(it != textureDatas_.end());
    return it->second.metadata;
}

void TextureManager::LoadAllTexture(const std::string& directoryPath) {
    if (std::filesystem::exists(directoryPath)) {
        // --- 全体計測開始 ---
        auto totalStart = std::chrono::high_resolution_clock::now();
        int count = 0;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(directoryPath)) {
            // フォルダではなく「ファイル」だった場合のみ処理
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

        // --- 全体計測終了 ---
        auto totalEnd = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> totalDiff = totalEnd - totalStart;

        std::string totalLog = std::format("========== [TextureManager] Total Load Time: {:.3f} s ({} files) ==========\n",
            totalDiff.count(), count);
        DebugConsole::GetInstance()->AddLog(totalLog);
    }
}

std::vector<std::string> TextureManager::GetLoadedTexturePaths() const {
    std::vector<std::string> paths;
    // textureHandleMap_ のキー(パス)を全部集めて返す
    for (const auto& pair : textureHandleMap_) {
        paths.push_back(pair.first);
    }
    return paths;
}

uint32_t TextureManager::GetSrvHandle(const std::string& filePath) {
    auto it = textureHandleMap_.find(filePath);
    if (it != textureHandleMap_.end()) {
        return it->second;
    }
    // 見つからなかった場合は0を返す（絶対にLoadを呼ばない！）
    return 0;
}