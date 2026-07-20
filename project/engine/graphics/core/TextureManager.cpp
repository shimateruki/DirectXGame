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
const std::filesystem::path kDDSCacheDisableFlagPath = "Resources/.cache/disable_dds_cache.flag";
// この時間未満の読み込みはDDS化しても効果が薄いため、変換要求を出さない。
constexpr float kDDSCacheRequestMinDurationMs = 2.0f;
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
// 原因調査用に、フラグファイルだけでDDSキャッシュの使用を一時停止できるようにする。

bool IsDDSCacheDisabled() {
    std::error_code ec;
    return std::filesystem::exists(kDDSCacheDisableFlagPath, ec) && !ec;
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

std::string GetDDSFormat(const std::string& filePath, bool isLinearTexture) {
    const std::string ext = ToLower(std::filesystem::path(filePath).extension().string());
    if (ext == ".hdr") {
        return "BC6H_UF16";
    }
    if (isLinearTexture) {
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
    // 読み込みが軽い画像はDDS化しても効果が薄いため、変換キューへ積まない。
    if (durationMs < kDDSCacheRequestMinDurationMs) {
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
    ID3D12GraphicsCommandList* commandList,
    D3D12_RESOURCE_STATES finalState)
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

    // Copy QueueではCOMMONまで戻し、Direct Queueの初回参照時に読み取り状態へ昇格させます。
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = finalState;
    commandList->ResourceBarrier(1, &barrier);
}

std::string MakeTextureCacheKey(const std::string& path, bool isLinearTexture) {
    return NormalizeTexturePath(path) + (isLinearTexture ? "|linear" : "|srgb");
}

struct ResolvedTexturePath {
    std::string effectiveFilePath;
    std::string loadPath;
    std::string cacheKey;
    bool isLinearTexture = false;
    bool useDDSCache = false;
    bool isSourceTexture = false;
    bool ddsIsUpToDate = false;
};

ResolvedTexturePath ResolveTexturePath(
    const std::string& filePath,
    TextureManager::TextureColorSpace colorSpace,
    bool allowDDSCache) {
    ResolvedTexturePath resolved;
    resolved.isLinearTexture = colorSpace == TextureManager::TextureColorSpace::Linear ||
        (colorSpace == TextureManager::TextureColorSpace::Auto && IsLinearTexturePath(filePath));

    std::filesystem::path path(filePath);
    resolved.useDDSCache = allowDDSCache && !IsDDSCacheDisabled();
    const bool requestedDDS = ToLower(path.extension().string()) == ".dds";
    if (requestedDDS) {
        const std::filesystem::path sourcePath = FindSourceTextureForDDS(path);
        if (!sourcePath.empty()) {
            const bool ddsMissing = !std::filesystem::exists(path);
            const bool ddsOutdated = !ddsMissing &&
                std::filesystem::last_write_time(sourcePath) > std::filesystem::last_write_time(path);
            if (!resolved.useDDSCache || ddsMissing || ddsOutdated) {
                path = sourcePath;
            }
        }
    }

    resolved.effectiveFilePath = NormalizeTexturePath(path.string());
    if (colorSpace == TextureManager::TextureColorSpace::Auto &&
        IsLinearTexturePath(resolved.effectiveFilePath)) {
        resolved.isLinearTexture = true;
    }

    const std::string extension = ToLower(path.extension().string());
    resolved.isSourceTexture = IsSourceTextureExtension(extension);
    std::filesystem::path ddsPath = path;
    ddsPath.replace_extension(".dds");
    resolved.loadPath = resolved.effectiveFilePath;

    if (resolved.useDDSCache && resolved.isSourceTexture && std::filesystem::exists(ddsPath)) {
        if (!std::filesystem::exists(path)) {
            resolved.loadPath = NormalizeTexturePath(ddsPath.string());
            resolved.ddsIsUpToDate = true;
        }
        else {
            const auto sourceTime = std::filesystem::last_write_time(path);
            const auto ddsTime = std::filesystem::last_write_time(ddsPath);
            if (sourceTime <= ddsTime) {
                resolved.loadPath = NormalizeTexturePath(ddsPath.string());
                resolved.ddsIsUpToDate = true;
            }
        }
    }

    if (ToLower(std::filesystem::path(resolved.loadPath).extension().string()) == ".hdr") {
        resolved.isLinearTexture = true;
    }
    resolved.cacheKey = MakeTextureCacheKey(resolved.loadPath, resolved.isLinearTexture);
    return resolved;
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
    UploadTextureData(
        texture,
        mipImages,
        intermediateResource.GetAddressOf(),
        device,
        uploadCommandList.Get(),
        D3D12_RESOURCE_STATE_GENERIC_READ);

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

    D3D12_COMMAND_QUEUE_DESC copyQueueDesc{};
    copyQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    copyQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    copyQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    copyQueueDesc.NodeMask = 0;

    HRESULT hr = device_->CreateCommandQueue(
        &copyQueueDesc,
        IID_PPV_ARGS(copyCommandQueue_.GetAddressOf()));
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) {
        copyCommandQueue_.Reset();
        return;
    }

    hr = device_->CreateFence(
        0,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(copyFence_.GetAddressOf()));
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) {
        copyCommandQueue_.Reset();
        copyFence_.Reset();
    }
}

