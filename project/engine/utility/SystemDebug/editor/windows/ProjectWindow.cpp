#define NOMINMAX
#include "ProjectWindow.h"
#include "PresetEditor.h"
#include "TextureManager.h"
#include <limits>
#include <cfloat>
#include <functional>
#include "DebugEditor.h"
#include "imgui.h"
#include "ModelManager.h"
#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3d.h"
#include "EffectObject3d.h"
#include "MeshRenderer.h"
#include "WinApp.h"
#include "CameraManager.h"
#include "Object3dCommon.h"
#include "engine/graphics/3d/light/Light.h"
#include "engine/graphics/3d/light/LightManager.h"
#include "IconsFontAwesome5.h"
#include "AssetDatabase.h"
#include "EditorAssetDragPayload.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>


namespace fs = std::filesystem;

namespace {
// 大きなカタログでもSRVと描画先を増やし続けず、GPUが読み終えた画像を再利用します。
constexpr size_t kPresetThumbnailCacheLimit = 96;
constexpr size_t kPbrThumbnailCacheLimit = 64;
std::map<std::string, uint64_t> presetThumbnailUseFence;
// HUD 用モデル画像の生成設定。表示姿勢・拡縮・位置調整と出力名をまとめる。
struct HudPortraitSpec {
    const char* presetName;
    const char* fileStem;
    float yaw;
    float pitch;
    float scale;
    float offsetY;
};

constexpr std::array<HudPortraitSpec, 6> kHudPortraitSpecs = {
    HudPortraitSpec{ "Builtin/Enemy/Slime", "slime", 0.0f, -0.12f, 1.08f, 0.0f },
    HudPortraitSpec{ "Builtin/Enemy/Bomber", "bomber", 0.0f, -0.12f, 1.08f, 0.0f },
    HudPortraitSpec{ "Builtin/Enemy/GiantSlime", "giant_slime", 0.0f, -0.12f, 1.24f, 0.0f },
    HudPortraitSpec{ "Builtin/Enemy/FireSlime", "fire_slime", 0.0f, -0.12f, 1.08f, 0.0f },
    HudPortraitSpec{ "Builtin/Enemy/ThunderSlime", "thunder_slime", 0.0f, -0.12f, 1.08f, 0.0f },
    HudPortraitSpec{ "Builtin/Enemy/WindSlime", "wind_slime", 0.0f, -0.12f, 1.08f, 0.0f },
};

constexpr const char* kHudPortraitDirectory = "Resources/sprite/ui/hud/morph_icons";

std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool IsModelAssetFile(const fs::path& path) {
    const std::string ext = ToLowerAscii(path.extension().string());
    return ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

bool IsGeneratedLodAsset(const fs::path& path) {
    return ToLowerAscii(path.stem().string()).find("_lod") != std::string::npos;
}

void ApplyThumbnailLightOverride(Object3d* object) {
    if (!object) {
        return;
    }

    if (auto* material = object->GetMaterialData()) {
        material->enableLighting = 0;
        material->selectedLighting = 0;
        material->emissive = (std::max)(material->emissive, 1.0f);
    }

    for (Object3d* child : object->GetChildren()) {
        ApplyThumbnailLightOverride(child);
    }
}

bool SavePng(const DirectX::Image& image, const fs::path& path) {
    const std::wstring widePath = path.wstring();
    return SUCCEEDED(DirectX::SaveToWICFile(
        image,
        DirectX::WIC_FLAGS_NONE,
        GUID_ContainerFormatPng,
        widePath.c_str(),
        &GUID_WICPixelFormat32bppRGBA));
}

bool CreateHurtPortrait(const DirectX::Image& source, DirectX::ScratchImage& hurtImage) {
    if (source.format != DXGI_FORMAT_R8G8B8A8_UNORM || source.width == 0 || source.height == 0) {
        return false;
    }

    const size_t squashedHeight = (std::max)(size_t{ 1 }, static_cast<size_t>(std::lround(source.height * 0.86)));
    DirectX::ScratchImage squashed;
    if (FAILED(DirectX::Resize(
        source,
        source.width,
        squashedHeight,
        DirectX::TEX_FILTER_CUBIC,
        squashed))) {
        return false;
    }

    if (FAILED(hurtImage.Initialize2D(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        source.width,
        source.height,
        1,
        1))) {
        return false;
    }

    const DirectX::Image* squashedImage = squashed.GetImage(0, 0, 0);
    const DirectX::Image* destination = hurtImage.GetImage(0, 0, 0);
    if (!squashedImage || !destination) {
        return false;
    }

    std::memset(destination->pixels, 0, destination->slicePitch);
    const size_t verticalOffset = (source.height - squashedHeight) / 2 + 4;
    const size_t copyHeight = (std::min)(squashedHeight, source.height - (std::min)(verticalOffset, source.height));
    const size_t copyBytes = (std::min)(source.width * 4, (std::min)(squashedImage->rowPitch, destination->rowPitch));
    for (size_t y = 0; y < copyHeight; ++y) {
        std::memcpy(
            destination->pixels + (verticalOffset + y) * destination->rowPitch,
            squashedImage->pixels + y * squashedImage->rowPitch,
            copyBytes);
    }

    // 被弾時は同じモデルの形を保ったまま、つぶれと赤みだけで反応を出します。
    for (size_t y = 0; y < destination->height; ++y) {
        uint8_t* row = destination->pixels + y * destination->rowPitch;
        for (size_t x = 0; x < destination->width; ++x) {
            uint8_t* pixel = row + x * 4;
            if (pixel[3] == 0) {
                continue;
            }
            pixel[0] = static_cast<uint8_t>((std::min)(255.0f, pixel[0] * 0.88f + 42.0f));
            pixel[1] = static_cast<uint8_t>(pixel[1] * 0.76f);
            pixel[2] = static_cast<uint8_t>(pixel[2] * 0.76f);
        }
    }
    return true;
}
}

void ProjectWindow::Initialize(DebugEditor* editor, DirectXCommon* dxCommon) {
    editor_ = editor;
    dxCommon_ = dxCommon;
    if (dxCommon_) {
        previewObject3dCommon_ = std::make_unique<Object3dCommon>();
        previewObject3dCommon_->Initialize(dxCommon_);
        // シーンの昼夜やフォグに左右されず、全PBR素材を同じ条件で比較する。
        pbrPreviewLightResource_ = dxCommon_->CreateBufferResource(sizeof(DirectionalLight));
        if (pbrPreviewLightResource_) {
            DirectionalLight light{};
            light.color = { 1.0f, 1.0f, 1.0f, 1.0f };
            light.direction = { -0.4f, -0.7f, 0.55f };
            light.intensity = 2.0f;
            light.ambientColor = { 0.24f, 0.26f, 0.29f };
            light.enableFog = 0;
            light.volumetricIntensity = 0.0f;
            light.volumetricSteps = 0;
            light.lightViewProj = Math::MakeIdentity4x4();
            void* mapped = nullptr;
            if (SUCCEEDED(pbrPreviewLightResource_->Map(0, nullptr, &mapped)) && mapped) {
                std::memcpy(mapped, &light, sizeof(light));
                pbrPreviewLightResource_->Unmap(0, nullptr);
            } else {
                pbrPreviewLightResource_.Reset();
            }
        }
        // ルートCBVの前回値を引き継がず、撮影中の局所ライトは常に0個にする。
        pbrPreviewPointLightsResource_ = dxCommon_->CreateBufferResource(sizeof(LightManager::PointLightConstData));
        pbrPreviewSpotLightsResource_ = dxCommon_->CreateBufferResource(sizeof(LightManager::SpotLightConstData));
        const auto clearLightBuffer = [](ID3D12Resource* resource, size_t bytes) {
            if (!resource) return;
            void* mapped = nullptr;
            if (SUCCEEDED(resource->Map(0, nullptr, &mapped)) && mapped) {
                std::memset(mapped, 0, bytes);
                resource->Unmap(0, nullptr);
            }
        };
        clearLightBuffer(pbrPreviewPointLightsResource_.Get(), sizeof(LightManager::PointLightConstData));
        clearLightBuffer(pbrPreviewSpotLightsResource_.Get(), sizeof(LightManager::SpotLightConstData));
    }
}

void ProjectWindow::QueueHudPortraitExports(bool forceAll) {
    const fs::path outputDirectory(kHudPortraitDirectory);
    for (const HudPortraitSpec& spec : kHudPortraitSpecs) {
        const fs::path normalPath = outputDirectory / (std::string(spec.fileStem) + ".png");
        const fs::path hurtPath = outputDirectory / (std::string(spec.fileStem) + "_hurt.png");
        if (!forceAll && fs::exists(normalPath) && fs::exists(hurtPath)) {
            continue;
        }

        if (presetThumbnailAlbum_.find(spec.presetName) == presetThumbnailAlbum_.end()) {
            CreatePresetThumbnailResource(spec.presetName);
        }

        ThumbnailData& data = presetThumbnailAlbum_[spec.presetName];
        data.isHudPortrait = true;
        data.exportRequested = true;
        data.exportFileStem = spec.fileStem;
        data.portraitYaw = spec.yaw;
        data.portraitPitch = spec.pitch;
        data.portraitScale = spec.scale;
        data.portraitOffsetY = spec.offsetY;
        data.isCaptured = false;
        data.previewObject.reset();
    }
}

// ==========================================================
// 画用紙（レンダーターゲット）の作成
// ==========================================================
void ProjectWindow::CreateThumbnailResource(const std::string& modelName) {
    if (!dxCommon_) return;
    auto device = dxCommon_->GetDevice();
    ThumbnailData& data = thumbnailAlbum_[modelName];

    // 1. 画用紙（テクスチャ）の作成
    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = kThumbnailSize;
    resDesc.Height = kThumbnailSize;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = resDesc.Format;
    clearValue.Color[0] = 0.15f; clearValue.Color[1] = 0.15f; clearValue.Color[2] = 0.15f; clearValue.Color[3] = 1.0f; // 背景色

    device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&data.resource));

    // 2. RTV（描き込むための準備）
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&data.rtvHeap));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = resDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(data.resource.Get(), &rtvDesc, data.rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // 3. SRV（ImGuiに表示するための画像ハンドル）
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = resDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    data.srvHandle = SRVManager::GetInstance()->CreateSRV(data.resource.Get(), srvDesc);

    data.isCaptured = false;
}

