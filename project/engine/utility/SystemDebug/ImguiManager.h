#pragma once
#include "DirectXCommon.h"
#include "WinApp.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

/// <summary>
/// ImGuiの管理クラス
/// </summary>
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
    void EndFrame();

    void Draw(); 

private:
    ImGuiManager() = default;
    ~ImGuiManager() = default;
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

private:
    DirectXCommon* dxCommon_ = nullptr;
};