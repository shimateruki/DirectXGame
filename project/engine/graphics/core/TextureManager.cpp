#include "TextureManager.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

#include <Windows.h>
#include <shellapi.h>

#include "DirectXCommon.h"
#include "ProfilerManager.h"
#include "SRVManager.h"
#include "d3dx12.h"

#pragma comment(lib, "shell32.lib")

namespace {
const std::filesystem::path kDDSCacheRequestPath = "Resources/.cache/dds_cache_requests.jsonl";
// 文字列を小文字化し、拡張子やパスの比較を大文字小文字に左右されない形へ揃える。

std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}
// Windowsのバックスラッシュをスラッシュへ統一し、テクスチャパスを比較しやすくする。

std::string NormalizeTexturePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}
// 正規化したテクスチャパスを小文字化し、キャッシュ判定用のキーとして扱いやすくする。

std::string NormalizeTexturePathLower(std::string path) {
    return ToLower(NormalizeTexturePath(std::move(path)));
}
// 絶対パスを可能な限りプロジェクト相対パスへ戻し、ログやキャッシュ要求を環境依存にしにくくする。

std::string ToProjectPath(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
    if (ec) {
        return NormalizeTexturePath(path.string());
    }

    absolutePath = absolutePath.lexically_normal();
    std::filesystem::path rootPath = std::filesystem::current_path(ec);
    if (!ec) {
        std::filesystem::path relativePath = absolutePath.lexically_relative(rootPath.lexically_normal());
        if (!relativePath.empty()) {
            return NormalizeTexturePath(relativePath.string());
        }
    }

    return NormalizeTexturePath(absolutePath.string());
}
// DDS生成元として扱う画像拡張子かどうかを判定する。

bool IsSourceTextureExtension(const std::string& ext) {
    const std::string lowerExt = ToLower(ext);
    return lowerExt == ".png" ||
           lowerExt == ".jpg" ||
           lowerExt == ".jpeg" ||
           lowerExt == ".tga" ||
           lowerExt == ".hdr";
}
// DDS指定時に元画像が存在するか探し、古いDDSを元画像から再生成できるようにする。

std::filesystem::path FindSourceTextureForDDS(const std::filesystem::path& ddsPath) {
    static const char* kSourceExts[] = { ".png", ".jpg", ".jpeg", ".tga", ".hdr" };
    for (const char* ext : kSourceExts) {
        std::filesystem::path candidate = ddsPath;
        candidate.replace_extension(ext);
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}
// エディタの一時プレビュー画像かどうかを判定し、不要なDDSキャッシュ生成を避ける。

bool IsEditorPreviewTexture(const std::string& path) {
    const std::string normalized = "/" + NormalizeTexturePathLower(path);
    return normalized.find("/generated/text/_preview_") != std::string::npos ||
           normalized.find("/generated/editor/text_preview/") != std::string::npos;
}
// 法線やマスク系のテクスチャ名を判定し、sRGB変換しないリニア扱いにする。

bool IsLinearTexturePath(const std::string& path) {
    const std::string lowerPath = NormalizeTexturePathLower(path);
    return lowerPath.find("normal") != std::string::npos ||
           lowerPath.find("bakedshader") != std::string::npos ||
           lowerPath.find("_mask") != std::string::npos ||
           lowerPath.find("_noise") != std::string::npos ||
           lowerPath.find("_n") != std::string::npos ||
           lowerPath.find("nor") != std::string::npos ||
           lowerPath.find("arm") != std::string::npos ||
           lowerPath.find("orm") != std::string::npos ||
           lowerPath.find("rough") != std::string::npos ||
           lowerPath.find("metal") != std::string::npos ||
           lowerPath.find("ao") != std::string::npos;
}
// テクスチャ用途に応じて、DDS変換時に使用する圧縮フォーマット名を決める。

std::string GetDDSFormat(const std::string& filePath, bool isNormalMap) {
    const std::string ext = ToLower(std::filesystem::path(filePath).extension().string());
    if (ext == ".hdr") {
        return "BC6H_UF16";
    }
    if (isNormalMap || IsLinearTexturePath(filePath)) {
        return "BC7_UNORM";
    }
    return "BC7_UNORM_SRGB";
}
// DDS生成要求を処理する監視スクリプトを、必要になったタイミングで一度だけ起動する。

void StartDDSCacheWatcherIfNeeded() {
    static bool watcherStartTried = false;
    if (watcherStartTried) {
        return;
    }
    watcherStartTried = true;

    std::error_code ec;
    const std::filesystem::path scriptPath =
        (std::filesystem::current_path(ec) / "tools/dds_cache/start_dds_cache_watcher.vbs").lexically_normal();
    if (ec || !std::filesystem::exists(scriptPath)) {
        return;
    }

    const std::wstring parameters = L"\"" + scriptPath.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"wscript.exe", parameters.c_str(), nullptr, SW_HIDE);
}
// JSONLへ書き込む文字列をエスケープし、パスに記号が含まれても壊れないようにする。

