#pragma once
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <string>
#include "GPUParticleConfig.h"
#include <vector>
#include <Object3dCommon.h>

/// <summary>
/// 個別のエフェクト（炎、魔法など）を担当する独立した部隊
/// </summary>
class GPUParticleSystem {
public:
    // HLSLと完全に一致させた構造体
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
    };

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
        Vector2 padding_col;
    };

    struct EmitRequest {
        CSConfig config;
        ID3D12Resource* vb;
        uint32_t vCount;
        uint32_t vStride;
        uint32_t boneSrv;
    };

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
        Vector2 spriteSheetPadding;
    };

    GPUParticleSystem() = default;
    ~GPUParticleSystem() = default;

    void Initialize(DirectXCommon* dxCommon);
    void Update(float deltaTime);
    void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t dummyTex = 0, uint32_t depthSrvHandle = 0);

    void EmitFromConfig(const GPUParticleConfig& config);
    void SetCurrentTexture(const std::string& path);

    void SetEmitterMesh(ID3D12Resource* vb, uint32_t vCount, uint32_t vStride, uint32_t boneSrvIndex) {
        emitterVertexBuffer_ = vb;
        emitterVertexCount_ = vCount;
        emitterVertexStride_ = vStride;
        emitterBoneSrvIndex_ = boneSrvIndex;
    }

    void SetTimeScale(float scale) { timeScale_ = scale; }
    void RequestWarmup() { warmupRequested_ = true; lastEmitTimer_ = 0.0f; }
    bool IsActive() const { return warmupRequested_ || lastEmitTimer_ <= 2.0f; }

    // ★重要: 部隊ごとに作られるので、10万から1万に減らす！
    static const uint32_t kMaxParticles = 10000;
    static const uint32_t kMaxEmitRequests = 256;
private:
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

    Microsoft::WRL::ComPtr<ID3D12Resource> configBuffer_;
    CSConfig* configData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraBuffer_;
    CameraData* cameraData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> dummyVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> dummyBoneBuffer_;
    uint32_t dummyBoneSrvIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateInit_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateUpdate_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineStateEmit_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> graphicsRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateAdd_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateAlpha_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateDistortion_;

    std::vector<EmitRequest> emitRequests_;
    CSConfig lastConfig_ = {};
    bool isInitialized_ = false;

    float totalTime_ = 0.0f;
    float frameDeltaTime_ = 0.0f;
    float timeScale_ = 1.0f;

    // 軽量化用: 最後にEmitしてからどれくらい経ったか
    float lastEmitTimer_ = 0.0f;
    bool warmupRequested_ = false;
    const float kIdleKillTime = 5.0f; // 5秒間何も出なかったら一旦止める

    uint32_t emitCountThisFrame_ = 0;
    float softParticleFade_ = 5.0f;
    uint32_t blendModeIndex_ = 0;
    uint32_t spriteSheetColumns_ = 1;
    uint32_t spriteSheetRows_ = 1;
    uint32_t spriteSheetFrameCount_ = 1;
    float spriteSheetFps_ = 0.0f;
    uint32_t spriteSheetLoop_ = 0;
    uint32_t spriteSheetRandomStart_ = 0;

    uint32_t currentTextureHandle_ = 0;

    ID3D12Resource* emitterVertexBuffer_ = nullptr;
    uint32_t emitterVertexCount_ = 0;
    uint32_t emitterVertexStride_ = 0;
    uint32_t emitterBoneSrvIndex_ = 0;
};
