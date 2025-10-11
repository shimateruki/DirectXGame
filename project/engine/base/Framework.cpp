#include "engine/base/Framework.h"
#include "engine/3d/TextureManager.h"
#include "engine/3d/ModelManager.h"

void Framework::Initialize() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    winApp_ = std::make_unique<WinApp>();
    winApp_->Initialize(L"GE3", WinApp::kClientWidth, WinApp::kClientHeight);

    dxCommon_ = DirectXCommon::GetInstance();
    dxCommon_->Initialize(winApp_.get());


    InputManager::GetInstance()->Initialize(winApp_->GetHwnd());

    // --- AudioPlayerの初期化を追加 ---
    audioPlayer_ = AudioPlayer::GetInstance();
    audioPlayer_->Initialize();

    ModelManager::GetInstance()->Initialize(dxCommon_);
    TextureManager::GetInstance()->Initialize(dxCommon_);
}

void Framework::Finalize() {
    // --- マネージャクラスの終了処理 ---
    ModelManager::GetInstance()->Finalize();

    // --- エンジンシステムの終了処理 ---
    dxCommon_->Finalize();


    // COMの終了処理
    CoUninitialize();
}

void Framework::Run() {
    // メインループ
    while (winApp_->Update() == false) {
        // 更新処理
        Update();
        // 描画処理
        Draw();
    }
}