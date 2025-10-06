#include "engine/base/Framework.h"
#include "engine/3d/TextureManager.h"
#include "engine/3d/ModelManager.h"

void Framework::Initialize() {
    // COMの初期化
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // --- エンジンシステムの初期化 ---
    winApp_ = std::make_unique<WinApp>();
    winApp_->Initialize(L"CG2", 1280, 720);

    dxCommon_ = DirectXCommon::GetInstance();
    dxCommon_->Initialize(winApp_.get());

    inputManager_ = std::make_unique<InputManager>();
    inputManager_->Initialize(winApp_->GetHwnd());

    // --- マネージャクラスの初期化 ---
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