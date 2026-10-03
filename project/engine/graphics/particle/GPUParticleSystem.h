#pragma once
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <cstddef>
#include <string>
#include "GPUParticleConfig.h"
#include <vector>
#include <Object3dCommon.h>

/// <summary>
/// 1種類のGPUパーティクルをCompute Shaderで更新し、描画する。
/// </summary>
class GPUParticleSystem {
public:
    // GPUとの共有レイアウト。フィールド順とアラインメントは対応するHLSLに合わせる。
    struct Particle {
        Vector3 position;
        float life;
        Vector3 velocity;
        float maxLife;
        Vector4 color;
        float scale;
        float rotation;
        float rotSpeed;
        uint32_t configIndex;

        Vector4 memBaseColor;
        Vector4 memMidColor;
        Vector4 memEndColor;
        float memBaseSize;
        float memMidSize;
        float memEndSize;
        float memColorMidTime;
        float memSizeMidTime;
        uint32_t memColorEaseType;
        uint32_t memSizeEaseType;
        float memColorIntensity;
        Vector3 memGravity;
        float memDrag;
        Vector3 memWind;
        float memTurbulence;
        uint32_t memFieldType;
        float memFieldStrength;
        float memFieldRadius;
        float memFieldFalloff;
        Vector3 memFieldPosition;
        float memFieldPadding;
    };

    /// <summary>Compute Shaderに渡す発生・更新設定。HLSLの定数バッファ配置と一致させる。</summary>
    struct alignas(256) CSConfig {
        float deltaTime;
        float time;
        uint32_t startIndex;
        uint32_t emitCount;

        Vector3 emitPos;
        float emitLife;
        Vector3 emitArea;
        float padding1;

        Vector3 emitVelocity;
        float velocityVariance;

        Vector4 baseColor;

        Vector3 gravity;
        float drag;

        Vector3 wind;
        float turbulence;

        float baseSize;
        float midSize;
        float endSize;
        float sizeMidTime;

        Vector4 midColor;

        float colorMidTime;
        float rotSpeedVariance;
        Vector2 padding2;

        Vector4 endColor;

        uint32_t shapeType;
        float shapeRadius;
        float shapeAngle;
        float padding3;

        uint32_t sizeEaseType;
        uint32_t colorEaseType;
        uint32_t meshVertexCount;
        uint32_t meshVertexStride;

        Matrix4x4 emitterWorldMatrix;
        Matrix4x4 viewProj;
        Matrix4x4 inverseViewProj;
        Vector2 screenSize;

        uint32_t enableCollision;
        float restitution;
        float colorIntensity;
        uint32_t currentConfigIndex;
        uint32_t maxParticles;
        float initialAge;

        uint32_t fieldType;
        float fieldStrength;
        float fieldRadius;
        float fieldFalloff;
        Vector3 fieldPosition;
        float fieldPadding;
    };

    /// <summary>次回GPU更新で処理する粒子発生要求とメッシュ表面参照。</summary>
    struct EmitRequest {
        CSConfig config;
        ID3D12Resource* vb;
        uint32_t vCount;
        uint32_t vStride;
        uint32_t boneSrv;
    };

    /// <summary>描画シェーダーに渡すカメラとSprite表示設定。</summary>
    struct alignas(256) CameraData {
        Matrix4x4 viewProj;
        Matrix4x4 billboardMatrix;
        Matrix4x4 projection;
        float softParticleFade;
        int blendMode;
        Vector2 screenSize;
        uint32_t spriteSheetColumns;
        uint32_t spriteSheetRows;
        uint32_t spriteSheetFrameCount;
        float spriteSheetFps;
        uint32_t spriteSheetLoop;
        uint32_t spriteSheetRandomStart;
        uint32_t alignToVelocity;
        float velocityStretch;
        uint32_t particleType;
        float trailLength;
        uint32_t receiveLighting;
        float lightingStrength;
        Vector3 lightDirection;
        float cameraPadding0;
        Vector3 lightColor;
        float cameraPadding1;
    };

    GPUParticleSystem() = default;
    ~GPUParticleSystem() = default;

    /// <summary>粒子バッファとCompute/描画パイプラインを初期化する。</summary>
    /// <param name="dxCommon">有効期間中に保持するDirectXの管理インスタンス。</param>
    /// <param name="maxParticles">このシステムが確保する粒子数。</param>
    void Initialize(DirectXCommon* dxCommon, uint32_t maxParticles = kMaxParticles);

    /// <summary>発生要求を反映し、粒子の寿命・移動・色をGPUで更新する。</summary>
    /// <param name="deltaTime">今回の更新時間（秒）。</param>
    void Update(float deltaTime);

