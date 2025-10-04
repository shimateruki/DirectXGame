#pragma once
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon; // 前方宣言

class Object3dCommon {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// 描画前の共通コマンド（描画前に呼ぶ）
    /// </summary>
    void PreDraw(ID3D12GraphicsCommandList* commandList);

private:
    /// <summary>
    /// ルートシグネチャの作成
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// グラフィックスパイプラインの生成
    /// </summary>
    void CreateGraphicsPipeline();

private:
    // DirectX基盤クラスのポインタ
    DirectXCommon* dxCommon_ = nullptr;
    // ルートシグネチャ
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
    // パイプラインステートオブジェクト
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;
};