void ProjectWindow::CreatePresetThumbnailResource(const std::string& presetName) {
    if (!dxCommon_) return;
    if (presetThumbnailAlbum_.size() >= kPresetThumbnailCacheLimit) {
        const uint64_t completed = dxCommon_->GetFence()->GetCompletedValue();
        auto oldest = presetThumbnailAlbum_.end();
        uint64_t oldestFence = UINT64_MAX;
        for (auto it = presetThumbnailAlbum_.begin(); it != presetThumbnailAlbum_.end(); ++it) {
            if (it->second.isHudPortrait || it->second.exportRequested) continue;
            const uint64_t lastUse = presetThumbnailUseFence[it->first];
            if (lastUse <= completed && lastUse < oldestFence) { oldest = it; oldestFence = lastUse; }
        }
        if (oldest == presetThumbnailAlbum_.end()) return;
        ThumbnailData reused = std::move(oldest->second);
        presetThumbnailUseFence.erase(oldest->first);
        presetThumbnailAlbum_.erase(oldest);
        reused.previewObjects.clear(); reused.previewObject.reset(); reused.retiredPreviewObjects.clear();
        reused.retiredPreviewFence = 0; reused.isCaptured = false;
        presetThumbnailAlbum_[presetName] = std::move(reused);
        return;
    }
    auto device = dxCommon_->GetDevice();
    ThumbnailData& data = presetThumbnailAlbum_[presetName];

    // 画用紙（テクスチャ）の作成
    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = kThumbnailSize;
    resDesc.Height = kThumbnailSize;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = resDesc.Format;
    clearValue.Color[0] = 0.2f; clearValue.Color[1] = 0.2f; clearValue.Color[2] = 0.25f; clearValue.Color[3] = 1.0f; // プリセット用は少し色を変える

    device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&data.resource));

    // RTV
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&data.rtvHeap));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = resDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(data.resource.Get(), &rtvDesc, data.rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = resDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    data.srvHandle = SRVManager::GetInstance()->CreateSRV(data.resource.Get(), srvDesc);

    data.isCaptured = false;
}

void ProjectWindow::CreatePbrThumbnailResource(const std::string& name) {
    if (!dxCommon_ || name.empty()) return;

    // GPUがまだ参照する画像を破棄しないよう、完了済みの最古の枠だけを再利用する。
    if (pbrThumbnailAlbum_.size() >= kPbrThumbnailCacheLimit) {
        const uint64_t completed = dxCommon_->GetFence()->GetCompletedValue();
        auto oldest = pbrThumbnailAlbum_.end();
        for (auto it = pbrThumbnailAlbum_.begin(); it != pbrThumbnailAlbum_.end(); ++it) {
            if (it->second.lastUseFence <= completed &&
                (oldest == pbrThumbnailAlbum_.end() || it->second.lastUseFence < oldest->second.lastUseFence)) {
                oldest = it;
            }
        }
        if (oldest == pbrThumbnailAlbum_.end()) return;
        PbrThumbnailData reused = std::move(oldest->second);
        pbrThumbnailAlbum_.erase(oldest);
        reused.thumbnail.previewObject.reset();
        reused.thumbnail.isCaptured = false;
        pbrThumbnailAlbum_[name] = std::move(reused);
        return;
    }

    auto* device = dxCommon_->GetDevice();
    auto& data = pbrThumbnailAlbum_[name].thumbnail;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kThumbnailSize;
    desc.Height = kThumbnailSize;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = desc.Format;
    clear.Color[0] = 0.18f;
    clear.Color[1] = 0.19f;
    clear.Color[2] = 0.22f;
    clear.Color[3] = 1.0f;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&data.resource)))) return;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeap{};
    rtvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeap.NumDescriptors = 1;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeap, IID_PPV_ARGS(&data.rtvHeap)))) return;
    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = desc.Format;
    rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(data.resource.Get(), &rtv, data.rtvHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    data.srvHandle = SRVManager::GetInstance()->CreateSRV(data.resource.Get(), srv);
}

uint64_t ProjectWindow::GetPbrThumbnailGpuPtr(const std::string& name,
    const std::string& albedoPath, const std::string& normalPath, const std::string& ormPath) {
    if (name.empty() || albedoPath.empty() || normalPath.empty() || ormPath.empty()) return 0;
    if (pbrThumbnailAlbum_.find(name) == pbrThumbnailAlbum_.end()) CreatePbrThumbnailResource(name);
    auto it = pbrThumbnailAlbum_.find(name);
    if (it == pbrThumbnailAlbum_.end() || !it->second.thumbnail.resource || !it->second.thumbnail.srvHandle) return 0;
    PbrThumbnailData& data = it->second;
    if (data.albedoPath != albedoPath || data.normalPath != normalPath || data.ormPath != ormPath) {
        data.albedoPath = albedoPath;
        data.normalPath = normalPath;
        data.ormPath = ormPath;
        data.thumbnail.isCaptured = false;
    }
    data.lastUseFence = dxCommon_->GetFenceValue() + 1;
    return data.thumbnail.isCaptured
        ? SRVManager::GetInstance()->GetGPUDescriptorHandle(data.thumbnail.srvHandle).ptr : 0;
}

void ProjectWindow::InvalidatePbrThumbnails() {
    for (auto& [name, data] : pbrThumbnailAlbum_) data.thumbnail.isCaptured = false;
}

