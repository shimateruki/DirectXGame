#pragma once
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

/// <summary>
/// Sprite描画で共有するルートシグネチャとパイプラインを管理する。
/// </summary>
class SpriteCommon {
public:
    /// <summary>DirectX基盤からSprite用GPUパイプラインを作成する。</summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>Sprite描画用のルートシグネチャとパイプラインを設定する。</summary>
    void SetPipeline(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// DirectX基盤を取得する。
    /// </summary>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    // 定数バッファ、テクスチャ、サンプラの接続を定義する。
    void CreateRootSignature();

    // Sprite描画のブレンド、入力レイアウト、シェーダーを設定する。
    void CreatePipeline();

private:
    // DirectX基盤への参照。SpriteCommonは所有しない。
    DirectXCommon* dxCommon_ = nullptr;

    // スプライト描画で使うD3D12リソース。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;
};
