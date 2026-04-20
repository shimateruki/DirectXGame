#pragma once
#include <string>
#include <map>
#include <wrl.h>
#include <d3d12.h>
#include <memory>
class DebugEditor; // 前方宣言
class DirectXCommon;
class Object3d;
class Object3dCommon;
struct ThumbnailData {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;       // 写真の画用紙（実体）
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;  // 描き込むための筆（RTV）
    uint32_t srvHandle = 0;                                // ImGuiに渡す画像ハンドル（SRV）
    bool isCaptured = false;                               // 撮影済みフラグ
    std::shared_ptr<Object3d> previewObject;
};

class ProjectWindow {
public:
    ProjectWindow() = default;
    ~ProjectWindow() = default;

    // DirectXCommonを受け取れるように引数を追加！
    void Initialize(DebugEditor* editor, DirectXCommon* dxCommon);

    // 毎フレームの描画処理 (UI)
    void Draw();

    //  ゲームの描画ループの先頭で呼ばれる「裏撮影」関数
    void CapturePendingThumbnails();

private:
    // 画用紙を作る関数
    void CreateThumbnailResource(const std::string& modelName);

    DebugEditor* editor_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr; // 追加
    std::unique_ptr<Object3dCommon> previewObject3dCommon_;
    // モデル名と写真のヒモヅケ辞書（アルバム）
    std::map<std::string, ThumbnailData> thumbnailAlbum_;
    const int kThumbnailSize = 256; // 写真の高解像度サイズ
};