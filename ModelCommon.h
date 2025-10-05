#pragma once

class DirectXCommon;

// 3D���f���̋��ʕ���
class ModelCommon {
public:
    /// <summary>
    /// ������
    /// </summary>
    /// <param name="dxCommon">DirectX���</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// DirectX��Ղ��擾
    /// </summary>
    /// <returns>DirectX���</returns>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    DirectXCommon* dxCommon_ = nullptr;
};