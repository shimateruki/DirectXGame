#pragma once
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <cstddef>
#include <string>
#include "GpuParticleConfig.h"
#include <vector>
#include <Object3DCommon.h>

// 担当：一つのGPU粒子システムの発生要求・Compute更新・描画を処理する。
// 関連：GpuParticleManager（管理）、GpuParticleConfig（設定）、DirectXCommon（GPU基盤）
// 主な入口：Initialize / EmitFromConfig / Update / Draw / ResetForSceneTransition
class GpuParticleSystem {
public:
    // GPUとの共有レイアウト。フィールド順とアラインメントは対応するHLSLに合わせる。
    // 関連：GpuParticleSystem（この型を使う機能の本体）
    struct Particle {
        engine::Vector3 position;
        float life;
        engine::Vector3 velocity;
        float maxLife;
        engine::Vector4 color;
        float scale;
        float rotation;
        float rotSpeed;
        uint32_t configIndex; // 発生時に割り当てた設定スロット番号。

        engine::Vector4 memBaseColor;
        engine::Vector4 memMidColor;
        engine::Vector4 memEndColor;

        float memBaseSize;
        float memMidSize;
        float memEndSize;

        float memColorMidTime;
        float memSizeMidTime;

        uint32_t memColorEaseType;
        uint32_t memSizeEaseType;

        float memColorIntensity;

        engine::Vector3 memGravity;
        float memDrag;
        engine::Vector3 memWind;
        float memTurbulence;

        uint32_t memFieldType;
        float memFieldStrength;
        float memFieldRadius;
        float memFieldFalloff;
        engine::Vector3 memFieldPosition;
        float memFieldPadding;
    };

    /// <summary>Compute Shaderに渡す発生・更新設定。HLSLの定数バッファ配置と一致させる。</summary>
    // 関連：GpuParticleSystem（この型を使う機能の本体）
    struct alignas(256) CSConfig {
        float deltaTime; // GPUへ渡す一更新の秒数。
        float time;
        uint32_t startIndex;
        uint32_t emitCount;

        engine::Vector3 emitPos;
        float emitLife;
        engine::Vector3 emitArea;
        float padding1;

        engine::Vector3 emitVelocity;
        float velocityVariance;

        engine::Vector4 baseColor;

        engine::Vector3 gravity;
        float drag;

        engine::Vector3 wind;
        float turbulence;

        float baseSize;
        float midSize;
        float endSize;
        float sizeMidTime;

        engine::Vector4 midColor;

        float colorMidTime;
        float rotSpeedVariance;
        engine::Vector2 padding2;

        engine::Vector4 endColor;

        uint32_t shapeType;
        float shapeRadius;
        float shapeAngle;
        float padding3;

        uint32_t sizeEaseType;
        uint32_t colorEaseType;
        uint32_t meshVertexCount;
        uint32_t meshVertexStride;

        engine::Matrix4x4 emitterWorldMatrix;
        engine::Matrix4x4 viewProj;
        engine::Matrix4x4 inverseViewProj;
        engine::Vector2 screenSize;

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
        engine::Vector3 fieldPosition;
        float fieldPadding;
    };

    /// <summary>次回GPU更新で処理する粒子発生要求とメッシュ表面参照。</summary>
    // 関連：GpuParticleSystem（この型を使う機能の本体）
    struct EmitRequest {
        CSConfig config;
        ID3D12Resource* vb;
        uint32_t vCount;
        uint32_t vStride;
        uint32_t boneSrv;
    };

    /// <summary>描画シェーダーに渡すカメラとSprite表示設定。</summary>
    // 関連：GpuParticleSystem（この型を使う機能の本体）
    struct alignas(256) CameraData {
        engine::Matrix4x4 viewProj;
        engine::Matrix4x4 billboardMatrix;
        engine::Matrix4x4 projection;

        float softParticleFade;
        int blendMode;
        engine::Vector2 screenSize;

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
        engine::Vector3 lightDirection;
        float cameraPadding0;
        engine::Vector3 lightColor;
        float cameraPadding1;
    };

    GpuParticleSystem() = default;

    ~GpuParticleSystem() = default;

    /// <summary>粒子バッファとCompute/描画パイプラインを初期化する。</summary>
    /// <param name="dxCommon">有効期間中に保持するDirectXの管理インスタンス。</param>
    /// <param name="maxParticles">このシステムが確保する粒子数。</param>
    void Initialize(DirectXCommon* dxCommon, uint32_t maxParticles = kMaxParticles);

    /// <summary>時間倍率を掛けた経過秒を蓄積する。Compute命令はまだ発行しない。</summary>
    /// <param name="deltaTime">倍率適用前の経過秒。通常は0以上。</param>
    /// <remarks>Drawまでの複数回分を加算し、次のDrawで一回のGPU更新として消費する。</remarks>
    void Update(float deltaTime);

    /// <summary>蓄積時間のCompute更新、発生要求の処理、生存粒子のIndirect Drawを記録する。</summary>
    /// <param name="commandList">描画先のコマンドリスト。</param>
    /// <param name="viewMatrix">現在のビュー行列。</param>
    /// <param name="projectionMatrix">現在の射影行列。</param>
    /// <param name="dummyTex">テクスチャ未指定時に使用するSRV。</param>
    /// <param name="depthSrvHandle">現カメラの深度SRV。画面深度衝突、遮蔽、Soft Particleに使う。</param>
    /// <remarks>コマンド記録後に発生要求と蓄積秒を消費する。Updateだけの反復はGPUの固定刻み再計算ではない。</remarks>
    void Draw(ID3D12GraphicsCommandList* commandList, const engine::Matrix4x4& viewMatrix, const engine::Matrix4x4& projectionMatrix, uint32_t dummyTex = 0, uint32_t depthSrvHandle = 0);

