#pragma once
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <cstdint>
#include <vector>
#include <string>

class DirectXCommon;

/// <summary>
/// ブルーム、トーンマップ、各種レンズエフェクトを管理するポストプロセス制御クラス
/// </summary>
// PostEffectは、レンダー済み画面へブルーム、色調補正、アウトラインなどの画面効果を適用します。
class PostEffect {
public:
    static constexpr int kCameraPreviewTextureIndex = 6;
    static constexpr int kCinematicCameraPreviewTextureIndex = 7;

    enum PipelineIndex : int {
        kCopyPipeline = 0,
        kCompositeHdrPipeline,
        kExtractPipeline,
        kDownsamplePipeline,
        kAddPipeline,
        kCompositeBackBufferPipeline,
    };

    // ブルームで生成する縮小テクスチャ段数です。
    // 高品質は従来と同じ4段、低品質ほど全画面パスを減らします。
    enum class BloomQuality : int32_t {
        Low = 0,
        Medium,
        High,
    };

    static PostEffect* GetInstance() {
        static PostEffect instance;
        return &instance;
    }
    // 定数バッファ用構造体 (16バイト境界に準拠)
        // ポストエフェクトシェーダーへ渡す全画面効果のパラメータ群です。
struct Params {
        // --- Bloom ---
        float threshold = 1.0f;             // 高輝度抽出しきい値
        float bloomIntensity = 0.0f;        // ブルーム合成強度
        float spread = 1.0f;                // サンプリングの広がり
        int32_t enableToneMapping = 0;      // 0:Off, 1:ACES, 2:Luminance-ACES

        // --- Lens ---
        float vignetteIntensity = 0.0f;     // 周辺減光の強さ
        float chromaticAberration = 0.0f;   // 色収差のズレ幅
        float filmGrainIntensity = 0.0f;    // フィルム粒子の強さ
        float vignettePower = 1.0f;         // 周辺減光の丸み (資料)
        float time = 0.0f;                  // 時間経過（ノイズアニメ用）


        // --- Radial Blur ---
        float radialCenterX = 0.5f;         // 放射ブラー中心X
        float radialCenterY = 0.5f;         // 放射ブラー中心Y
        float radialIntensity = 0.0f;       // 放射ブラー強度
        int32_t radialBlurSamples = 1;      // サンプリング数 (資料)

        // --- Color Grading & Action ---
        float lutIntensity = 0.0f;          // LUT適用強度
        float colorExposure = 0.0f;         // Color grading exposure
        float colorContrast = 1.0f;         // Color grading contrast
        float colorSaturation = 1.0f;       // Color grading saturation
        float colorTemperature = 0.0f;      // Warm/cool color shift
        float colorTint = 0.0f;             // Green/magenta color shift
        float damageFlash = 0.0f;           // 被弾時の画面赤化
        float cinemaBarHeight = 0.0f;       // シネマスコープ（上下黒帯）の高さ
        float wobbleIntensity = 0.0f;       // 画面の波打ち歪み

        // --- Retro ---
        float scanlineIntensity = 0.0f;     // ブラウン管走査線
        float mosaicSize = 0.0f;            // ピクセルモザイクサイズ
        float dangerVignette = 0.0f;
        float blackout = 0.0f;
        float grayscaleIntensity = 0.0f;
        float sepiaIntensity = 0.0f;
        int32_t boxFilterSize = 0;          // 0:Off, 1:3x3, 2:5x5... (資料)
        int32_t gaussianFilterSize = 0;     // 0:Off, 1:3x3, 2:5x5... (資料)
        float gaussianSigma = 1.0f;         // ガウス関数のシグマ (資料)
        float luminanceOutlineIntensity = 0.0f; // 輝度ベースのアウトライン (資料)
        float depthOutlineIntensity = 0.0f;     // 深度ベースのアウトライン (資料)
        
        // --- Dissolve & Random ---
        float dissolveThreshold = 0.0f;     // ディゾルブのしきい値 (資料)
        float dissolveEdgeWidth = 0.02f;    // エッジの幅 (資料)
        float randomIntensity = 0.0f;       // GPUによる乱数生成の強度 (資料)
        float linearWorkflowEnabled = 0.0f; // 1: Editor色をsRGBからリニアへ変換

        Vector3 dissolveEdgeColor = { 1.0f, 0.4f, 0.3f }; // エッジの色 (資料)
        float padding_m2 = 0.0f;            // 144バイト境界に合わせる
        
        Matrix4x4 projectionInverse;            // 深度復元用の逆行列 (資料)

        // --- Slime Fade ---
        float slimeFadeIntensity = 0.0f;    // スライムフェードの進捗 (0.0 - 1.0)
        float slimeDensity = 1.0f;          // スライムの密度（ノイズのスケール）
        float padding_s1 = 0.0f;
        float padding_s2 = 0.0f;
        Vector3 slimeColor = { 0.18f, 0.8f, 0.44f }; // スライムの色 (Emerald Green)
        float padding_s3 = 0.0f;

