#pragma once

class DirectXCommon;

/// <summary>
/// 3Dモデル描画で共有するDirectX基盤参照を保持する。
/// </summary>
class ModelCommon {
public:
    /// <summary>
    /// DirectX基盤を登録する。
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// DirectX基盤を取得する。
    /// </summary>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    // DirectX基盤への参照。ModelCommonは所有しない。
    DirectXCommon* dxCommon_ = nullptr;
};
