#include "Framework.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "SRVManager.h" 
#include"ImguiManager.h"
#include"LightManager.h"
void Framework::Initialize() {
	// ウィンドウ、DirectX、入力、音声、ImGui、各種マネージャを起動順に初期化する。
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    winApp_ = std::make_unique<WinApp>();
    winApp_->Initialize(L"スライムの大冒険", WinApp::kClientWidth, WinApp::kClientHeight);

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

void Framework::Finalize() {
	// GPU処理の完了を待ってから、音声・ImGui・モデル・DirectXなどを終了する。
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

void Framework::Run() {
	// ウィンドウが閉じられるまで、更新と描画を毎フレーム実行する。
    while (winApp_->Update() == false) {
        Update();
        Draw();
    }
}