std::string EscapeJson(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}
// 読み込んだ元画像に対応するDDS生成要求をJSONLへ追記し、次回以降の読み込みを軽くする。

void AppendDDSCacheRequest(const std::string& filePath, bool isNormalMap, float durationMs) {
    const std::filesystem::path sourcePath(filePath);
    const std::string ext = ToLower(sourcePath.extension().string());
    if (!IsSourceTextureExtension(ext) || IsEditorPreviewTexture(filePath)) {
        return;
    }

    static std::set<std::string> requestedPaths;
    const std::string source = ToProjectPath(sourcePath);
    if (!requestedPaths.insert(source).second) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(kDDSCacheRequestPath.parent_path(), ec);
    if (ec) {
        return;
    }

    std::filesystem::path ddsPath = sourcePath;
    ddsPath.replace_extension(".dds");

    std::ofstream ofs(kDDSCacheRequestPath, std::ios::app);
    if (!ofs) {
        return;
    }

    ofs << "{\"source\":\"" << EscapeJson(source)
        << "\",\"dds\":\"" << EscapeJson(ToProjectPath(ddsPath))
        << "\",\"format\":\"" << EscapeJson(GetDDSFormat(filePath, isNormalMap))
        << "\",\"durationMs\":" << durationMs
        << "}\n";

    StartDDSCacheWatcherIfNeeded();
}
// 拡張子を除いたテクスチャ基準パスを作り、元画像とDDSの対応確認に使う。

std::string GetTextureBasePath(const std::filesystem::path& path) {
    std::filesystem::path base = path.parent_path() / path.stem();
    return NormalizeTexturePath(base.string());
}
}
// CPU側で読み込んだミップ画像をGPUテクスチャへ転送し、シェーダーから読める状態へ遷移させる。

void UploadTextureData(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Resource** intermediateResource,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList)
{
    // アップロードに必要なサブリソース情報を準備します。
    // ミップごとの転送情報をDirectXTexから作成する。
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

    // 転送完了後はシェーダー読み取り用の状態へ遷移させる。
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);
}

// 実行中の描画コマンドリストを触らず、テクスチャ転送専用の一時コマンドリストでGPUへ送る。
bool UploadTextureDataWithDedicatedCommandList(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue)
{
    if (!texture || !device || !commandQueue) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> uploadAllocator;
    HRESULT hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(uploadAllocator.GetAddressOf()));
    if (FAILED(hr)) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
    hr = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        uploadAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(uploadCommandList.GetAddressOf()));
    if (FAILED(hr)) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
    UploadTextureData(texture, mipImages, intermediateResource.GetAddressOf(), device, uploadCommandList.Get());

    hr = uploadCommandList->Close();
    if (FAILED(hr)) {
        return false;
    }

    ID3D12CommandList* commandLists[] = { uploadCommandList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
    if (FAILED(hr)) {
        return false;
    }

    constexpr UINT64 fenceValue = 1;
    hr = commandQueue->Signal(fence.Get(), fenceValue);
    if (FAILED(hr)) {
        return false;
    }

    if (fence->GetCompletedValue() < fenceValue) {
        HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!fenceEvent) {
            return false;
        }

        hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
        if (FAILED(hr)) {
            CloseHandle(fenceEvent);
            return false;
        }

        WaitForSingleObject(fenceEvent, INFINITE);
        CloseHandle(fenceEvent);
    }

    return true;
}
// TextureManagerのシングルトンインスタンスを返す。

