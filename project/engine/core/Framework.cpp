#include "Framework.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "SrvManager.h" 
#include"ImGuiManager.h"
#include"LightManager.h"
#include "engine/system/scene/LoadingProfiler.h"

namespace engine {
void Framework::Initialize() {
	// ウィンドウ、DirectX、入力、音声、ImGui、各種マネージャを起動順に初期化する。
    StartupProfileScope frameworkProfile("Framework::Initialize");
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    winApp_ = std::make_unique<WinApp>();
    winApp_->Initialize(L"スライムの大冒険", 1280, 720);

    dxCommon_ = DirectXCommon::GetInstance();
    {
        StartupProfileScope phase("DirectX device");
        dxCommon_->Initialize(winApp_.get());
    }

    engine::InputManager::GetInstance()->Initialize(winApp_->GetHwnd());

    audioPlayer_ = AudioPlayer::GetInstance();
    audioPlayer_->Initialize();

    // 先に描画用の記述子を確保できるようにし、深度・影のビューとImGuiの初期化を続ける。
    SrvManager::GetInstance()->Initialize(dxCommon_);
    dxCommon_->CreateDepthSrv();
    dxCommon_->CreateShadowMap();
    ImGuiManager::GetInstance()->Initialize(winApp_.get(), dxCommon_);

    // ModelManagerとTextureManagerの初期化
    {
        StartupProfileScope phase("ModelManager::Initialize");
        ModelManager::GetInstance()->Initialize(dxCommon_);
    }

    TextureManager::GetInstance()->Initialize(dxCommon_);
    LightManager::GetInstance()->Initialize(dxCommon_);

    // 重いGPU準備はSceneManagerが必要な範囲だけ行う。ここでは所有者とDeviceを結び付ける。
    renderCommonResources_ = std::make_unique<engine::RenderCommonResources>();
    renderCommonResources_->Initialize(dxCommon_);

}

void Framework::Finalize() {
	// GPU処理の完了を待ってから、音声・ImGui・モデル・DirectXなどを終了する。
    if (dxCommon_) {
        // --- GPU処理が完全に終わるまで待機し、リセット ---
        dxCommon_->FlushCommandQueue();
    }

    AudioPlayer::GetInstance()->Finalize();
    ImGuiManager::GetInstance()->Finalize();
    ModelManager::GetInstance()->Finalize();

    // SceneとEditorの終了・GPU待機が完了してから、共有PSOをDeviceより先に破棄する。
    renderCommonResources_.reset();
    dxCommon_->Finalize();
    // OS側のCOMを使う描画基盤を解放してから、初期化時のCOM登録を解除する。
    CoUninitialize();
}

void Framework::Run() {
	// ウィンドウが閉じられるまで、更新と描画を毎フレーム実行する。
    while (winApp_->Update() == false) {
        Update();
        Draw();
    }
}

} // namespace engine
