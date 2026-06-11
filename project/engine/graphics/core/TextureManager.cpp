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

std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string NormalizeTexturePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string NormalizeTexturePathLower(std::string path) {
    return ToLower(NormalizeTexturePath(std::move(path)));
}

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

bool IsSourceTextureExtension(const std::string& ext) {
    const std::string lowerExt = ToLower(ext);
    return lowerExt == ".png" ||
           lowerExt == ".jpg" ||
           lowerExt == ".jpeg" ||
           lowerExt == ".tga" ||
           lowerExt == ".hdr";
}

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

bool IsEditorPreviewTexture(const std::string& path) {
    const std::string normalized = "/" + NormalizeTexturePathLower(path);
    return normalized.find("/generated/text/_preview_") != std::string::npos ||
           normalized.find("/generated/editor/text_preview/") != std::string::npos;
}

bool IsLinearTexturePath(const std::string& path) {
    const std::string lowerPath = NormalizeTexturePathLower(path);
    return lowerPath.find("normal") != std::string::npos ||
           lowerPath.find("_n") != std::string::npos ||
           lowerPath.find("nor") != std::string::npos ||
           lowerPath.find("arm") != std::string::npos ||
           lowerPath.find("orm") != std::string::npos ||
           lowerPath.find("rough") != std::string::npos ||
           lowerPath.find("metal") != std::string::npos ||
           lowerPath.find("ao") != std::string::npos;
}

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

void StartDDSCacheWatcherIfNeeded() {
    static bool watcherStartTried = false;
    if (watcherStartTried) {
        return;
    }
    watcherStartTried = true;

    std::error_code ec;
    const std::filesystem::path scriptPath =
        (std::filesystem::current_path(ec) / "tools/start_dds_cache_watcher.vbs").lexically_normal();
    if (ec || !std::filesystem::exists(scriptPath)) {
        return;
    }

    const std::wstring parameters = L"\"" + scriptPath.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"wscript.exe", parameters.c_str(), nullptr, SW_HIDE);
}

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

std::string GetTextureBasePath(const std::filesystem::path& path) {
    std::filesystem::path base = path.parent_path() / path.stem();
    return NormalizeTexturePath(base.string());
}
}

/// <summary>
/// テクスチャデータをGPUにアップロードするためのヘルパー関数。
/// </summary>
void UploadTextureData(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Resource** intermediateResource,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList)
{
    // アップロードに必要なサブリソース情報を準備します。
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

TextureManager* TextureManager::GetInstance() {
    static TextureManager instance;
    return &instance;
}

void TextureManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    dxCommon_ = dxCommon;
    device_ = dxCommon->GetDevice();
}

uint32_t TextureManager::Load(const std::string& filePath, bool isNormalMap, bool allowDDSCache, bool forceReload) {
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

    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
    UploadTextureData(resource.Get(), mipImages, &intermediateResource, device_.Get(), dxCommon_->GetCommandList());
    dxCommon_->FlushCommandQueue(true);

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

    if (allowDDSCache && isSourceTexture && !ddsIsUpToDate) {
        AppendDDSCacheRequest(effectiveFilePath, isNormalMap, duration);
    }

    TextureData& newData = textureDatas_[srvHandle];
    newData.filePath = loadPath;
    newData.metadata = metadata;
    newData.resource = resource;
    newData.intermediateResource = intermediateResource;
    newData.srvHandle = srvHandle;

    textureHandleMap_[loadPath] = srvHandle;
    return srvHandle;
}

const DirectX::TexMetadata& TextureManager::GetMetadata(uint32_t textureHandle) {
    auto it = textureDatas_.find(textureHandle);
    assert(it != textureDatas_.end());
    return it->second.metadata;
}

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

std::vector<std::string> TextureManager::GetLoadedTexturePaths() const {
    std::vector<std::string> paths;
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