TextureManager* TextureManager::GetInstance() {
    static TextureManager instance;
    return &instance;
}
// DirectXCommonからデバイスを受け取り、テクスチャ生成に必要な参照を保持する。

void TextureManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    device_ = dxCommon->GetDevice();
}
// テクスチャを読み込み、必要ならDDSキャッシュを優先しながらSRVハンドルとして登録する。

uint32_t TextureManager::Load(const std::string& filePath, bool isNormalMap, bool allowDDSCache, bool forceReload) {
    // 名前からリニア扱いが必要なテクスチャを自動判定する。
    if (!isNormalMap && IsLinearTexturePath(filePath)) {
        isNormalMap = true;
    }

    std::filesystem::path path(filePath);
    const bool requestedDDS = ToLower(path.extension().string()) == ".dds";
    if (requestedDDS) {
        const std::filesystem::path sourcePath = FindSourceTextureForDDS(path);
        if (!sourcePath.empty()) {
            const bool ddsMissing = !std::filesystem::exists(path);
            const bool ddsOutdated = !ddsMissing && std::filesystem::last_write_time(sourcePath) > std::filesystem::last_write_time(path);
            if (ddsMissing || ddsOutdated) {
                path = sourcePath;
            }
        }
    }

    const std::string effectiveFilePath = NormalizeTexturePath(path.string());
    if (!isNormalMap && IsLinearTexturePath(effectiveFilePath)) {
        isNormalMap = true;
    }

    const std::string ext = ToLower(path.extension().string());
    const bool isSourceTexture = IsSourceTextureExtension(ext);

    std::filesystem::path ddsPath = path;
    ddsPath.replace_extension(".dds");

    std::string loadPath = effectiveFilePath;
    bool ddsIsUpToDate = false;

    // 元画像より新しいDDSがある場合は、重い変換を避けてDDSを優先する。
    if (allowDDSCache && isSourceTexture && std::filesystem::exists(ddsPath)) {
        if (!std::filesystem::exists(path)) {
            loadPath = NormalizeTexturePath(ddsPath.string());
            ddsIsUpToDate = true;
        } else {
            const auto srcTime = std::filesystem::last_write_time(path);
            const auto dstTime = std::filesystem::last_write_time(ddsPath);
            if (srcTime <= dstTime) {
                loadPath = NormalizeTexturePath(ddsPath.string());
                ddsIsUpToDate = true;
            }
        }
    }

    if (!forceReload) {
        auto it = textureHandleMap_.find(loadPath);
        if (it != textureHandleMap_.end()) {
            return it->second;
        }
    }

    const auto start = std::chrono::high_resolution_clock::now();

    const bool forceSRGB = !isNormalMap && ToLower(std::filesystem::path(loadPath).extension().string()) != ".hdr";
    DirectX::ScratchImage mipImages = dxCommon_->LoadTexture(loadPath, forceSRGB);
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = dxCommon_->CreateTextureResource(metadata);
    if (!resource) {
        return 0;
    }

    if (!UploadTextureDataWithDedicatedCommandList(resource.Get(), mipImages, device_.Get(), dxCommon_->GetCommandQueue())) {
        return 0;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    if (metadata.IsCubemap()) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = UINT(metadata.mipLevels);
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    } else if (metadata.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    }

    uint32_t srvHandle = 0;
    auto existingIt = textureHandleMap_.find(loadPath);
    if (forceReload && existingIt != textureHandleMap_.end()) {
        srvHandle = existingIt->second;
        SRVManager::GetInstance()->CreateSRVforResource(srvHandle, resource.Get(), srvDesc);
    } else {
        srvHandle = SRVManager::GetInstance()->CreateSRV(resource.Get(), srvDesc);
    }

    const auto end = std::chrono::high_resolution_clock::now();
    float duration = std::chrono::duration<float, std::milli>(end - start).count();
    ProfilerManager::GetInstance()->RecordLoadTime("Sprite", filePath, duration);

    // まだ有効なDDSがない元画像は、裏側でDDS生成できるよう要求を積む。
    if (allowDDSCache && isSourceTexture && !ddsIsUpToDate) {
        AppendDDSCacheRequest(effectiveFilePath, isNormalMap, duration);
    }

    TextureData& newData = textureDatas_[srvHandle];
    newData.filePath = loadPath;
    newData.metadata = metadata;
    newData.resource = resource;
    newData.srvHandle = srvHandle;

    textureHandleMap_[loadPath] = srvHandle;
    return srvHandle;
}
// 読み込み済みテクスチャのメタデータを取得し、サイズやミップ情報を参照できるようにする。

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureHandle) {
    auto it = textureDatas_.find(textureHandle);
    assert(it != textureDatas_.end());
    return it->second.metadata;
}
// 指定ディレクトリ配下の画像を走査し、元画像とDDSの重複を避けながらまとめて読み込む。