    /// <summary>設定をコピーし、次のDrawで発生させる。要求が256件に達した場合は追加しない。</summary>
    /// <remarks>頂点バッファは借用。描画設定はSystemで共有するため、Managerの共有キーが一致する設定を渡す。</remarks>
    void EmitFromConfig(const GpuParticleConfig& config);

    /// <summary>以後の描画に使用するテクスチャを切り替える。</summary>
    void SetCurrentTexture(const std::string& path);

    /// <summary>メッシュ表面から発生させる際の頂点バッファを設定する。</summary>
    /// <remarks>頂点バッファは所有しないため、描画完了まで呼び出し側で保持する。</remarks>
    void SetEmitterMesh(ID3D12Resource* vb, uint32_t vCount, uint32_t vStride, uint32_t boneSrvIndex) {
        emitterVertexBuffer_ = vb;
        emitterVertexCount_ = vCount; // モデル表面発生に使う頂点数。
        emitterVertexStride_ = vStride;
        emitterBoneSrvIndex_ = boneSrvIndex; // 発生元モデルのボーン行列SRV。
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

    // GPU更新に使うCompute Shaderとパイプラインを作る。
    void CreateComputePipeline();

    // 粒子描画用のルートシグネチャとパイプラインを作る。
    void CreateGraphicsPipeline();

    // コマンド発行とリソース確保に使用するDirectX基盤への非所有参照。
    DirectXCommon* dxCommon_ = nullptr;

    // 全粒子の構造化バッファと、Compute書込・描画読込用のDescriptor番号。
    Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer_;
    uint32_t uavIndex_ = 0;
    uint32_t freeListIndexUav_ = 0;
    uint32_t freeListUav_ = 0;
    uint32_t srvIndex_ = 0;

    // 空き粒子番号、現在生存している粒子番号、間接描画引数のGPUバッファ。
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> aliveParticleIndexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indirectDrawArgumentBuffer_;
    uint32_t aliveParticleIndexUav_ = 0;
    uint32_t indirectDrawArgumentUav_ = 0;
    uint32_t aliveParticleIndexSrv_ = 0;

    // 発生・更新条件をCompute Shaderへ渡す定数バッファとマップ済み領域。
    Microsoft::WRL::ComPtr<ID3D12Resource> configBuffer_;
    CSConfig* configData_ = nullptr;

    // GPU描画に必要な視点情報のバッファとマップ済み領域。
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraBuffer_;
    CameraData* cameraData_ = nullptr; // マップ済みカメラ定数バッファへの参照。

    // 頂点・ボーン入力が必要な描画経路へ渡す代替バッファ。
    Microsoft::WRL::ComPtr<ID3D12Resource> dummyVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> dummyBoneBuffer_;
    uint32_t dummyBoneSrvIndex_ = 0;

    // 初期化、描画引数のリセット、粒子更新、発生のComputeパイプライン。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateInit_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateResetDrawArguments_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateUpdate_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateEmit_;

    // 加算・アルファ・歪みの描画パイプラインと間接描画コマンド形式。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> graphicsRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateAdd_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateAlpha_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateDistortion_;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> indirectDrawCommandSignature_;

    // CPUがこのフレームに受け付けた発生要求。描画時にGPUへ渡す。
    std::vector<EmitRequest> emitRequests_;

    // 直近の設定と、GPUバッファの初期化済み状態。
    CSConfig lastConfig_ = {};
    bool isInitialized_ = false;

    // シミュレーションの総時刻、1フレームの秒数、再生倍率。
    float totalTime_ = 0.0f;
    float frameDeltaTime_ = 0.0f; // このフレームで進めるシミュレーション秒数。
    float timeScale_ = 1.0f; // シミュレーションの時間倍率。

    // 最後の発生からの秒数と、新規発生を要求した状態。
    // 寿命の残存期間を過ぎたら更新・描画を省略する。
    float lastEmitTimer_ = 0.0f;
    bool warmupRequested_ = false;
    float activeLifetimeWindow_ = 2.0f; // 稼働判定に使う発生後の保持秒数。
    uint32_t maxParticles_ = kMaxParticles;

    // このフレームの発生数と、粒子の描画・アニメーション設定。
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

    // ライティング有効時に粒子へ適用する光の向きと色。
    engine::Vector3 lightDirection_ = { -0.4f, -1.0f, 0.3f };
    engine::Vector3 lightColor_ = { 1.0f, 1.0f, 1.0f };

    // 現在描画に使う粒子画像のTextureManagerハンドル。
    uint32_t currentTextureHandle_ = 0;

    // メッシュ表面から発生させるときの頂点情報。元バッファは借用する。
    ID3D12Resource* emitterVertexBuffer_ = nullptr;
    uint32_t emitterVertexCount_ = 0; // モデル表面発生に使う頂点数。
    uint32_t emitterVertexStride_ = 0;
    uint32_t emitterBoneSrvIndex_ = 0; // 発生元モデルのボーン行列SRV。
};
