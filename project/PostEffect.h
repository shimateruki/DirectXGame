#pragma once
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <vector>
#include <string>

class DirectXCommon;

/// <summary>
/// ブルーム、トーンマップ、各種レンズエフェクトを管理するポストプロセス制御クラス
/// </summary>
class PostEffect {
public:
    // 定数バッファ用構造体 (16バイト境界に準拠)
    struct Params {
        // --- Bloom ---
        float threshold = 1.5f;             // 高輝度抽出しきい値
        float bloomIntensity = 2.0f;        // ブルーム合成強度
        float spread = 2.0f;                // サンプリングの広がり
        int32_t enableToneMapping = 1;      // 0:Off, 1:ACES, 2:Luminance-ACES

        // --- Lens ---
        float vignetteIntensity = 1.0f;     // 周辺減光の強さ
        float chromaticAberration = 0.02f;  // 色収差のズレ幅
        float filmGrainIntensity = 0.03f;   // フィルム粒子の強さ
        float time = 0.0f;                  // 時間経過（ノイズアニメ用）

        // --- Radial Blur ---
        float radialCenterX = 0.5f;         // 放射ブラー中心X
        float radialCenterY = 0.5f;         // 放射ブラー中心Y
        float radialIntensity = 0.0f;       // 放射ブラー強度
        float radialPadding = 0.0f;

        // --- Color Grading & Action ---
        float lutIntensity = 0.0f;          // LUT適用強度
        float damageFlash = 0.0f;           // 被弾時の画面赤化
        float cinemaBarHeight = 0.0f;       // シネマスコープ（上下黒帯）の高さ
        float wobbleIntensity = 0.0f;       // 画面の波打ち歪み

        // --- Retro ---
        float scanlineIntensity = 0.0f;     // ブラウン管走査線
        float mosaicSize = 0.0f;            // ピクセルモザイクサイズ
        float padding1 = 0.0f;
        float padding2 = 0.0f;
        float padding3 = 0.0f;
    };

    // 初期化: 各パス用のリソースとPSOを生成
    void Initialize(DirectXCommon* dxCommon);

    // 描画実行: 指定したPSOとSRVを用いてパスを実行
    void Draw(ID3D12GraphicsCommandList* commandList, uint32_t srvHandle, int psoIndex = 0);

    // 描画前準備: 指定したレンダーターゲットをセット
    void PreDrawScene(ID3D12GraphicsCommandList* commandList, int targetTexIndex = 0, bool clear = true);

    // リソースバリア管理
    void TransitionToSRV(ID3D12GraphicsCommandList* commandList, int texIndex);
    void TransitionToRTV(ID3D12GraphicsCommandList* commandList, int texIndex);

    // アクセッサ
    uint32_t GetSRVHandle(int texIndex = 0) const { return renderTextures_[texIndex].srvHandle; }
    ID3D12Resource* GetRenderTexture(int texIndex = 0) const { return renderTextures_[texIndex].resource.Get(); }
    Params* GetParams() { return paramsData_; }
    void SetLUTTexture(uint32_t srvHandle) { lutSrvHandle_ = srvHandle; }

private:
    void CreateMesh();
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateRenderTexture(int texIndex, int width, int height, DXGI_FORMAT format);
    void CreateConstBuffer();

private:
    // レンダーターゲット管理用
    struct RenderTexture {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
        uint32_t srvHandle = 0;
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };

    // マルチパス用の内部テクスチャチェーン
    std::vector<RenderTexture> renderTextures_;

    // 各パス（Copy, Composite, Blur, etc...）ごとのパイプラインステート
    std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelineStates_;

    DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

    // 定数バッファ関連
    Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
    Params* paramsData_ = nullptr;

    // 描画用矩形メッシュ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    struct VertexPosUv {
        Vector3 pos;
        Vector2 uv;
    };

    // カラーグレーディング用LUTのSRVハンドル
    uint32_t lutSrvHandle_ = 0;
};