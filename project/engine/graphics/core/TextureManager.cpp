#include "TextureManager.h"
#include <cassert>
#include "SRVManager.h"
#include "d3dx12.h"
#include "DirectXCommon.h" 
#include <filesystem>
#include <algorithm>
#include "ProfilerManager.h"
#include <set>

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



uint32_t TextureManager::Load(const std::string& filePath, bool isNormalMap) {

    // 0. 自動判定: 引数がfalseでもファイル名から推測する
    if (!isNormalMap) {
        std::string lowerPath = filePath;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
        if (lowerPath.find("normal") != std::string::npos ||
            lowerPath.find("_n") != std::string::npos ||
            lowerPath.find("nor") != std::string::npos ||  // nor_dx 等
            lowerPath.find("arm") != std::string::npos ||  // AO, Roughness, Metal
            lowerPath.find("orm") != std::string::npos) { // ORMもデータテクスチャなのでLinearに
            isNormalMap = true;
        }
    }

    // 1. すでに DDS パスが渡されているか、対応する DDS が存在するか確認
    std::filesystem::path path(filePath);
    std::filesystem::path ddsPath = path;
    ddsPath.replace_extension(".dds");

    std::string loadPath = filePath;
    bool alreadyHasDDS = std::filesystem::exists(ddsPath);
    bool ddsIsUpToDate = false;

    // DDS が存在する場合、タイムスタンプを比較
    if (alreadyHasDDS) {
        auto srcTime = std::filesystem::last_write_time(path);
        auto dstTime = std::filesystem::last_write_time(ddsPath);
        if (srcTime <= dstTime) {
            loadPath = ddsPath.string(); // DDSの方が新しい（または同じ）なら採用
            ddsIsUpToDate = true;
        }
    }

    // 1. 過去に読み込み済みのテクスチャか検索
    auto it = textureHandleMap_.find(loadPath);
    if (it != textureHandleMap_.end()) {
        return it->second;
    }

    // --- 計測開始 ---
    auto start = std::chrono::high_resolution_clock::now();

    // 2. テクスチャファイルを読み込み、リソースを作成
    DirectX::ScratchImage mipImages = dxCommon_->LoadTexture(loadPath);
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
    } else if (metadata.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    }


    // SRVManagerにSRVの作成を依頼し、返ってきた「本物のハンドル」を取得
    uint32_t srvHandle = SRVManager::GetInstance()->CreateSRV(resource.Get(), srvDesc);

    // --- 計測終了 ---
    auto end = std::chrono::high_resolution_clock::now();
    float duration = std::chrono::duration<float, std::milli>(end - start).count();
    ProfilerManager::GetInstance()->RecordLoadTime("Sprite", filePath, duration);

    // 4. 重いテクスチャ（かつDDSが未生成または古い）なら、次回の為にDDS変換を実行
    const float kThresholdMs = 20.0f; // 20ms を超えたら重いと判定
    if (!ddsIsUpToDate && duration > kThresholdMs) {
        // 0. 自動判定: 引数がfalseでもファイル名から推測する
        if (!isNormalMap) {
            if (filePath.find("Normal") != std::string::npos || 
                filePath.find("_n") != std::string::npos ||
                filePath.find("nor") != std::string::npos ||  // nor_dx 等
                filePath.find("arm") != std::string::npos ||  // AO, Roughness, Metal
                filePath.find("ORM") != std::string::npos) { // ORMもデータテクスチャなのでLinearに
                isNormalMap = true;
            }
        }
        // バックグラウンド的にDDS生成 (今回は同期実行だが、パスだけ返す)
        ConvertToDDS(filePath, isNormalMap);
    }

    // 5. 新しいテクスチャデータをmapに格納

    TextureData& newData = textureDatas_[srvHandle];
    newData.filePath = loadPath;
    newData.metadata = metadata;
    newData.resource = resource;
    newData.intermediateResource = intermediateResource;
    newData.srvHandle = srvHandle;

    // 5. ファイルパスと「本物のハンドル」の対応をマップに記録
    textureHandleMap_[loadPath] = srvHandle;

    // 6. 「本物のハンドル」を返す
    return srvHandle;
}

