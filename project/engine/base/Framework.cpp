#include "engine/base/Framework.h"
#include "engine/3d/TextureManager.h"
#include "engine/3d/ModelManager.h"
#include "engine/base/SRVManager.h" // ★ SRVManager.h をインクルード
#include"engine//io/ImguiManager.h"
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