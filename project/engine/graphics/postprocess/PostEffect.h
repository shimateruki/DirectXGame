#pragma once
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <cstdint>
#include <vector>
#include <string>

class DirectXCommon;

/// <summary>
/// 描画済み画面へブルーム、色調補正、輪郭などの画面効果を適用する。
/// </summary>
class PostEffect {
public:
    static constexpr int kCameraPreviewTextureIndex = 6;
    static constexpr int kCinematicCameraPreviewTextureIndex = 7;

    /// <summary>画面効果に使う全画面描画パイプラインの種類。</summary>
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

    /// <summary>共有のポストエフェクト管理インスタンスを返す。</summary>
    static PostEffect* GetInstance() {
        static PostEffect instance;
        return &instance;
    }

    /// <summary>ポストエフェクトシェーダーへ渡す定数バッファの配置。</summary>
    /// <remarks>フィールド順とアラインメントをHLSL側に合わせる。</remarks>
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
        float vignettePower = 1.0f;         // 周辺減光の丸み
        float time = 0.0f;                  // 時間経過（ノイズアニメ用）


        // --- Radial Blur ---
        float radialCenterX = 0.5f;         // 放射ブラー中心X
        float radialCenterY = 0.5f;         // 放射ブラー中心Y
        float radialIntensity = 0.0f;       // 放射ブラー強度
        int32_t radialBlurSamples = 1;      // サンプリング数

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
        int32_t boxFilterSize = 0;          // 0:Off, 1:3x3, 2:5x5...
        int32_t gaussianFilterSize = 0;     // 0:Off, 1:3x3, 2:5x5...
        float gaussianSigma = 1.0f;         // ガウス関数のシグマ
        float luminanceOutlineIntensity = 0.0f; // 輝度ベースのアウトライン
        float depthOutlineIntensity = 0.0f;     // 深度ベースのアウトライン
        
        // --- Dissolve & Random ---
        float dissolveThreshold = 0.0f;     // ディゾルブのしきい値
        float dissolveEdgeWidth = 0.02f;    // エッジの幅
        float randomIntensity = 0.0f;       // GPUによる乱数生成の強度
        float linearWorkflowEnabled = 0.0f; // 1: Editor色をsRGBからリニアへ変換

        Vector3 dissolveEdgeColor = { 1.0f, 0.4f, 0.3f }; // エッジの色
        float padding_m2 = 0.0f;            // 144バイト境界に合わせる
        
        Matrix4x4 projectionInverse;            // 深度復元用の逆行列

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

    /// <summary>画面効果の描画リソースとパイプラインを初期化する。</summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>時間依存の画面効果パラメータを更新する。</summary>
    void Update(float deltaTime);

    /// <summary>入力SRVを指定のパイプラインで全画面描画する。</summary>
    void Draw(ID3D12GraphicsCommandList* commandList, uint32_t srvHandle, int psoIndex = 0);

    /// <summary>シーンの描画先をポストエフェクト用テクスチャへ切り替える。</summary>
    void PreDrawScene(ID3D12GraphicsCommandList* commandList, int targetTexIndex = 0, bool clear = true);

    /// <summary>深度も含めてシーン描画先を切り替える。</summary>
    void PreDrawSceneWithDepth(ID3D12GraphicsCommandList* commandList, int targetTexIndex = kCameraPreviewTextureIndex, bool clear = true);

    /// <summary>Camera Preview内の特殊描画が自身の色と深度を参照できる状態へ切り替える。</summary>
    bool BeginCameraPreviewSpecialPass(ID3D12GraphicsCommandList* commandList, int targetTexIndex);

    /// <summary>Camera Previewの特殊描画を終了する。</summary>
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

    /// <summary>ブルームの縮小段数を決める品質を設定する。</summary>
    void SetBloomQuality(BloomQuality quality);

    /// <summary>現在のパラメータでブルーム描画が必要か返す。</summary>
    bool IsBloomActive() const;

    /// <summary>現在の品質に対応するブルームの縮小段数を返す。</summary>
    int GetBloomLevelCount() const;

    /// <summary>現在の設定から見込まれる全画面描画パス数を返す。</summary>
    int GetExpectedPostEffectPassCount() const;
    float GetCameraPreviewResolutionScale() const { return cameraPreviewResolutionScale_; }
    int GetCameraPreviewWidth() const { return cameraPreviewWidth_; }
    int GetCameraPreviewHeight() const { return cameraPreviewHeight_; }

    /// <summary>カメラプレビューの内部解像度倍率を設定する。</summary>
    void SetCameraPreviewResolutionScale(float scale);
    void SetLUTTexture(uint32_t srvHandle) { lutSrvHandle_ = srvHandle; }

    /// <summary>カラーグレーディング用LUTをパスから読み込む。</summary>
    bool SetLUTTexturePath(const std::string& path);
    const std::string& GetLUTTexturePath() const { return lutTexturePath_; }

    void SetNoiseTexture(uint32_t srvHandle) { noiseSrvHandle_ = srvHandle; }

    /// <summary>画面効果パラメータをニュートラルな状態へ戻す。</summary>
    void ResetToNeutral();

    /// <summary>描画先テクスチャを指定サイズに作り直す。</summary>
    void ResizeRenderTextures(int width, int height);

private:
    // 全画面矩形、共通ルートシグネチャ、画面効果の各パイプラインを作る。
    void CreateMesh();
    void CreateRootSignature();
    void CreatePipelineState();

    // 描画先テクスチャ、カメラプレビューと画面効果定数を準備・再生成する。
    void CreateRenderTexture(int texIndex, int width, int height, DXGI_FORMAT format);
    void ResizeCameraPreviewTextures();
    void CreateConstBuffer();

private:
    // 各パスの色・深度・Grabリソースと現在のバリア状態をひとまとまりで管理する。
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

    /// <summary>全画面矩形一頂点分の位置とUV。</summary>
    struct VertexPosUv {
        Vector3 pos;
        Vector2 uv;
    };

    // カラーグレーディング用LUTのSRVハンドル
    uint32_t lutSrvHandle_ = 0;
    std::string lutTexturePath_ = "Resources/texture/lut/soft_adventure_lut.png";
    uint32_t noiseSrvHandle_ = 0;
    bool bloomEnabled_ = true;
    BloomQuality bloomQuality_ = BloomQuality::High;
    int renderWidth_ = 0;
    int renderHeight_ = 0;
    int cameraPreviewWidth_ = 0;
    int cameraPreviewHeight_ = 0;
    float cameraPreviewResolutionScale_ = 0.5f;
};
