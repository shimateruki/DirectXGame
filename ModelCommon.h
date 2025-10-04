//================================================================
// ModelCommon.h (新規作成)
//================================================================
#pragma once

// 前方宣言
class DirectXCommon;

/// <summary>
/// 3Dモデルで共通して利用するデータや処理をまとめたクラス
/// </summary>
class ModelCommon {
public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXの基盤オブジェクト</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// DirectXCommonのポインタを取得する
    /// </summary>
    /// <returns>DirectXCommonのポインタ</returns>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    // DirectX基盤 (外部から借りてくる)
    DirectXCommon* dxCommon_ = nullptr;
};