bool TextureManager::BeginAsyncUploadBatch() {
    if (!device_ || !copyCommandQueue_ || !copyFence_ || asyncUploadBatchSubmitted_) {
        return false;
    }
    if (asyncUploadBatchActive_) {
        return true;
    }

    asyncTextureUploads_.clear();
    asyncUploadAllocator_.Reset();
    asyncUploadCommandList_.Reset();

    HRESULT hr = device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_COPY,
        IID_PPV_ARGS(asyncUploadAllocator_.GetAddressOf()));
    if (FAILED(hr)) {
        return false;
    }

    hr = device_->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_COPY,
        asyncUploadAllocator_.Get(),
        nullptr,
        IID_PPV_ARGS(asyncUploadCommandList_.GetAddressOf()));
    if (FAILED(hr)) {
        asyncUploadAllocator_.Reset();
        return false;
    }

    pendingCopyFenceValue_ = 0;
    asyncUploadBatchActive_ = true;
    asyncUploadRecording_ = false;
    asyncUploadBatchSubmitted_ = false;
    return true;
}

void TextureManager::SetAsyncUploadRecording(bool enabled) {
    asyncUploadRecording_ = enabled && asyncUploadBatchActive_ && !asyncUploadBatchSubmitted_;
}

bool TextureManager::RecordAsyncTextureUpload(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages) {
    if (!texture || !device_ || !asyncUploadCommandList_ ||
        !asyncUploadBatchActive_ || !asyncUploadRecording_ || asyncUploadBatchSubmitted_) {
        return false;
    }

    AsyncTextureUpload upload;
    upload.destination = texture;
    UploadTextureData(
        texture,
        mipImages,
        upload.intermediate.GetAddressOf(),
        device_.Get(),
        asyncUploadCommandList_.Get(),
        D3D12_RESOURCE_STATE_COMMON);
    if (!upload.intermediate) {
        return false;
    }

    asyncTextureUploads_.push_back(std::move(upload));
    return true;
}

bool TextureManager::SubmitAsyncUploadBatch() {
    if (!asyncUploadBatchActive_) {
        return true;
    }
    if (asyncUploadBatchSubmitted_) {
        return true;
    }
    if (!asyncUploadCommandList_ || !copyCommandQueue_ || !copyFence_) {
        return false;
    }

    asyncUploadRecording_ = false;
    HRESULT hr = asyncUploadCommandList_->Close();
    if (FAILED(hr)) {
        return false;
    }

    ID3D12CommandList* commandLists[] = { asyncUploadCommandList_.Get() };
    copyCommandQueue_->ExecuteCommandLists(1, commandLists);

    pendingCopyFenceValue_ = ++copyFenceValue_;
    hr = copyCommandQueue_->Signal(copyFence_.Get(), pendingCopyFenceValue_);
    if (FAILED(hr)) {
        pendingCopyFenceValue_ = 0;
        return false;
    }

    asyncUploadBatchSubmitted_ = true;
    return true;
}

bool TextureManager::PollAsyncUploadBatch() {
    if (!asyncUploadBatchActive_) {
        return true;
    }
    if (!asyncUploadBatchSubmitted_ || !copyFence_) {
        return false;
    }
    if (copyFence_->GetCompletedValue() < pendingCopyFenceValue_) {
        return false;
    }

    ReleaseCompletedAsyncUploadBatch();
    return true;
}

