#include "Framework.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "SRVManager.h" // ★ SRVManager.h をインクルード
#include"ImguiManager.h"
void Framework::Initialize() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    winApp_ = std::make_unique<WinApp>();
    winApp_->Initialize(L"GE3", WinApp::kClientWidth, WinApp::kClientHeight);

    dxCommon_ = DirectXCommon::GetInstance();
    dxCommon_->Initialize(winApp_.get());

    InputManager::GetInstance()->Initialize(winApp_->GetHwnd());

    audioPlayer_ = AudioPlayer::GetInstance();
    audioPlayer_->Initialize();

    SRVManager::GetInstance()->Initialize(dxCommon_);
    ImGuiManager::GetInstance()->Initialize(winApp_.get(), dxCommon_);
    // ModelManagerとTextureManagerの初期化
    ModelManager::GetInstance()->Initialize(dxCommon_);
    TextureManager::GetInstance()->Initialize(dxCommon_);

}

void Framework::Finalize() {
    if (dxCommon_) {
        // --- GPU処理が完全に終わるまで待機し、リセット ---
        dxCommon_->FlushCommandQueue(true);
    }
    AudioPlayer::GetInstance()->Finalize();
    ImGuiManager::GetInstance()->Finalize();
    ModelManager::GetInstance()->Finalize();

    dxCommon_->Finalize();
    CoUninitialize();
}



void Framework::Run() {
    // (変更なし)
    while (winApp_->Update() == false) {
        Update();
        Draw();
    }
}