void TextureManager::LoadAllTexture(const std::string& directoryPath) {
    if (!std::filesystem::exists(directoryPath)) {
        return;
    }

    std::vector<std::filesystem::path> files;
    std::set<std::string> sourceBaseNames;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(directoryPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::filesystem::path path = entry.path();
        files.push_back(path);

        const std::string ext = ToLower(path.extension().string());
        if (IsSourceTextureExtension(ext) && !IsEditorPreviewTexture(path.string())) {
            sourceBaseNames.insert(GetTextureBasePath(path));
        }
    }

    std::sort(files.begin(), files.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
        return lhs.generic_string() < rhs.generic_string();
    });

    for (const auto& path : files) {
        const std::string pathString = NormalizeTexturePath(path.string());
        if (IsEditorPreviewTexture(pathString)) {
            continue;
        }

        const std::string ext = ToLower(path.extension().string());
        if (ext == ".dds") {
            if (sourceBaseNames.count(GetTextureBasePath(path)) != 0) {
                continue;
            }
            Load(pathString, false);
            continue;
        }

        if (IsSourceTextureExtension(ext)) {
            Load(pathString, IsLinearTexturePath(pathString));
        }
    }
}
// 現在読み込み済みのテクスチャパス一覧を返し、エディタやデバッグ表示で利用する。

std::vector<std::string> TextureManager::GetLoadedTexturePaths() const {
    std::vector<std::string> paths;
    for (const auto& pair : textureHandleMap_) {
        paths.push_back(pair.first);
    }
    return paths;
}
// 指定パスに対応するSRVハンドルを返し、元画像名で問い合わせた場合はDDS登録も補助的に探す。

uint32_t TextureManager::GetSrvHandle(const std::string& filePath) {
    auto it = textureHandleMap_.find(filePath);
    if (it != textureHandleMap_.end()) {
        return it->second;
    }

    std::filesystem::path ddsPath(filePath);
    if (IsSourceTextureExtension(ddsPath.extension().string())) {
        ddsPath.replace_extension(".dds");
        it = textureHandleMap_.find(NormalizeTexturePath(ddsPath.string()));
        if (it != textureHandleMap_.end()) {
            return it->second;
        }
    }

    return 0;
}
