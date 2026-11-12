#pragma once
#include "DirectXCommon.h"
#include "WinApp.h"

#ifdef USE_IMGUI
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#endif

// 担当：ImGuiのコンテキスト・フレーム・描画と後片付けを管理する。
// 関連：Framework（寿命）、WinApp（入力）、DirectXCommon / SrvManager（描画）
class ImGuiManager {
public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static ImGuiManager* GetInstance();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(WinApp* winApp, DirectXCommon* dxCommon);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// フレームの開始
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// フレームの終了と描画
    /// </summary>
    // 複数ビューポート有効時のサブウィンドウを更新・描画する。本画面へのコマンド発行は Draw が担う。
    void EndFrame();

    /// <summary>作成済みのImGui描画データをコマンドリストへ送る。</summary>
    void Draw(); 

private:
    ImGuiManager() = default;

    ~ImGuiManager() = default;

    ImGuiManager(const ImGuiManager&) = delete;

    ImGuiManager& operator=(const ImGuiManager&) = delete;

private:
    DirectXCommon* dxCommon_ = nullptr;
};