        // --- Iris Out ---
        float irisFadeIntensity = 0.0f;     // アイリスフェードの進捗 (0.0: 開, 1.0: 閉)
        float irisCenterX = 0.5f;           // 中心X
        float irisCenterY = 0.5f;           // 中心Y
        float padding_i1 = 0.0f;
    };

    // 初期化: 各パス用のリソースとPSOを生成
        // 画面効果用のメッシュ、RenderTexture、RootSignature、PipelineStateを作成します。
void Initialize(DirectXCommon* dxCommon);

    // 更新: 時間の進行などを処理
        // 時間依存の画面効果パラメータを更新します。
void Update(float deltaTime);

    // 描画実行: 指定したPSOとSRVを用いてパスを実行
        // 入力SRVを指定して、ポストエフェクトをフルスクリーン描画します。
void Draw(ID3D12GraphicsCommandList* commandList, uint32_t srvHandle, int psoIndex = 0);

    // 描画前準備: 指定したレンダーターゲットをセット
        // シーン描画先をポストエフェクト用RenderTextureへ切り替えます。
    void PreDrawScene(ID3D12GraphicsCommandList* commandList, int targetTexIndex = 0, bool clear = true);
    void PreDrawSceneWithDepth(ID3D12GraphicsCommandList* commandList, int targetTexIndex = kCameraPreviewTextureIndex, bool clear = true);
    // Camera Preview内の水・炎などが、Preview自身の色と深度を参照できる状態へ切り替えます。
    bool BeginCameraPreviewSpecialPass(ID3D12GraphicsCommandList* commandList, int targetTexIndex);
    void EndCameraPreviewSpecialPass(ID3D12GraphicsCommandList* commandList, int targetTexIndex);

    // リソースバリア管理
    void TransitionToSRV(ID3D12GraphicsCommandList* commandList, int texIndex);
    void TransitionToRTV(ID3D12GraphicsCommandList* commandList, int texIndex);

    // アクセッサ
    uint32_t GetSRVHandle(int texIndex = 0) const { return renderTextures_[texIndex].srvHandle; }
    ID3D12Resource* GetRenderTexture(int texIndex = 0) const { return renderTextures_[texIndex].resource.Get(); }
    uint32_t GetDepthSRVHandle(int texIndex) const { return renderTextures_[texIndex].depthSrvHandle; }
    uint32_t GetGrabSRVHandle(int texIndex) const { return renderTextures_[texIndex].grabSrvHandle; }
    Params* GetParams() { return paramsData_; }
    bool IsBloomEnabled() const { return bloomEnabled_; }
    void SetBloomEnabled(bool enabled) { bloomEnabled_ = enabled; }
    BloomQuality GetBloomQuality() const { return bloomQuality_; }
    void SetBloomQuality(BloomQuality quality);
    bool IsBloomActive() const;
    int GetBloomLevelCount() const;
    int GetExpectedPostEffectPassCount() const;
    float GetCameraPreviewResolutionScale() const { return cameraPreviewResolutionScale_; }
    int GetCameraPreviewWidth() const { return cameraPreviewWidth_; }
    int GetCameraPreviewHeight() const { return cameraPreviewHeight_; }
    void SetCameraPreviewResolutionScale(float scale);
    void SetLUTTexture(uint32_t srvHandle) { lutSrvHandle_ = srvHandle; }
    void SetNoiseTexture(uint32_t srvHandle) { noiseSrvHandle_ = srvHandle; }
        // 画面効果をニュートラルな初期状態へ戻します。
void ResetToNeutral();
    void ResizeRenderTextures(int width, int height);

private:
    void CreateMesh();
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateRenderTexture(int texIndex, int width, int height, DXGI_FORMAT format);
    void ResizeCameraPreviewTextures();
    void CreateConstBuffer();

private:
    // レンダーターゲット管理用
        // ポストエフェクトで使うRenderTexture、RTV、DSV、SRV、現在状態をまとめます。
struct RenderTexture {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
        Microsoft::WRL::ComPtr<ID3D12Resource> depthResource;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
        Microsoft::WRL::ComPtr<ID3D12Resource> grabResource;
        uint32_t srvHandle = 0;
        uint32_t depthSrvHandle = 0;
        uint32_t grabSrvHandle = 0;
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
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
    uint32_t noiseSrvHandle_ = 0;
    bool bloomEnabled_ = true;
    BloomQuality bloomQuality_ = BloomQuality::High;
    int renderWidth_ = 0;
    int renderHeight_ = 0;
    int cameraPreviewWidth_ = 0;
    int cameraPreviewHeight_ = 0;
    float cameraPreviewResolutionScale_ = 0.5f;
};
