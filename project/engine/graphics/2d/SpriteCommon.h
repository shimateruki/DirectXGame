#pragma once
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

/// <summary>
/// スプライト描画で共有するルートシグネチャとパイプラインを管理する。
/// </summary>
// SpriteCommonは、Sprite描画で共通利用するRootSignatureとPipelineStateを管理します。
class SpriteCommon {
public:
    /// <summary>
    /// DirectX基盤を受け取り、スプライト描画用パイプラインを初期化する。
    /// </summary>
        // DirectX共通機能を受け取り、Sprite用GPUパイプラインを作成します。
void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// スプライト描画前に共通パイプラインをコマンドリストへ設定する。
    /// </summary>
        // Sprite描画前に必要なRootSignatureとPipelineStateをコマンドリストへ設定します。
void SetPipeline(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// DirectX基盤を取得する。
    /// </summary>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    /// <summary>
    /// スプライト描画用のルートシグネチャを生成する。
    /// </summary>
        // Sprite用の定数バッファ、テクスチャ、サンプラ構成を定義します。
void CreateRootSignature();

    /// <summary>
    /// スプライト描画用のグラフィックスパイプラインを生成する。
    /// </summary>
        // Sprite描画に使うブレンド、入力レイアウト、シェーダーパイプラインを作成します。
void CreatePipeline();

private:
    // DirectX基盤への参照。SpriteCommonは所有しない。
    DirectXCommon* dxCommon_ = nullptr;

    // スプライト描画で使うD3D12リソース。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;
};