std::string TextureManager::ConvertToDDS(const std::string& filePath, bool isNormalMap) {
    std::filesystem::path path(filePath);
    std::string ext = path.extension().string();

    // すでに DDS なら何もしない
    if (ext == ".dds") return filePath;

    // サポートされている形式以外なら何もしない
    if (ext != ".png" && ext != ".jpg" && ext != ".tga" && ext != ".hdr") return filePath;

    // 出力先 DDS パスの生成 (元のファイルと同じ場所に .dds を作る)
    std::filesystem::path ddsPath = path;
    ddsPath.replace_extension(".dds");

    // 更新が必要かチェック (DDSがない、または元ファイルより古い)
    bool needUpdate = false;
    if (!std::filesystem::exists(ddsPath)) {
        needUpdate = true;
    }
    else {
        auto srcTime = std::filesystem::last_write_time(path);
        auto dstTime = std::filesystem::last_write_time(ddsPath);
        if (srcTime > dstTime) {
            needUpdate = true;
        }
    }

    if (needUpdate) {
        // Texconv を実行 (Windows環境のパス形式に合わせる)
        std::string tool = "Resources\\tools\\Texconv.exe";
        if (!std::filesystem::exists(tool)) return filePath; // ツールがなければ諦めて元のをロード

        std::string format = isNormalMap ? "BC7_UNORM" : "BC7_UNORM_SRGB";

        // HDR の場合は特殊処理 (BC6H)
        if (ext == ".hdr") format = "BC6H_UF16";

        std::string outputDir = path.parent_path().string();

        // コマンド構築
        // -f: 形式指定, -y: 上書き許可, -o: 出力先, -m: ミップマップ生成(デフォルト)
        std::string command = tool + " -f " + format + " -y -o \"" + outputDir + "\" \"" + filePath + "\"";

        // 実行 (コンソールウィンドウを一瞬出さないために、本来は CreateProcess 等が良いが、一旦 system)
        std::system(command.c_str());
    }

    return ddsPath.string();
}

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureHandle) {
    auto it = textureDatas_.find(textureHandle);
    assert(it != textureDatas_.end());
    return it->second.metadata;
}

void TextureManager::LoadAllTexture(const std::string& directoryPath) {
    if (std::filesystem::exists(directoryPath)) {
        std::vector<std::string> files;
        std::set<std::string> ddsBaseNames;

        // 1次スキャン：全ファイル取得とDDSの存在確認
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directoryPath)) {
            if (entry.is_regular_file()) {
                std::string path = entry.path().string();
                std::replace(path.begin(), path.end(), '\\', '/');
                files.push_back(path);

                if (entry.path().extension() == ".dds") {
                    std::string base = entry.path().parent_path().string() + "/" + entry.path().stem().string();
                    std::replace(base.begin(), base.end(), '\\', '/');
                    ddsBaseNames.insert(base);
                }
            }
        }

        // 2次スキャン：フィルタリングしてロード
        for (const std::string& path : files) {
            std::filesystem::path p(path);
            std::string ext = p.extension().string();
            std::string base = p.parent_path().string() + "/" + p.stem().string();
            std::replace(base.begin(), base.end(), '\\', '/');

            // 同じ名前のDDSがあれば、元の画像(png/jpg/hdr)はロードしない
            if ((ext == ".png" || ext == ".jpg" || ext == ".hdr") && ddsBaseNames.count(base)) {
                continue;
            }

            if (ext == ".png" || ext == ".jpg" || ext == ".hdr" || ext == ".dds") {
                bool isNormal = (path.find("Normal") != std::string::npos || 
                                 path.find("_n") != std::string::npos ||
                                 path.find("ARM") != std::string::npos ||
                                 path.find("ORM") != std::string::npos);
                Load(path, isNormal);
            }
        }
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