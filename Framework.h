#pragma once

#include "WinApp.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include <memory>

// �Q�[���G���W���̔ėp�I�Ȋ�ՃN���X
class Framework {
public:
    /// <summary>
    /// �f�X�g���N�^
    /// </summary>
    virtual ~Framework() = default;

    /// <summary>
    /// ������
    /// </summary>
    virtual void Initialize();

    /// <summary>
    /// �I������
    /// </summary>
    virtual void Finalize();

    /// <summary>
    /// ���C�����[�v�����s
    /// </summary>
    void Run();

protected:
    /// <summary>
    /// ���t���[���̍X�V�����i�p����ŃI�[�o�[���C�h�j
    /// </summary>
    virtual void Update() = 0; // �������z�֐�

    /// <summary>
    /// �`�揈���i�p����ŃI�[�o�[���C�h�j
    /// </summary>
    virtual void Draw() = 0; // �������z�֐�

protected:
    // --- �G���W���V�X�e�� ---
    std::unique_ptr<WinApp> winApp_;
    DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<InputManager> inputManager_;
};