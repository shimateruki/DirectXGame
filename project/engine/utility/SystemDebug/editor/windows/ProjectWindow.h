#pragma once
#include "Camera.h"
#include <d3d12.h>
#include <map>
#include <memory>
#include <string>
#include <wrl.h>

class DebugEditor;
class DirectXCommon;
class Object3d;
class Object3dCommon;

/// <summary>
/// Projectウィンドウで表示するモデル/プリセットのサムネイル情報。
/// </summary>
struct ThumbnailData {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    uint32_t srvHandle = 0;
    bool isCaptured = false;
    std::shared_ptr<Object3d> previewObject;
};

/// <summary>
/// モデル、プリセット、シーン素材を一覧し、配置やサムネイル生成を行うProjectウィンドウ。
/// </summary>
class ProjectWindow {
public:
    ProjectWindow() = default;
    ~ProjectWindow() = default;

    /// <summary>
    /// 親EditorとDirectX基盤を登録する。
    /// </summary>
    void Initialize(DebugEditor* editor, DirectXCommon* dxCommon);

    /// <summary>
    /// ProjectウィンドウのUIを描画する。
    /// </summary>
    void Draw();

    /// <summary>
    /// 未撮影のサムネイルをGameView描画ループの先頭で撮影する。
    /// </summary>
    void CapturePendingThumbnails();

    uint64_t GetPresetThumbnailGpuPtr(const std::string& presetName);

private:
    void CreateThumbnailResource(const std::string& modelName);
    void CreatePresetThumbnailResource(const std::string& presetName);

    // EditorとDirectX基盤への参照。ProjectWindowは所有しない。
    DebugEditor* editor_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;

    std::unique_ptr<Object3dCommon> previewObject3dCommon_;

    // モデル/プリセット名からサムネイル情報を引くキャッシュ。
    std::map<std::string, ThumbnailData> thumbnailAlbum_;
    std::map<std::string, ThumbnailData> presetThumbnailAlbum_;
    const int kThumbnailSize = 256;

    std::string currentModelDirectory_ = "Resources/3DModel/";

    // サムネイル撮影用の簡易スタジオカメラ。
    Camera studioCamera_;
    bool isStudioCameraInitialized_ = false;
};
