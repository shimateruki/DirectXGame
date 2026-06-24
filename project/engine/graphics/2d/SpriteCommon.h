#pragma once
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

/// <summary>
/// スプライト描画で共有するルートシグネチャとパイプラインを管理する。
/// </summary>
class SpriteCommon {
public:
    /// <summary>
    /// DirectX基盤を受け取り、スプライト描画用パイプラインを初期化する。
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// スプライト描画前に共通パイプラインをコマンドリストへ設定する。
    /// </summary>
    void SetPipeline(ID3D12GraphicsCommandList* commandList);

    /// <summary>
    /// DirectX基盤を取得する。
    /// </summary>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    /// <summary>
    /// スプライト描画用のルートシグネチャを生成する。
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// スプライト描画用のグラフィックスパイプラインを生成する。
    /// </summary>
    void CreatePipeline();

private:
    // DirectX基盤への参照。SpriteCommonは所有しない。
    DirectXCommon* dxCommon_ = nullptr;

    // スプライト描画で使うD3D12リソース。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;
};
