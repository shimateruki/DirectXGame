#include "engine/base/Framework.h"
#include "engine/3d/TextureManager.h"
#include "engine/3d/ModelManager.h"

void Framework::Initialize() {
    // COM�̏�����
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // --- �G���W���V�X�e���̏����� ---
    winApp_ = std::make_unique<WinApp>();
    winApp_->Initialize(L"CG2", 1280, 720);

    dxCommon_ = DirectXCommon::GetInstance();
    dxCommon_->Initialize(winApp_.get());

    inputManager_ = std::make_unique<InputManager>();
    inputManager_->Initialize(winApp_->GetHwnd());

    // --- �}�l�[�W���N���X�̏����� ---
    ModelManager::GetInstance()->Initialize(dxCommon_);
    TextureManager::GetInstance()->Initialize(dxCommon_);
}

void Framework::Finalize() {
    // --- �}�l�[�W���N���X�̏I������ ---
    ModelManager::GetInstance()->Finalize();

    // --- �G���W���V�X�e���̏I������ ---
    dxCommon_->Finalize();

    // COM�̏I������
    CoUninitialize();
}

void Framework::Run() {
    // ���C�����[�v
    while (winApp_->Update() == false) {
        // �X�V����
        Update();
        // �`�揈��
        Draw();
    }
}