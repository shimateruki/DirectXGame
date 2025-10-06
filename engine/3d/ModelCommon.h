#pragma once

class DirectXCommon;

// 3Dモデルの共通部分
class ModelCommon {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="dxCommon">DirectX基盤</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// DirectX基盤を取得
    /// </summary>
    /// <returns>DirectX基盤</returns>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    DirectXCommon* dxCommon_ = nullptr;
};