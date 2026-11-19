#pragma once

#include <d3d12.h>
#include "Camera.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

namespace engine { class DirectXCommon; }
namespace engine { class Object3D; class Object3DCommon; }

// モデル・プリセット・VFX・PBR素材を専用の描画先に描き、サムネイルを保持する。
// ProjectWindowやInspectorへ表示用のGPUハンドルを渡し、ゲームのSceneには登録しない。
// 主な入口：Initialize / GetModelThumbnailGpuPtr / CapturePendingThumbnails
class AssetThumbnailRenderer {
public:
    AssetThumbnailRenderer();
    ~AssetThumbnailRenderer();

    void Initialize(engine::DirectXCommon* dxCommon);
    // 描画コマンド記録中に画像を生成し、呼び出し元のカメラと描画範囲を復元する。
    void CapturePendingThumbnails();
    // フレームのGPUコマンド送信後に、撮影済みHUDポートレートをPNGへ書く。
    void ExportCapturedHudPortraits();
    void QueueHudPortraitExports(bool forceAll);

    // 戻り値はImGui等へ渡すSRVのGPUハンドル。
    uint64_t GetModelThumbnailGpuPtr(const std::string& modelName);
    uint64_t GetPresetThumbnailGpuPtr(const std::string& presetName);
    uint64_t GetPresetPreviewGpuPtr(const std::string& presetName, float yaw, float pitch, float zoom);
    uint64_t GetPbrThumbnailGpuPtr(const std::string& name, const std::string& albedoPath,
        const std::string& normalPath, const std::string& ormPath);

    void InvalidatePresetThumbnail(const std::string& presetName);
    void InvalidatePbrThumbnails();
    void RenamePresetThumbnail(const std::string& oldName, const std::string& newName);

private:
    // GPUリソースと、GPU処理完了まで保持が必要なプレビューオブジェクト。
    struct ThumbnailData {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
        uint32_t srvHandle = 0;

        bool isCaptured = false;
        bool isHudPortrait = false;

        bool exportRequested = false;
        std::string exportFileStem;
        // キューへ入れた時点の保存先。後から登録が変わっても出力待ちの画像を別の場所へ書かない。
        std::string exportDirectory;

        float portraitYaw = 0.0f;
        float portraitPitch = -0.2f;
        float portraitScale = 1.0f;
        float portraitOffsetY = 0.0f;

        float previewYaw = 0.0f;
        float previewPitch = 0.0f;
        float previewZoom = 1.0f;

        engine::Vector3 previewCenter{};
        engine::Vector3 previewExtent{};
        bool previewHasBounds = false;

        bool contentDirty = true;
        std::shared_ptr<engine::Object3D> previewObject;

        // セットの各ノードを所有し、親子関係と元の相対サイズを保持します。
        std::vector<std::shared_ptr<engine::Object3D>> previewObjects;

        // 再撮影直前のGPU参照が残り得るため、旧プレビューはGPUの処理完了まで保持します。
        std::vector<std::shared_ptr<engine::Object3D>> retiredPreviewObjects;
        uint64_t retiredPreviewFence = 0;
    };

    /// 指定モデルのプレビューObjectと描画先を作り、サムネイル撮影の準備をする。
    void CreateThumbnailResource(const std::string& modelName);

    // Presetのプレビューとサムネイル描画先を準備する。
    void CreatePresetThumbnailResource(const std::string& presetName);

    // PBRセットのサムネイル描画先を準備する。
    void CreatePbrThumbnailResource(const std::string& name);

    engine::DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<engine::Object3DCommon> previewObject3DCommon_;

    // モデル/プリセット名からサムネイル情報を引くキャッシュ。
    std::map<std::string, ThumbnailData> thumbnailAlbum_;
    std::map<std::string, ThumbnailData> presetThumbnailAlbum_;

    /// <summary>PBRプリセットの画像パスと、描画完了まで必要なサムネイル情報を保持する。</summary>
    // 関連：AssetThumbnailRenderer（この型を使う機能の本体）
    struct PbrThumbnailData {
        ThumbnailData thumbnail;
        std::string albedoPath;
        std::string normalPath;
        std::string ormPath;
        uint64_t lastUseFence = 0;
    };

    std::map<std::string, PbrThumbnailData> pbrThumbnailAlbum_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pbrPreviewLightResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pbrPreviewPointLightsResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pbrPreviewSpotLightsResource_;
    const int kThumbnailSize = 256;

    std::map<std::string, uint64_t> presetThumbnailUseFence_;

    engine::Camera studioCamera_;
    Microsoft::WRL::ComPtr<ID3D12Resource> studioDepth_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> studioDsvHeap_;
    uint32_t studioDepthSrv_ = 0;
    float portraitRotationY_ = 0.0f;
    bool isStudioCameraInitialized_ = false;
    bool hudPortraitAutoScanDone_ = false;
};