uint64_t ProjectWindow::GetPresetThumbnailGpuPtr(const std::string& presetName) {
    if (presetName.empty()) {
        return 0;
    }

    if (presetThumbnailAlbum_.find(presetName) == presetThumbnailAlbum_.end()) {
        CreatePresetThumbnailResource(presetName);
    }

    auto it = presetThumbnailAlbum_.find(presetName);
    if (it == presetThumbnailAlbum_.end() || it->second.srvHandle == 0) {
        return 0;
    }

    presetThumbnailUseFence[presetName] = dxCommon_->GetFenceValue() + 1;
    if (presetName.starts_with("@effect:") && !it->second.isCaptured) return 0;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(it->second.srvHandle);
    return static_cast<uint64_t>(gpuHandle.ptr);
}

// ==========================================================
// 撮影処理（Game::Drawの最初で呼ばれる）
// ==========================================================
void ProjectWindow::InvalidatePresetThumbnail(const std::string& presetName) {
    const auto it = presetThumbnailAlbum_.find(presetName);
    if (it != presetThumbnailAlbum_.end()) it->second.isCaptured = false;
}

void ProjectWindow::CapturePendingThumbnails() {
    if (!dxCommon_) return;
    if (!hudPortraitAutoScanDone_) {
        hudPortraitAutoScanDone_ = true;
        QueueHudPortraitExports(false);
    }
    auto device = dxCommon_->GetDevice();
    auto commandList = dxCommon_->GetCommandList();

    // =======================================================
    //  シーンの進行状況に依存しない、安全な専用 Object3dCommon を使う！
    // =======================================================
    Object3dCommon* objCommon = previewObject3dCommon_.get();
    if (!objCommon) return;

    // --- Zバッファの生成 ---
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_studioDepth;
    static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> s_studioDsvHeap;
    static uint32_t s_studioDepthSrv = 0;
    if (!s_studioDepth) {
        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = kThumbnailSize;
        depthDesc.Height = kThumbnailSize;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;;
        clearValue.DepthStencil.Depth = 1.0f;

        device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&s_studioDepth));

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&s_studioDsvHeap));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(s_studioDepth.Get(), &dsvDesc, s_studioDsvHeap->GetCPUDescriptorHandleForHeapStart());
        D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
        depthSrv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depthSrv.Texture2D.MipLevels = 1;
        s_studioDepthSrv = SRVManager::GetInstance()->CreateSRV(s_studioDepth.Get(), depthSrv);
    }

    D3D12_VIEWPORT originalViewport{};
    originalViewport.Width = (float)WinApp::kClientWidth;
    originalViewport.Height = (float)WinApp::kClientHeight;
    originalViewport.TopLeftX = 0;
    originalViewport.TopLeftY = 0;
    originalViewport.MinDepth = 0.0f;
    originalViewport.MaxDepth = 1.0f;

    D3D12_RECT originalScissor{};
    originalScissor.left = 0;
    originalScissor.right = WinApp::kClientWidth;
    originalScissor.top = 0;
    originalScissor.bottom = WinApp::kClientHeight;

    if (!isStudioCameraInitialized_) {
        studioCamera_.Initialize();
        studioCamera_.SetInputEnabled(false);
        isStudioCameraInitialized_ = true;
    }
    studioCamera_.Update(); // 行列を計算

    // メインカメラから、このスタジオ用カメラにすり替える！
    Camera* previousCameraOverride = CameraManager::GetInstance()->GetActiveCameraOverride();
    CameraManager::GetInstance()->SetActiveCamera(&studioCamera_);
    // =================================================================

    for (auto& pair : thumbnailAlbum_) {
        std::string modelName = pair.first;
        ThumbnailData& data = pair.second;
        if (data.isCaptured) continue;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = data.resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = data.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = s_studioDsvHeap->GetCPUDescriptorHandleForHeapStart();
        commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

        const float clearColor[] = { 0.15f, 0.15f, 0.15f, 1.0f };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        D3D12_VIEWPORT viewport{};
        viewport.Width = (float)kThumbnailSize;
        viewport.Height = (float)kThumbnailSize;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        commandList->RSSetViewports(1, &viewport);

        D3D12_RECT scissorRect{};
        scissorRect.left = 0;
        scissorRect.right = kThumbnailSize;
        scissorRect.top = 0;
        scissorRect.bottom = kThumbnailSize;
        commandList->RSSetScissorRects(1, &scissorRect);

        objCommon->SetGraphicsCommand();
        objCommon->SetPipelineState(BlendMode::kNormal);

        if (!data.previewObject) {
            data.previewObject = std::make_shared<Object3d>();
            data.previewObject->Initialize(objCommon);
            data.previewObject->SetIsUIPreview(true);

            // モデルをロードしてポインタを取得
            Model* model = ModelManager::GetInstance()->LoadModel(modelName);
            data.previewObject->SetModel(modelName);

            // 初期フィット計算
            float autoScale = 1.0f;
            Vector3 autoTranslate = { 0.0f, 0.0f, 0.0f };

            if (model) {
                Vector3 modelSize = model->GetSize();
                Vector3 modelCenter = model->GetCenter();

                // 最も長い辺を見つける
                float maxDim = (std::max)({ modelSize.x, modelSize.y, modelSize.z });
                if (maxDim > 0.001f) {
                    // モデルのサイズと中心点に基づき、フィットするスケールとオフセットを計算
                    autoScale = 8.5f / maxDim;
                    autoTranslate.x = -modelCenter.x * autoScale;
                    autoTranslate.y = -modelCenter.y * autoScale + 1.1f;
                    autoTranslate.z = -modelCenter.z * autoScale;
                }
            }

            // 計算したスケールと位置をセット（これ以降は保持される）
            data.previewObject->SetScale({ autoScale, autoScale, autoScale });
            data.previewObject->SetTranslate(autoTranslate);

            // スタジオカメラの角度に合わせ、モデルを上向きにして正面を向くように調整
            data.previewObject->SetEulerX(-0.2f);
        }

        auto& targetObj = data.previewObject;
        ApplyThumbnailLightOverride(targetObj.get());

        // くるくる回転
        targetObj->SetEulerY(targetObj->GetRotation().y + 0.02f);

        // 行列の更新と描画
        targetObj->UpdateLocalMatrix();
        targetObj->UpdateWorldMatrix();

        targetObj->Draw(nullptr, nullptr);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(1, &barrier);

        data.isCaptured = true;
    }

    // --- プリセットの撮影ループ ---
    int capturedPresetCount = 0;
    for (auto& pair : presetThumbnailAlbum_) {
        std::string presetName = pair.first;
        ThumbnailData& data = pair.second;
        if (data.retiredPreviewFence != 0 && dxCommon_->GetFence()->GetCompletedValue() >= data.retiredPreviewFence) {
            data.retiredPreviewObjects.clear();
            data.retiredPreviewFence = 0;
        }
        if (data.isCaptured) continue;
        // 表示した項目だけ生成し、初回撮影によるフレーム停止を抑えます。
        if (!data.isHudPortrait && capturedPresetCount >= 4) continue;
        ++capturedPresetCount;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = data.resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = data.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = s_studioDsvHeap->GetCPUDescriptorHandleForHeapStart();
        commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

        const float clearColor[] = {
            data.isHudPortrait ? 0.0f : 0.2f,
            data.isHudPortrait ? 0.0f : 0.2f,
            data.isHudPortrait ? 0.0f : 0.25f,
            data.isHudPortrait ? 0.0f : 1.0f
        };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        D3D12_VIEWPORT viewport = { 0, 0, (float)kThumbnailSize, (float)kThumbnailSize, 0.0f, 1.0f };
        commandList->RSSetViewports(1, &viewport);
        D3D12_RECT scissor = { 0, 0, kThumbnailSize, kThumbnailSize };
        commandList->RSSetScissorRects(1, &scissor);

        objCommon->SetGraphicsCommand();
        objCommon->SetPipelineState(BlendMode::kNormal);

        if (presetName.starts_with("@effect:")) {
            // GPUで使用中の旧Objectを破棄せず、既存のFence付き退避へ渡します。
            if (data.previewObject) {
                data.retiredPreviewObjects.push_back(std::move(data.previewObject));
                data.retiredPreviewFence = dxCommon_->GetFenceValue() + 1;
            }
            auto effect = std::make_shared<EffectObject3d>();
            effect->Initialize(objCommon);
            data.previewObject = effect;
            try {
                if (effect->LoadFromJson(presetName.substr(8))) {
                    effect->SetIsUIPreview(true);
                    effect->SetEditorInternal(true);
                    effect->editHasCollision_ = false;
                    effect->SetCollisionAttribute(0); effect->SetCollisionMask(0);
                    effect->SetTargetObject(nullptr); effect->SetAutoLoop(false);
                    // 背景歪みは撮影先のSceneカラーを持たないため、素材の色と形を撮ります。
                    effect->SetEnableDistortion(false);
                    const float lifetime = (std::max)(0.01f, effect->GetLifetime());
                    effect->Play(lifetime);
                    effect->Update(lifetime * 0.4f);
                    // 手続き生成メッシュはModelNameが空でもRendererに実体があります。
                    if (auto* model = effect->GetMeshRenderer() ? effect->GetMeshRenderer()->GetModel() : nullptr) {
                        auto scale = effect->GetScale();
                        const auto size = model->GetSize();
                        const float extent = (std::max)({std::abs(size.x * scale.x), std::abs(size.y * scale.y), std::abs(size.z * scale.z), 0.001f});
                        scale = scale * (6.0f / extent);
                        effect->SetScale(scale);
                        effect->SetRotation({-0.35f, 0.3f, 0});
                        const auto center = Math::Transform(model->GetCenter(), Math::MakeAffineMatrix(scale, effect->GetRotation(), {}));
                        effect->SetTranslate({-center.x, 1.1f - center.y, -center.z});
                    }
                    effect->UpdateLocalMatrix(); effect->UpdateWorldMatrix();
                    effect->Draw(nullptr, nullptr);
                }
            } catch (const std::exception& error) {
                DebugConsole::GetInstance()->AddLog(std::string("Effect thumbnail: ") + error.what());
            }
        } else if (data.isHudPortrait) {
            if (!data.previewObject) {
                data.previewObject = std::make_shared<Object3d>();
                data.previewObject->Initialize(objCommon);
                data.previewObject->SetIsUIPreview(true);
            }

            // プリセットの設定を適用！
            PresetManager::GetInstance()->ApplyPresetToObject(presetName, data.previewObject.get());

            // プレビュー用に姿勢を微調整（くるくる回す）
            static float rotationY = 0.0f;
            rotationY += 0.02f;
            data.previewObject->SetEulerY(data.isHudPortrait ? data.portraitYaw : rotationY);
            data.previewObject->SetEulerX(data.isHudPortrait ? data.portraitPitch : -0.2f);

            // モデルの自動フィット計算 (モデルが設定されている場合のみ)
            if (!data.previewObject->GetModelName().empty()) {
                Model* model = ModelManager::GetInstance()->LoadModel(data.previewObject->GetModelName());
                if (model) {
                    Vector3 modelSize = model->GetSize();
                    Vector3 modelCenter = model->GetCenter();
                    float maxDim = (std::max)({ modelSize.x, modelSize.y, modelSize.z });
                    if (maxDim > 0.001f) {
                        float autoScale = 8.0f / maxDim;
                        if (data.isHudPortrait) {
                            autoScale *= data.portraitScale;
                        }
                        data.previewObject->SetScale({ autoScale, autoScale, autoScale });
                        data.previewObject->SetTranslateX(-modelCenter.x * autoScale);
                        data.previewObject->SetTranslateY(-modelCenter.y * autoScale + 1.1f + (data.isHudPortrait ? data.portraitOffsetY : 0.0f));
                        data.previewObject->SetTranslateZ(-modelCenter.z * autoScale);
                    }
                }
            }

            ApplyThumbnailLightOverride(data.previewObject.get());

            data.previewObject->UpdateLocalMatrix();
            data.previewObject->UpdateWorldMatrix();
            data.previewObject->Draw(nullptr, nullptr);

        } else {
            // Sceneの実体やギミックのUpdateを使わず、描画専用Objectで階層を再現します。
            for (auto& node : data.previewObjects) data.retiredPreviewObjects.push_back(std::move(node));
            data.previewObjects.clear();
            if (data.previewObject) data.retiredPreviewObjects.push_back(std::move(data.previewObject));
            if (!data.retiredPreviewObjects.empty()) data.retiredPreviewFence = dxCommon_->GetFenceValue() + 1;
            data.previewObject = std::make_shared<Object3d>();
            data.previewObject->Initialize(objCommon);
            data.previewObject->SetIsUIPreview(true);
            const auto& presets = PresetManager::GetInstance()->GetPresets();
            const auto presetIt = presets.find(presetName);
            if (presetIt != presets.end()) {
                std::function<void(const nlohmann::json&, Object3d*, int)> build = [&](const nlohmann::json& source, Object3d* parent, int depth) {
                    if (!source.is_object() || depth > 32) return;
                    auto node = std::make_shared<Object3d>();
                    node->Initialize(objCommon);
                    nlohmann::json visual = source;
                    visual.erase("children"); visual.erase("guid"); visual.erase("prefabInstance");
                    visual["type"] = "Model";
                    // 一覧では配置方向によらず、正面の形状を確認できる向きに揃えます。
                    if (depth == 0) { visual["rotate"] = {0.0f, 0.0f, 0.0f}; visual["quaternion"] = {0.0f, 0.0f, 0.0f, 1.0f}; }
                    // Recorder・衝突地形・Componentを読み込まず、描画設定だけを復元します。
                    visual.erase("recorder"); visual.erase("terrainCollisionPath"); visual.erase("components");
                    node->ImportFromJson(visual);
                    const bool invisible = source.value("type", "") == "InvisibleBox" || source.value("presetUsage", "") == "当たり判定";
                    if (invisible) node->SetModel("");
                    node->SetIsUIPreview(true);
                    node->SetParent(parent);
                    node->UpdateWorldMatrix();
                    data.previewObjects.push_back(node);
                    if (source.contains("children") && source["children"].is_array())
                        for (const auto& child : source["children"]) build(child, node.get(), depth + 1);
                };
                build(presetIt->second, data.previewObject.get(), 0);
            }
            Vector3 low = {FLT_MAX,FLT_MAX,FLT_MAX}, high = {-FLT_MAX,-FLT_MAX,-FLT_MAX};
            bool hasBounds = false;
            for (const auto& node : data.previewObjects) {
                if (!node->GetModel()) continue;
                const AABB bounds = node->GetModelWorldAABB();
                low.x = (std::min)(low.x,bounds.min.x); low.y = (std::min)(low.y,bounds.min.y); low.z = (std::min)(low.z,bounds.min.z);
                high.x = (std::max)(high.x,bounds.max.x); high.y = (std::max)(high.y,bounds.max.y); high.z = (std::max)(high.z,bounds.max.z);
                hasBounds = true;
            }
            if (hasBounds) {
                const Vector3 center = {(low.x+high.x)*.5f,(low.y+high.y)*.5f,(low.z+high.z)*.5f};
                const Vector3 extent = {high.x-low.x,high.y-low.y,high.z-low.z};
                const float maximum = (std::max)({extent.x,extent.y,extent.z,.001f});
                const float fit = 6.0f / maximum;
                Transform framingValue = *data.previewObject->GetTransform();
                Transform* framing = &framingValue;
                framing->scale = {fit,fit,fit};
                // 正面形状と床の上面が読める、固定された斜め上の構図を使います。
                framing->rotate = {-.28f,.32f,0};
                if (extent.y < (std::max)(extent.x,extent.z)*.12f) framing->rotate.x = -.55f;
                framing->isQuaternionMaster = false;
                const Matrix4x4 orientation = Math::MakeAffineMatrix(framing->scale,framing->rotate,{});
                const Vector3 offset = Math::Transform(center,orientation);
                framing->translate = {-offset.x,1.1f-offset.y,-offset.z};
                data.previewObject->SetLocalTransform(framingValue);
            }
            ApplyThumbnailLightOverride(data.previewObject.get());
            data.previewObject->UpdateWorldMatrix();
            for (const auto& node : data.previewObjects) {
                const int material = node->GetMaterialType();
                if (material < 8 || (material > 22 && material != 26))
                    node->Draw(nullptr,nullptr);
            }
            // 不透明物体の深度を特殊材質へ渡すため、DSVを外して読取状態へ遷移します。
            commandList->OMSetRenderTargets(1,&rtvHandle,false,nullptr);
            D3D12_RESOURCE_BARRIER depthRead{};
            depthRead.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            depthRead.Transition.pResource = s_studioDepth.Get();
            depthRead.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            depthRead.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            commandList->ResourceBarrier(1,&depthRead);
            const uint32_t neutralColor = TextureManager::GetInstance()->Load("Resources/sprite/common/white.dds");
            for (const auto& node : data.previewObjects) {
                const int material = node->GetMaterialType();
                if ((material >= 8 && material <= 22) || material == 26)
                    node->DrawSpecialMaterialForCamera(&studioCamera_,s_studioDepthSrv,neutralColor);
            }
            std::swap(depthRead.Transition.StateBefore,depthRead.Transition.StateAfter);
            commandList->ResourceBarrier(1,&depthRead);
        }

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(1, &barrier);

        data.isCaptured = true;
    }


    // PBR一覧は撮影用の球だけを描く。1フレーム2枚までに抑えてUI停止を防ぐ。
    int capturedPbrCount = 0;
    for (auto& [name, entry] : pbrThumbnailAlbum_) {
        ThumbnailData& data = entry.thumbnail;
        if (data.isCaptured || !data.resource || !data.rtvHeap ||
            entry.albedoPath.empty() || entry.normalPath.empty() || entry.ormPath.empty()) continue;
        if (capturedPbrCount++ >= 2) break;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = data.resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(1, &barrier);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = data.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = s_studioDsvHeap->GetCPUDescriptorHandleForHeapStart();
        commandList->OMSetRenderTargets(1, &rtv, false, &dsv);
        const float clearColor[] = { 0.18f, 0.19f, 0.22f, 1.0f };
        commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
        commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        const D3D12_VIEWPORT viewport = { 0, 0, static_cast<float>(kThumbnailSize),
            static_cast<float>(kThumbnailSize), 0.0f, 1.0f };
        const D3D12_RECT scissor = { 0, 0, kThumbnailSize, kThumbnailSize };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);

        if (!data.previewObject) {
            data.previewObject = std::make_shared<Object3d>();
            data.previewObject->Initialize(objCommon);
            data.previewObject->SetIsUIPreview(true);
            data.previewObject->SetModel("Primitives/sphere");
            if (Model* sphere = ModelManager::GetInstance()->LoadModel("Primitives/sphere")) {
                const Vector3 size = sphere->GetSize();
                const Vector3 center = sphere->GetCenter();
                const float extent = (std::max)({ size.x, size.y, size.z, 0.001f });
                const float scale = 8.5f / extent;
                data.previewObject->SetScale({ scale, scale, scale });
                data.previewObject->SetTranslate({
                    -center.x * scale, 1.1f - center.y * scale, -center.z * scale });
            }
            data.previewObject->SetEulerX(-0.2f);
            data.previewObject->SetMaterialType(0);
            data.previewObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            data.previewObject->SetEmissive(1.0f);
            data.previewObject->SetRoughness(1.0f);
            data.previewObject->SetMetallic(1.0f);
            data.previewObject->SetEnableLighting(true);
            data.previewObject->SetSelectedLighting(2);
            data.previewObject->SetEnableNormalMap(true);
            data.previewObject->SetAutoTextureTiling(false);
            data.previewObject->SetTextureTiling({ 1.0f, 1.0f });
            if (auto* renderer = data.previewObject->GetMeshRenderer()) {
                renderer->SetPreviewDirectionalLightResource(pbrPreviewLightResource_.Get());
            }
        }
        data.previewObject->SetTexture(entry.albedoPath);
        data.previewObject->SetNormalMap(entry.normalPath);
        data.previewObject->SetOrmMap(entry.ormPath);
        data.previewObject->UpdateLocalMatrix();
        data.previewObject->UpdateWorldMatrix();
        data.previewObject->Draw(pbrPreviewPointLightsResource_.Get(), pbrPreviewSpotLightsResource_.Get());

        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commandList->ResourceBarrier(1, &barrier);
        data.isCaptured = true;
        entry.lastUseFence = dxCommon_->GetFenceValue() + 1;
    }

    CameraManager::GetInstance()->SetActiveCamera(previousCameraOverride);

    commandList->RSSetViewports(1, &originalViewport);
    commandList->RSSetScissorRects(1, &originalScissor);
}

