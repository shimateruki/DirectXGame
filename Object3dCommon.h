#pragma once

#include "DirectXCommon.h"
#include <wrl.h>

/// <summary>
/// 3Dオブジェクトの描画に関わる共通処理をまとめたクラス
/// </summary>
class Object3dCommon {
public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectX汎用クラスのインスタンス</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// 3Dオブジェクト描画前の共通コマンドを設定する
    /// </summary>
    void SetGraphicsCommand();

private:
    /// <summary>
    /// ルートシグネチャの作成
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// パイプラインステートの作成
    /// </summary>
    void CreatePipelineState();

private:
    // DirectX汎用クラス（ポインタ）
    DirectXCommon* dxCommon_ = nullptr;
    // ルートシグネチャ
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    // パイプラインステート
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;
};