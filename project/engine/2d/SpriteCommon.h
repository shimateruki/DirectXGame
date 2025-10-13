#pragma once
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon; // 前方宣言

class SpriteCommon {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// 共通描画設定（描画前に呼ぶ）
    /// </summary>
    void SetPipeline(ID3D12GraphicsCommandList* commandList);
    /// <summary>
/// DirectX基盤を取得
/// </summary>
/// <returns>DirectX基盤</returns>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    /// <summary>
    /// ルートシグネチャの作成
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// グラフィックスパイプラインの生成
    /// </summary>
    void CreatePipeline();

 
private:
    // DirectX基盤クラスのポインタ
    DirectXCommon* dxCommon_ = nullptr;
    // ルートシグネチャ
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
    // パイプラインステートオブジェクト
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;
};