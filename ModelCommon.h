//================================================================
// ModelCommon.h (�V�K�쐬)
//================================================================
#pragma once

// �O���錾
class DirectXCommon;

/// <summary>
/// 3D���f���ŋ��ʂ��ė��p����f�[�^�⏈�����܂Ƃ߂��N���X
/// </summary>
class ModelCommon {
public:
    /// <summary>
    /// ����������
    /// </summary>
    /// <param name="dxCommon">DirectX�̊�ՃI�u�W�F�N�g</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// DirectXCommon�̃|�C���^���擾����
    /// </summary>
    /// <returns>DirectXCommon�̃|�C���^</returns>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    // DirectX��� (�O������؂�Ă���)
    DirectXCommon* dxCommon_ = nullptr;
};