void TextureManager::WaitForAsyncUploadBatch() {
    if (!asyncUploadBatchActive_) {
        return;
    }
    if (!asyncUploadBatchSubmitted_ && !SubmitAsyncUploadBatch()) {
        ReleaseCompletedAsyncUploadBatch();
        return;
    }

    if (copyFence_ && copyFence_->GetCompletedValue() < pendingCopyFenceValue_) {
        HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent) {
            const HRESULT hr = copyFence_->SetEventOnCompletion(
                pendingCopyFenceValue_,
                fenceEvent);
            if (SUCCEEDED(hr)) {
                WaitForSingleObject(fenceEvent, INFINITE);
            }
            CloseHandle(fenceEvent);
        }
    }

    ReleaseCompletedAsyncUploadBatch();
}

bool TextureManager::IsAsyncUploadBatchPending() const {
    return asyncUploadBatchActive_;
}

void TextureManager::ReleaseCompletedAsyncUploadBatch() {
    asyncUploadRecording_ = false;
    asyncUploadBatchSubmitted_ = false;
    asyncUploadBatchActive_ = false;
    pendingCopyFenceValue_ = 0;
    asyncTextureUploads_.clear();
    asyncUploadCommandList_.Reset();
    asyncUploadAllocator_.Reset();
}
// テクスチャを読み込み、必要ならDDSキャッシュを優先しながらSRVハンドルとして登録する。

uint32_t TextureManager::Load(const std::string& filePath, bool isNormalMap, bool allowDDSCache, bool forceReload) {
    return LoadInternal(
        filePath,
        isNormalMap ? TextureColorSpace::Linear : TextureColorSpace::Auto,
        allowDDSCache,
        forceReload);
}

uint32_t TextureManager::Load(
    const std::string& filePath,
    TextureColorSpace colorSpace,
    bool allowDDSCache,
    bool forceReload) {
    return LoadInternal(filePath, colorSpace, allowDDSCache, forceReload);
}

bool TextureManager::Prepare(
    const std::string& filePath,
    TextureColorSpace colorSpace,
    bool allowDDSCache) {
    const ResolvedTexturePath resolved = ResolveTexturePath(filePath, colorSpace, allowDDSCache);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (textureHandleMap_.contains(resolved.cacheKey) ||
            preparedTextures_.contains(resolved.cacheKey)) {
            return true;
        }
    }

    const auto start = std::chrono::high_resolution_clock::now();
    auto prepared = std::make_unique<PreparedTextureData>();
    prepared->mipImages = DirectXCommon::LoadTexture(
        resolved.loadPath,
        !resolved.isLinearTexture);
    const auto end = std::chrono::high_resolution_clock::now();
    prepared->decodeDurationMs = std::chrono::duration<float, std::milli>(end - start).count();
    if (prepared->mipImages.GetImageCount() == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (textureHandleMap_.contains(resolved.cacheKey) ||
        preparedTextures_.contains(resolved.cacheKey)) {
        return true;
    }
    preparedTextures_.emplace(resolved.cacheKey, std::move(prepared));
    return true;
}

