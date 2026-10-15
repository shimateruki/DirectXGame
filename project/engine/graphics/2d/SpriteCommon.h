#pragma once
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

namespace engine {

/// <summary>
/// Sprite描画で共有するルートシグネチャとパイプラインを管理する。
/// </summary>
class SpriteCommon {
public:
    /// <summary>DirectX基盤からSprite用GPUパイプラインを作成する。</summary>
    /// <param name="dxCommon">GPUデバイスとシェーダーコンパイルを提供する非所有参照。</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>Sprite描画用のルートシグネチャとパイプラインを設定する。</summary>
    /// <param name="commandList">設定先の描画コマンドリスト。</param>
    void SetPipeline(ID3D12GraphicsCommandList* commandList);

    /// <summary>初期化時に渡されたDirectX基盤の非所有参照を返す。</summary>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    /// <summary>定数バッファ、テクスチャ、サンプラの接続定義を作る。</summary>
    void CreateRootSignature();

    /// <summary>Sprite描画のブレンド、入力レイアウト、シェーダーを設定する。</summary>
    void CreatePipeline();

private:
    // DirectX基盤への参照。SpriteCommonは所有しない。
    DirectXCommon* dxCommon_ = nullptr;

    // スプライト描画で使うD3D12リソース。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;
};

} // namespace engine
