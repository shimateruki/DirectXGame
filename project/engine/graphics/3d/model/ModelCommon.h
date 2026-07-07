#pragma once

class DirectXCommon;

/// <summary>
/// 3Dモデル描画で共有するDirectX基盤参照を保持する。
/// </summary>
// ModelCommonは、Model読み込みと描画に必要なDirectX共通参照を保持します。
class ModelCommon {
public:
    /// <summary>
    /// DirectX基盤を登録する。
    /// </summary>
        // モデル関連クラスが使うDirectXCommonを設定します。
void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// DirectX基盤を取得する。
    /// </summary>
        // モデル描画やリソース作成で使うDirectXCommonを取得します。
DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    // DirectX基盤への参照。ModelCommonは所有しない。
    DirectXCommon* dxCommon_ = nullptr;
};
