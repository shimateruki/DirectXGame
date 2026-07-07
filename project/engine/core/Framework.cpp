#include "Framework.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "SRVManager.h" 
#include"ImguiManager.h"
#include"LightManager.h"
// ウィンドウ、DirectX、入力、音声、ImGui、各種マネージャを起動順に初期化する。

void Framework::Initialize() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    winApp_ = std::make_unique<WinApp>();
    winApp_->Initialize(L"CG4", WinApp::kClientWidth, WinApp::kClientHeight);

    dxCommon_ = DirectXCommon::GetInstance();
    dxCommon_->Initialize(winApp_.get());

    InputManager::GetInstance()->Initialize(winApp_->GetHwnd());

    audioPlayer_ = AudioPlayer::GetInstance();
    audioPlayer_->Initialize();

    SRVManager::GetInstance()->Initialize(dxCommon_);
    dxCommon_->CreateDepthSrv();
    dxCommon_->CreateShadowMap();
    ImGuiManager::GetInstance()->Initialize(winApp_.get(), dxCommon_);
    // ModelManagerとTextureManagerの初期化
    ModelManager::GetInstance()->Initialize(dxCommon_);
    TextureManager::GetInstance()->Initialize(dxCommon_);
    LightManager::GetInstance()->Initialize(dxCommon_);

}
// GPU処理の完了を待ってから、音声・ImGui・モデル・DirectXなどを終了する。

void Framework::Finalize() {
    if (dxCommon_) {
        // --- GPU処理が完全に終わるまで待機し、リセット ---
        dxCommon_->FlushCommandQueue();
    }
    AudioPlayer::GetInstance()->Finalize();
    ImGuiManager::GetInstance()->Finalize();
    ModelManager::GetInstance()->Finalize();

    dxCommon_->Finalize();
    CoUninitialize();
}
// ウィンドウが閉じられるまで、更新と描画を毎フレーム実行する。



void Framework::Run() {
    while (winApp_->Update() == false) {
        Update();
        Draw();
    }
}