    /// <summary>更新済み粒子を描画する。</summary>
    /// <param name="commandList">描画先のコマンドリスト。</param>
    /// <param name="viewMatrix">現在のビュー行列。</param>
    /// <param name="projectionMatrix">現在の射影行列。</param>
    /// <param name="dummyTex">テクスチャ未指定時に使用するSRV。</param>
    /// <param name="depthSrvHandle">ソフトパーティクル用の深度SRV。</param>
    void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t dummyTex = 0, uint32_t depthSrvHandle = 0);

    /// <summary>プリセット設定から次回更新分の粒子発生を予約する。</summary>
    void EmitFromConfig(const GPUParticleConfig& config);

    /// <summary>以後の描画に使用するテクスチャを切り替える。</summary>
    void SetCurrentTexture(const std::string& path);

    /// <summary>メッシュ表面から発生させる際の頂点バッファを設定する。</summary>
    /// <remarks>頂点バッファは所有しないため、描画完了まで呼び出し側で保持する。</remarks>
    void SetEmitterMesh(ID3D12Resource* vb, uint32_t vCount, uint32_t vStride, uint32_t boneSrvIndex) {
        emitterVertexBuffer_ = vb;
        emitterVertexCount_ = vCount;
        emitterVertexStride_ = vStride;
        emitterBoneSrvIndex_ = boneSrvIndex;
    }

    /// <summary>シミュレーションの時間倍率を指定する。</summary>
    void SetTimeScale(float scale) { timeScale_ = scale; }

    /// <summary>次回更新時に粒子を発生できる状態へ戻す。</summary>
    void RequestWarmup() { warmupRequested_ = true; lastEmitTimer_ = 0.0f; }

    /// <summary>GPUバッファを保持したまま、次回描画時に粒子状態を初期化する。</summary>
    void RequestSimulationReset();

    /// <summary>シーン切り替え時に描画を止め、次の発生要求時にGPU状態を初期化する。</summary>
    void ResetForSceneTransition();

    /// <summary>発生要求または生存し得る粒子があるかを返す。</summary>
    bool IsActive() const { return warmupRequested_ || lastEmitTimer_ <= activeLifetimeWindow_; }

    /// <summary>歪み描画のためにシーンカラーのコピーが必要かを返す。</summary>
    bool RequiresSceneColorCopy() const { return IsActive() && blendModeIndex_ == 2; }

    /// <summary>確保済み粒子の上限を返す。</summary>
    uint32_t GetParticleCapacity() const { return maxParticles_; }

    /// <summary>このシステムが保持するGPUリソースの概算バイト数を返す。</summary>
    size_t GetEstimatedMemoryBytes() const;

    // 既定値と下限。実際の確保数はプリセット設定から決められる。
    static constexpr uint32_t kMaxParticles = 10000;
    static constexpr uint32_t kMinParticles = 256;
    static const uint32_t kMaxEmitRequests = 256;
private:
    // 初期化時に一度だけ作るGPUリソースとパイプライン。
    void CreateBuffer();
    void CreateComputePipeline();
    void CreateGraphicsPipeline();

    DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer_;
    uint32_t uavIndex_ = 0;
    uint32_t freeListIndexUav_ = 0;
    uint32_t freeListUav_ = 0;
    uint32_t srvIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> aliveParticleIndexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indirectDrawArgumentBuffer_;
    uint32_t aliveParticleIndexUav_ = 0;
    uint32_t indirectDrawArgumentUav_ = 0;
    uint32_t aliveParticleIndexSrv_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> configBuffer_;
    CSConfig* configData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraBuffer_;
    CameraData* cameraData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> dummyVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> dummyBoneBuffer_;
    uint32_t dummyBoneSrvIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateInit_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateResetDrawArguments_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateUpdate_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateEmit_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> graphicsRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateAdd_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateAlpha_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateDistortion_;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> indirectDrawCommandSignature_;

    std::vector<EmitRequest> emitRequests_;
    CSConfig lastConfig_ = {};
    bool isInitialized_ = false;

    float totalTime_ = 0.0f;
    float frameDeltaTime_ = 0.0f;
    float timeScale_ = 1.0f;

    // 軽量化用: 最後にEmitしてからどれくらい経ったか
    float lastEmitTimer_ = 0.0f;
    bool warmupRequested_ = false;
    float activeLifetimeWindow_ = 2.0f;
    uint32_t maxParticles_ = kMaxParticles;

    uint32_t emitCountThisFrame_ = 0;
    float softParticleFade_ = 5.0f;
    uint32_t blendModeIndex_ = 0;
    uint32_t spriteSheetColumns_ = 1;
    uint32_t spriteSheetRows_ = 1;
    uint32_t spriteSheetFrameCount_ = 1;
    float spriteSheetFps_ = 0.0f;
    uint32_t spriteSheetLoop_ = 0;
    uint32_t spriteSheetRandomStart_ = 0;
    uint32_t alignToVelocity_ = 0;
    float velocityStretch_ = 0.0f;
    uint32_t particleType_ = 0;
    float trailLength_ = 0.15f;
    uint32_t receiveLighting_ = 0;
    float lightingStrength_ = 1.0f;
    Vector3 lightDirection_ = { -0.4f, -1.0f, 0.3f };
    Vector3 lightColor_ = { 1.0f, 1.0f, 1.0f };

    uint32_t currentTextureHandle_ = 0;

    ID3D12Resource* emitterVertexBuffer_ = nullptr;
    uint32_t emitterVertexCount_ = 0;
    uint32_t emitterVertexStride_ = 0;
    uint32_t emitterBoneSrvIndex_ = 0;
};