void ProjectWindow::ExportCapturedHudPortraits() {
    if (!dxCommon_) {
        return;
    }

    const fs::path outputDirectory(kHudPortraitDirectory);
    std::error_code directoryError;
    fs::create_directories(outputDirectory, directoryError);
    if (directoryError) {
        DebugConsole::GetInstance()->AddLog("HUD icon export failed: output directory could not be created.");
        return;
    }

    for (auto& [presetName, data] : presetThumbnailAlbum_) {
        if (!data.exportRequested || !data.isCaptured || !data.resource) {
            continue;
        }

        data.exportRequested = false;
        DirectX::ScratchImage captured;
        HRESULT hr = DirectX::CaptureTexture(
            dxCommon_->GetCommandQueue(),
            data.resource.Get(),
            false,
            captured,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (FAILED(hr)) {
            DebugConsole::GetInstance()->AddLog("HUD icon export failed while reading GPU texture: " + presetName);
            continue;
        }

        const DirectX::Image* source = captured.GetImage(0, 0, 0);
        if (!source) {
            DebugConsole::GetInstance()->AddLog("HUD icon export failed: captured image is empty: " + presetName);
            continue;
        }

        DirectX::ScratchImage converted;
        hr = DirectX::Convert(
            *source,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DirectX::TEX_FILTER_DEFAULT,
            DirectX::TEX_THRESHOLD_DEFAULT,
            converted);
        const DirectX::Image* normalImage = converted.GetImage(0, 0, 0);
        if (FAILED(hr) || !normalImage) {
            DebugConsole::GetInstance()->AddLog("HUD icon export failed while converting image: " + presetName);
            continue;
        }

        const fs::path normalPath = outputDirectory / (data.exportFileStem + ".png");
        const fs::path hurtPath = outputDirectory / (data.exportFileStem + "_hurt.png");
        DirectX::ScratchImage hurtImage;
        const bool savedNormal = SavePng(*normalImage, normalPath);
        const bool madeHurt = CreateHurtPortrait(*normalImage, hurtImage);
        const DirectX::Image* hurt = madeHurt ? hurtImage.GetImage(0, 0, 0) : nullptr;
        const bool savedHurt = hurt && SavePng(*hurt, hurtPath);

        if (savedNormal && savedHurt) {
            DebugConsole::GetInstance()->AddLog("HUD icons exported from preset: " + presetName);
        } else {
            DebugConsole::GetInstance()->AddLog("HUD icon PNG save failed: " + presetName);
        }
    }
}

void ProjectWindow::Draw() {
#ifdef USE_IMGUI
    AssetDatabase* assetDatabase = AssetDatabase::GetInstance();

    // Preset、Model、Prefab、VFXを一つのProjectウィンドウのタブで切り替える。
    ImGui::Begin("Project (Assets)");

    if (!ImGui::BeginTabBar("ProjectAssetTabs")) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabItem("プリセット")) {
        PresetEditor::GetInstance()->DrawLibrary(true);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("モデル")) {
        ImGui::SeparatorText("Asset Database");
    // 初回索引中は進捗、完了後はAsset数・再解析数・エラー数を示す。
    const AssetDatabaseRefreshResult& databaseStatus = assetDatabase->GetLastRefreshResult();
    if (assetDatabase->IsInitialIndexBuildInProgress()) {
        if (assetDatabase->IsInitialDirectoryScanInProgress()) {
            ImGui::Text(
                "Asset一覧を確認中: %zu件検出",
                assetDatabase->GetInitialDiscoveredAssetCount());
            const float scanAnimation = static_cast<float>(std::fmod(ImGui::GetTime() * 0.5, 1.0));
            ImGui::ProgressBar(scanAnimation, ImVec2(-1.0f, 0.0f));
        }
        else {
            const std::size_t progress = assetDatabase->GetInitialIndexProgress();
            const std::size_t total = assetDatabase->GetInitialIndexTotal();
            const float fraction = total > 0
                ? static_cast<float>(progress) / static_cast<float>(total)
                : 1.0f;
            ImGui::Text(
                "差分索引を確認中: %zu / %zu  (Cache %zu / 再解析 %zu)",
                progress,
                total,
                assetDatabase->GetInitialReusedAssetCount(),
                assetDatabase->GetInitialReparsedAssetCount());
            ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f));
        }
    }
    else {
        ImGui::Text(
            "%zu Assets / Cache %zu / 再解析 %zu / Meta作成 %zu / Error %zu",
            databaseStatus.assetCount,
            databaseStatus.reusedAssetCount,
            databaseStatus.reparsedAssetCount,
            databaseStatus.createdMetaCount,
            databaseStatus.errorCount);
    }
    // Asset索引とDDS変換が重なる間は、手動更新と追加の変換要求を無効にする。
    const DDSCacheBuildState ddsState = assetDatabase->GetDDSCacheBuildState();
    const bool ddsProcessBusy = ddsState == DDSCacheBuildState::Queued ||
        ddsState == DDSCacheBuildState::Running;
    ImGui::BeginDisabled(assetDatabase->IsInitialIndexBuildInProgress() || ddsProcessBusy);
    if (ImGui::Button(ICON_FA_SYNC " Asset Databaseを更新")) {
        assetDatabase->RequestRefresh(true);
    }
    ImGui::EndDisabled();

    if (ddsState == DDSCacheBuildState::Running || ddsState == DDSCacheBuildState::Queued) {
        ImGui::TextUnformatted(assetDatabase->GetDDSCacheBuildMessage().c_str());
        const float animation = static_cast<float>(std::fmod(ImGui::GetTime() * 0.35, 1.0));
        ImGui::ProgressBar(animation, ImVec2(-1.0f, 0.0f), "DDS Cache");
    }
    else if (ddsState == DDSCacheBuildState::Succeeded) {
        ImGui::TextColored(
            ImVec4(0.45f, 0.9f, 0.55f, 1.0f),
            "%s",
            assetDatabase->GetDDSCacheBuildMessage().c_str());
    }
    else if (ddsState == DDSCacheBuildState::Failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
            "%s",
            assetDatabase->GetDDSCacheBuildMessage().c_str());
    }

    const bool ddsBusy = ddsProcessBusy ||
        assetDatabase->IsInitialIndexBuildInProgress();
    ImGui::BeginDisabled(ddsBusy);
    if (ImGui::Button(ICON_FA_IMAGE " 未変換DDSを生成")) {
        assetDatabase->RequestDDSCacheBuild();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("未変換または元画像より古いDDSだけを非同期で生成します。");
    }
    if (!assetDatabase->GetIssues().empty() && ImGui::TreeNode("Asset Database Issues")) {
        for (const AssetDatabaseIssue& issue : assetDatabase->GetIssues()) {
            const ImVec4 color = issue.severity == AssetDatabaseIssueSeverity::Error
                ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                : ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
            ImGui::TextColored(color, "%s", issue.path.c_str());
            ImGui::TextWrapped("  %s", issue.message.c_str());
        }
        ImGui::TreePop();
    }

    // Modelの実ファイルとディレクトリを、サムネイル付きの格子へ並べる。
    if (ImGui::CollapsingHeader("Models (Source)", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string baseDirectory = "Resources/3DModel/";

        if (currentModelDirectory_.empty() || currentModelDirectory_.find(baseDirectory) != 0) {
            currentModelDirectory_ = baseDirectory;
        }

        if (currentModelDirectory_ != baseDirectory) {
            if (ImGui::Button(ICON_FA_ARROW_UP " Back")) {
                fs::path p(currentModelDirectory_);
                currentModelDirectory_ = p.parent_path().parent_path().generic_string() + "/";
                if (currentModelDirectory_.length() < baseDirectory.length() ||
                    currentModelDirectory_.find(baseDirectory) == std::string::npos) {
                    currentModelDirectory_ = baseDirectory;
                }
            }
            ImGui::SameLine();
        }
        ImGui::TextDisabled("%s", currentModelDirectory_.c_str());
        ImGui::Separator();

        // ディレクトリ内のAssetだけを表示し、下位フォルダーは個別の入口として描く。
        const std::vector<std::string> modelDirectories = assetDatabase->GetSubdirectories(currentModelDirectory_);
        const std::vector<const EditorAssetRecord*> directoryAssets =
            assetDatabase->GetAssetsInDirectory(currentModelDirectory_, false);
        ImGui::TextDisabled("Drag & Drop to Scene to Place");
        ImGui::Separator();

        const float thumbnailSize = 64.0f;
        const float padding = 16.0f;
        const float cellSize = thumbnailSize + padding;
        const float panelWidth = ImGui::GetContentRegionAvail().x;
        const int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));

        if (ImGui::BeginTable("ModelAssetTable", columnCount)) {
            for (const std::string& directoryPath : modelDirectories) {
                const fs::path path(directoryPath);
                const std::string folderName = path.filename().string();
                std::string payloadName = fs::relative(path, baseDirectory).generic_string();
                std::replace(payloadName.begin(), payloadName.end(), '\\', '/');
                const std::vector<const EditorAssetRecord*> childAssets =
                    assetDatabase->GetAssetsInDirectory(directoryPath, false);
                const auto modelAssetIterator = std::find_if(
                    childAssets.begin(),
                    childAssets.end(),
                    [](const EditorAssetRecord* asset) {
                        return asset && asset->type == EditorAssetType::Model;
                    });
                // 直下にModelを含むフォルダーは、フォルダー自体を配置可能なModelとして扱う。
                const bool isModelFolder = modelAssetIterator != childAssets.end();

                ImGui::TableNextColumn();
                if (isModelFolder) {
                    if (thumbnailAlbum_.find(payloadName) == thumbnailAlbum_.end()) {
                        CreateThumbnailResource(payloadName);
                    }
                    const uint32_t srvHandle = thumbnailAlbum_[payloadName].srvHandle;
                    const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
                        SRVManager::GetInstance()->GetGPUDescriptorHandle(srvHandle);

                    ImGui::PushID(directoryPath.c_str());
                    ImGui::BeginGroup();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::ImageButton(
                        payloadName.c_str(),
                        static_cast<ImTextureID>(gpuHandle.ptr),
                        ImVec2(thumbnailSize, thumbnailSize));
                    ImGui::PopStyleColor();

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        const EditorAssetDragPayload dragPayload = MakeEditorAssetDragPayload(
                            (*modelAssetIterator)->guid,
                            payloadName);
                        ImGui::SetDragDropPayload("MODEL_ASSET", &dragPayload, sizeof(dragPayload));
                        ImGui::Image(
                            static_cast<ImTextureID>(gpuHandle.ptr),
                            ImVec2(32.0f, 32.0f));
                        ImGui::SameLine();
                        ImGui::Text("Place: %s", folderName.c_str());
                        if (!(*modelAssetIterator)->guid.empty()) {
                            ImGui::TextDisabled("GUID: %s", (*modelAssetIterator)->guid.c_str());
                        }
                        ImGui::EndDragDropSource();
                    }
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                    ImGui::TextWrapped("%s", folderName.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                    ImGui::PopID();
                }
                else {
                    ImGui::PushID(directoryPath.c_str());
                    ImGui::BeginGroup();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));
                    ImGui::SetWindowFontScale(3.0f);
                    if (ImGui::Button(ICON_FA_FOLDER, ImVec2(thumbnailSize, thumbnailSize))) {
                        currentModelDirectory_ = directoryPath + "/";
                    }
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::PopStyleColor(4);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                    ImGui::TextWrapped("%s", folderName.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                    ImGui::PopID();
                }
            }

            // 個別ModelはGUIDをDrag Payloadへ載せ、Sceneへ配置するときの参照を保つ。
            for (const EditorAssetRecord* asset : directoryAssets) {
                if (!asset || asset->type != EditorAssetType::Model) {
                    continue;
                }
                const fs::path sourcePath(asset->sourcePath);
                const std::string fileName = sourcePath.filename().string();
                std::string payloadName = fs::relative(sourcePath, baseDirectory).generic_string();
                std::replace(payloadName.begin(), payloadName.end(), '\\', '/');
                const bool isGeneratedLod = IsGeneratedLodAsset(sourcePath);

                if (thumbnailAlbum_.find(payloadName) == thumbnailAlbum_.end()) {
                    CreateThumbnailResource(payloadName);
                }
                const uint32_t srvHandle = thumbnailAlbum_[payloadName].srvHandle;
                const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
                    SRVManager::GetInstance()->GetGPUDescriptorHandle(srvHandle);

                ImGui::TableNextColumn();
                ImGui::PushID(asset->guid.empty() ? asset->sourcePath.c_str() : asset->guid.c_str());
                ImGui::BeginGroup();
                if (isGeneratedLod) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.28f, 0.05f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.43f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.52f, 0.10f, 1.0f));
                }
                else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
                }
                ImGui::ImageButton(
                    payloadName.c_str(),
                    static_cast<ImTextureID>(gpuHandle.ptr),
                    ImVec2(thumbnailSize, thumbnailSize));
                ImGui::PopStyleColor(3);

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    const EditorAssetDragPayload dragPayload = MakeEditorAssetDragPayload(asset->guid, payloadName);
                    ImGui::SetDragDropPayload("MODEL_ASSET", &dragPayload, sizeof(dragPayload));
                    ImGui::Image(
                        static_cast<ImTextureID>(gpuHandle.ptr),
                        ImVec2(32.0f, 32.0f));
                    ImGui::SameLine();
                    ImGui::Text("Place: %s", fileName.c_str());
                    if (!asset->guid.empty()) {
                        ImGui::TextDisabled("GUID: %s", asset->guid.c_str());
                    }
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered() && !asset->guid.empty()) {
                    ImGui::SetTooltip(
                        "%s\nGUID: %s\nImporter: %s",
                        asset->sourcePath.c_str(),
                        asset->guid.c_str(),
                        asset->importer.c_str());
                }
                if (isGeneratedLod) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "LOD");
                }
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                ImGui::TextWrapped("%s", fileName.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::EndTabItem();
    }
    // Presetの作成・上書き・名前変更など、内容を変更する操作は管理タブに置く。
    if (ImGui::BeginTabItem("管理")) {
    if (ImGui::CollapsingHeader("Presets (Configured)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), ICON_FA_PLUS_SQUARE " Create New Preset");
        ImGui::TextDisabled(" (Use '/' in name for folders, e.g. 'Enemy/Slime')");

        if (editor_->GetSelectedObject()) {
            static char presetNameBuf[64] = "NewPreset";
            ImGui::PushItemWidth(200);
            ImGui::InputText("##PresetName", presetNameBuf, 64);
            ImGui::PopItemWidth();

            ImGui::SameLine();

            if (ImGui::Button(ICON_FA_SAVE " Save Selection")) {
                if (strlen(presetNameBuf) > 0) {
                    std::string pName = presetNameBuf;
                    PresetManager::GetInstance()->AddPresetFromObject(pName, editor_->GetSelectedObject());
                    if (presetThumbnailAlbum_.count(pName)) presetThumbnailAlbum_[pName].isCaptured = false;
                    DebugConsole::GetInstance()->AddLog("Saved Preset: " + pName);
                }
            }
        }
        else {
            ImGui::TextDisabled("(Select an object in Scene to save)");
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("HUDアイコンを再生成")) {
            QueueHudPortraitExports(true);
            DebugConsole::GetInstance()->AddLog("HUD icon regeneration queued.");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("実際の敵プリセットから通常・被弾PNGを生成します");

        ImGui::Separator();
        ImGui::Spacing();

        const auto& presets = PresetManager::GetInstance()->GetPresets();

        if (presets.empty()) {
            ImGui::TextDisabled("(No Presets Saved)");
        }
        else {
            // 名前の先頭のスラッシュ区切りを仮想フォルダーとして表示する。
            std::map<std::string, std::vector<std::string>> folders;
            std::vector<std::string> rootPresets;

            for (const auto& [name, data] : presets) {
                size_t slashPos = name.find('/');
                if (slashPos != std::string::npos) {
                    folders[name.substr(0, slashPos)].push_back(name);
                }
                else {
                    rootPresets.push_back(name);
                }
            }

            // どのフォルダーでも同じサムネイルとコンテキスト操作を使う。
            auto DrawPresetGrid = [&](const std::vector<std::string>& list, const char* tableId) {
                float thumbnailSize = 80.0f;
                float padding = 16.0f;
                float cellSize = thumbnailSize + padding;
                float panelWidth = ImGui::GetContentRegionAvail().x;
                int cols = std::max(1, (int)(panelWidth / cellSize));

                if (ImGui::BeginTable(tableId, cols)) {
                    for (const std::string& name : list) {
                        ImGui::TableNextColumn();
                        const uint64_t thumbnail = ImGui::IsRectVisible(ImVec2(thumbnailSize, thumbnailSize)) ? GetPresetThumbnailGpuPtr(name) : 0;
                        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
                        gpuHandle.ptr = thumbnail;

                        ImGui::PushID(name.c_str());
                        ImGui::BeginGroup();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
                        if (thumbnail) ImGui::ImageButton(name.c_str(), (ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(thumbnailSize, thumbnailSize));
                        else ImGui::Button(name.c_str(), ImVec2(thumbnailSize, thumbnailSize));
                        ImGui::PopStyleColor();

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            ImGui::SetDragDropPayload("PRESET_ASSET", name.c_str(), name.size() + 1);
                            ImGui::Image((ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(32.0f, 32.0f));
                            ImGui::SameLine(); ImGui::Text("Preset: %s", name.c_str());
                            ImGui::EndDragDropSource();
                        }

                        size_t lastSlash = name.find_last_of('/');
                        std::string shortName = PresetEditor::GetInstance()->GetDisplayName(name, presets.at(name));
                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                        ImGui::TextWrapped("%s", shortName.c_str());
                        ImGui::PopTextWrapPos();

                        bool openRename = false; // リネーム画面を開くフラグ

                        // 右クリックから上書き、Prefab化、サムネイル再生成、削除を行う。
                        if (ImGui::BeginPopupContextItem("PresetContextMenu")) {
                            if (ImGui::MenuItem(ICON_FA_EDIT " Rename")) {
                                openRename = true; // 直接開かずフラグを立てる
                            }
                            if (ImGui::MenuItem(ICON_FA_SAVE " Overwrite")) {
                                if (editor_->GetSelectedObject()) {
                                    PresetManager::GetInstance()->AddPresetFromObject(name, editor_->GetSelectedObject());
                                    if (auto thumbnailIt = presetThumbnailAlbum_.find(name); thumbnailIt != presetThumbnailAlbum_.end()) thumbnailIt->second.isCaptured = false;
                                }
                            }
                            if (ImGui::MenuItem(ICON_FA_CODE_BRANCH " Prefabへ変換")) {
                                std::string prefabName = name;
                                int suffix = 1;
                                while (PresetManager::GetInstance()->HasPrefab(prefabName)) {
                                    prefabName = name + "_Prefab" + std::to_string(suffix++);
                                }
                                if (PresetManager::GetInstance()->CreatePrefabFromPreset(name, prefabName)) {
                                    DebugConsole::GetInstance()->AddLog(
                                        "Converted Preset to Prefab: " + name + " -> " + prefabName);
                                }
                            }
                            if (ImGui::MenuItem(ICON_FA_CAMERA " Update Thumbnail")) if (auto thumbnailIt = presetThumbnailAlbum_.find(name); thumbnailIt != presetThumbnailAlbum_.end()) thumbnailIt->second.isCaptured = false;

                            ImGui::Separator();

                            if (ImGui::MenuItem(ICON_FA_TRASH_ALT " Delete")) {
                                PresetManager::GetInstance()->RemovePreset(name);
                                // presetThumbnailAlbum_.erase(name); // GPUクラッシュ防止のため意図的に残さない
                                ImGui::EndPopup(); ImGui::EndGroup(); ImGui::PopID();
                                continue;
                            }
                            ImGui::EndPopup();
                        }

                        // ImGuiのPopupはメニューを閉じた後に開くため、改名要求だけ保持する。
                        if (openRename) {
                            ImGui::OpenPopup("RenamePresetPopup");
                        }

                        // 名前変更後はサムネイルのキャッシュキーも新名称へ移す。
                        if (ImGui::BeginPopup("RenamePresetPopup")) {
                            static char renameBuf[64] = "";

                            // 開いた瞬間だけ文字列をコピーして初期化する
                            if (openRename) {
                                strncpy_s(renameBuf, name.c_str(), sizeof(renameBuf));
                            }

                            ImGui::Text("New Name:");
                            bool isEnter = ImGui::InputText("##NewName", renameBuf, sizeof(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);

                            bool isOk = ImGui::Button("OK");
                            ImGui::SameLine();
                            bool isCancel = ImGui::Button("Cancel");

                            // エンターかOKが押されたらリネーム実行
                            if (isEnter || isOk) {
                                std::string newName = renameBuf;
                                if (!newName.empty() && newName != name) {
                                    PresetManager::GetInstance()->RenamePreset(name, newName);
                                    if (presetThumbnailAlbum_.count(name)) {
                                        presetThumbnailAlbum_[newName] = std::move(presetThumbnailAlbum_[name]);
                                        presetThumbnailAlbum_.erase(name);
                                    }
                                }
                                ImGui::CloseCurrentPopup();
                            }
                            else if (isCancel) {
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::EndGroup();
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                };

            // フォルダーごとに同じ描画ラムダを呼び、ルート直下だけ最後へ置く。
            for (const auto& [folderName, list] : folders) {
                if (ImGui::TreeNodeEx(folderName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    DrawPresetGrid(list, folderName.c_str());
                    ImGui::TreePop();
                }
            }
            // ルート直下の表示
            if (!rootPresets.empty()) {
                if (folders.empty()) DrawPresetGrid(rootPresets, "RootPresetsTable");
                else if (ImGui::TreeNodeEx("Others", ImGuiTreeNodeFlags_DefaultOpen)) {
                    DrawPresetGrid(rootPresets, "RootPresetsTable");
                    ImGui::TreePop();
                }
            }
        }
    }

    // VFXのAsset一覧は別タブへ置き、Preset管理の操作と混ぜない。
        ImGui::Separator();
    ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Prefab")) {
        if (ImGui::CollapsingHeader("Prefabs (v3 / Variants)", ImGuiTreeNodeFlags_DefaultOpen)) {
            static char prefabNameBuf[64] = "NewPrefab";
            ImGui::InputText("Prefab名", prefabNameBuf, IM_ARRAYSIZE(prefabNameBuf));

            Object3d* selectedObject = editor_ ? editor_->GetSelectedObject() : nullptr;
            if (!selectedObject) {
                ImGui::TextDisabled("保存するObjectをHierarchyまたはGame Viewで選択してください。");
            }

            if (ImGui::Button("選択ObjectをPrefab保存") && selectedObject && prefabNameBuf[0] != '\0') {
                PresetManager::GetInstance()->AddPrefabFromObject(prefabNameBuf, selectedObject);
            }

            ImGui::TextDisabled("Scene上のInstanceは元Prefabとのリンクを保持し、InspectorからApply/Revertできます。");
            ImGui::TextDisabled("旧Prefab v1/v2は読み込み時にv3へ移行されます。Presetは右クリックからPrefabへ明示変換できます。");

            const auto& prefabs = PresetManager::GetInstance()->GetPrefabs();
            if (prefabs.empty()) {
                ImGui::TextDisabled("Prefabはまだありません。");
            }

            int prefabIndex = 0;
            for (const auto& [prefabName, prefabJson] : prefabs) {
                const bool isVariant = PresetManager::GetInstance()->IsPrefabVariant(prefabName);
                const std::string basePrefabName = PresetManager::GetInstance()->GetPrefabBaseName(prefabName);
                const auto structureSummary =
                    PresetManager::GetInstance()->GetPrefabStructureOverrideSummary(prefabName);
                ImGui::PushID(prefabName.c_str());
                ImGui::BeginGroup();
                ImGui::Button(isVariant ? "Variant" : "Prefab", ImVec2(86.0f, 46.0f));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && editor_) {
                    editor_->BeginPrefabEditSession(prefabName);
                }
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("PREFAB_ASSET", prefabName.c_str(), prefabName.size() + 1);
                    ImGui::Text("Prefab: %s", prefabName.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::TextWrapped("%s", prefabName.c_str());
                if (isVariant) {
                    ImGui::TextDisabled("Base: %s", basePrefabName.empty() ? "Missing" : basePrefabName.c_str());
                    if (structureSummary.HasOverrides()) {
                        ImGui::TextDisabled("Structure: +%d -%d Move:%d",
                            structureSummary.addedObjects,
                            structureSummary.removedObjects,
                            structureSummary.reparentedObjects);
                    }
                }
                if (ImGui::BeginPopupContextItem("PrefabContextMenu")) {
                    if (ImGui::MenuItem(ICON_FA_CUBE " Prefab Modeで開く") && editor_) {
                        editor_->BeginPrefabEditSession(prefabName);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_FA_CODE_BRANCH " Variantを作成")) {
                        std::string variantName = prefabName + "_Variant";
                        int suffix = 1;
                        while (PresetManager::GetInstance()->HasPrefab(variantName)) {
                            variantName = prefabName + "_Variant" + std::to_string(suffix++);
                        }
                        if (PresetManager::GetInstance()->CreatePrefabVariant(prefabName, variantName)) {
                            DebugConsole::GetInstance()->AddLog(
                                "Created Prefab Variant: " + variantName + " from " + prefabName);
                        }
                    }
                    ImGui::TextDisabled("Asset ID: %s", prefabJson.value("assetId", "").c_str());
                    ImGui::EndPopup();
                }
                ImGui::EndGroup();
                ImGui::PopID();

                ++prefabIndex;
                if (prefabIndex % 4 != 0) {
                    ImGui::SameLine();
                }
            }
        }
    ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("VFX")) {
if (ImGui::CollapsingHeader("VFX / Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string particleDir = "Resources/json/gpu_particles/";
        if (fs::exists(particleDir)) {
            float thumbnailSize = 64.0f;
            float padding = 16.0f;
            float cellSize = thumbnailSize + padding;
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int columnCount = std::max(1, (int)(panelWidth / cellSize));

            if (ImGui::BeginTable("ParticleAssetTable", columnCount)) {
                for (const auto& entry : fs::directory_iterator(particleDir)) {
                    if (entry.path().extension() == ".json") {
                        std::string presetName = entry.path().stem().string();
                        ImGui::TableNextColumn();

                        ImGui::PushID(("Particle_" + presetName).c_str());
                        ImGui::BeginGroup();

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.1f, 0.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.15f, 0.15f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.2f, 0.2f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));

                        ImGui::SetWindowFontScale(2.5f);
                        ImGui::Button(ICON_FA_FIRE, ImVec2(thumbnailSize, thumbnailSize));
                        ImGui::SetWindowFontScale(1.0f);
                        ImGui::PopStyleColor(4);

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            ImGui::SetDragDropPayload("PARTICLE_ASSET", presetName.c_str(), presetName.size() + 1);
                            ImGui::Text("Place Particle: %s", presetName.c_str());
                            ImGui::EndDragDropSource();
                        }

                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                        ImGui::TextWrapped("%s", presetName.c_str());
                        ImGui::PopTextWrapPos();

                        ImGui::EndGroup();
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
        }
    }

    ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::End();
#endif
}