uint32_t TextureManager::LoadInternal(
    const std::string& filePath,
    TextureColorSpace colorSpace,
    bool allowDDSCache,
    bool forceReload) {
    const ResolvedTexturePath resolved = ResolveTexturePath(filePath, colorSpace, allowDDSCache);
    const bool isLinearTexture = resolved.isLinearTexture;
    const bool useDDSCache = resolved.useDDSCache;
    const bool isSourceTexture = resolved.isSourceTexture;
    const bool ddsIsUpToDate = resolved.ddsIsUpToDate;
    const std::string& effectiveFilePath = resolved.effectiveFilePath;
    const std::string& loadPath = resolved.loadPath;
    const std::string& cacheKey = resolved.cacheKey;

    if (!forceReload) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = textureHandleMap_.find(cacheKey);
        if (it != textureHandleMap_.end()) {
            return it->second;
        }
    }

    const auto start = std::chrono::high_resolution_clock::now();

    // 法線・マスク系以外は見た目の色を保つためsRGBとして読み込む。
    const bool forceSRGB = !isLinearTexture;
    DirectX::ScratchImage mipImages;
    float preparedDecodeDurationMs = 0.0f;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto prepared = preparedTextures_.find(cacheKey);
        if (prepared != preparedTextures_.end()) {
            preparedDecodeDurationMs = prepared->second->decodeDurationMs;
            mipImages = std::move(prepared->second->mipImages);
            preparedTextures_.erase(prepared);
        }
    }
    if (mipImages.GetImageCount() == 0) {
        mipImages = DirectXCommon::LoadTexture(loadPath, forceSRGB);
    }
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = dxCommon_->CreateTextureResource(metadata);
    if (!resource) {
        return 0;
    }

    if (asyncUploadRecording_) {
        if (!RecordAsyncTextureUpload(resource.Get(), mipImages)) {
            return 0;
        }
    }
    else if (!UploadTextureDataWithDedicatedCommandList(
                 resource.Get(),
                 mipImages,
                 device_.Get(),
                 dxCommon_->GetCommandQueue())) {
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
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 他スレッドが読み込み中に同じテクスチャを先に登録していた場合は、そのハンドルを使う。
        auto existingIt = textureHandleMap_.find(cacheKey);
        if (!forceReload && existingIt != textureHandleMap_.end()) {
            return existingIt->second;
        }

        // forceReload時は既存SRV番号を再利用し、参照しているSpriteやMaterialを壊さない。
        if (forceReload && existingIt != textureHandleMap_.end()) {
            srvHandle = existingIt->second;
            SRVManager::GetInstance()->CreateSRVforResource(srvHandle, resource.Get(), srvDesc);
        } else {
            srvHandle = SRVManager::GetInstance()->CreateSRV(resource.Get(), srvDesc);
        }

        TextureData& newData = textureDatas_[srvHandle];
        newData.filePath = loadPath;
        newData.metadata = metadata;
        newData.resource = resource;
        newData.srvHandle = srvHandle;

        textureHandleMap_[cacheKey] = srvHandle;
    }

    const auto end = std::chrono::high_resolution_clock::now();
    float duration = preparedDecodeDurationMs +
        std::chrono::duration<float, std::milli>(end - start).count();
    ProfilerManager::GetInstance()->RecordLoadTime("Sprite", filePath, duration);

    // まだ有効なDDSがない元画像は、裏側でDDS生成できるよう要求を積む。
    if (useDDSCache && isSourceTexture && !ddsIsUpToDate) {
        AppendDDSCacheRequest(effectiveFilePath, isLinearTexture, duration);
    }
    return srvHandle;
}
// 読み込み済みテクスチャのメタデータを取得し、サイズやミップ情報を参照できるようにする。

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureHandle) {
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);

    std::set<std::string> uniquePaths;
    for (const auto& pair : textureDatas_) {
        uniquePaths.insert(pair.second.filePath);
    }
    return { uniquePaths.begin(), uniquePaths.end() };
}

std::vector<std::string> TextureManager::GetAvailableTexturePaths() const {
    std::set<std::string> uniquePaths;
    for (const std::string& loadedPath : GetLoadedTexturePaths()) {
        uniquePaths.insert(NormalizeTexturePath(loadedPath));
    }

    const std::filesystem::path roots[] = {
        "Resources/sprite",
        "Resources/texture"
    };
    std::error_code error;
    for (const std::filesystem::path& root : roots) {
        if (!std::filesystem::exists(root, error) || error) {
            error.clear();
            continue;
        }

        for (std::filesystem::recursive_directory_iterator iterator(
                 root,
                 std::filesystem::directory_options::skip_permission_denied,
                 error), end;
             iterator != end;
             iterator.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }
            if (!iterator->is_regular_file(error) || error) {
                error.clear();
                continue;
            }

            const std::string extension = ToLower(iterator->path().extension().string());
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                extension == ".tga" || extension == ".dds" || extension == ".hdr") {
                uniquePaths.insert(ToProjectPath(iterator->path()));
            }
        }
    }

    return { uniquePaths.begin(), uniquePaths.end() };
}
// 指定パスに対応するSRVハンドルを返し、元画像名で問い合わせた場合はDDS登録も補助的に探す。

uint32_t TextureManager::GetSrvHandle(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto findHandle = [this](const std::string& path) -> uint32_t {
        const bool autoLinear = IsLinearTexturePath(path);
        auto it = textureHandleMap_.find(MakeTextureCacheKey(path, autoLinear));
        if (it != textureHandleMap_.end()) {
            return it->second;
        }
        it = textureHandleMap_.find(MakeTextureCacheKey(path, !autoLinear));
        return it != textureHandleMap_.end() ? it->second : 0;
    };

    const std::string normalizedPath = NormalizeTexturePath(filePath);
    if (const uint32_t handle = findHandle(normalizedPath); handle != 0) {
        return handle;
    }

    std::filesystem::path ddsPath(normalizedPath);
    if (IsSourceTextureExtension(ddsPath.extension().string())) {
        ddsPath.replace_extension(".dds");
        if (const uint32_t handle = findHandle(NormalizeTexturePath(ddsPath.string())); handle != 0) {
            return handle;
        }
    }

    return 0;
}
