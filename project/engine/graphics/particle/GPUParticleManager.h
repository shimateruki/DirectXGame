#pragma once
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>
#include <map>        
#include <string>      
#include "GPUParticleConfig.h"

/// <summary>
/// Compute Shaderを用いてGPU上で10万個のパーティクルを制御する最強のマネージャー
/// </summary>
class GPUParticleManager {
public:
    // ★ HLSL側と全く同じ構造にする
    struct Particle {
        Vector3 position;
        float life;
        Vector3 velocity;
        float maxLife;
        Vector4 color;
        float scale;       
        float rotation;  
        float rotSpeed;  
        float padding;   

    };
    struct CSConfig {
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
        float padding2[2]; 
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
    };
    enum class BlendMode {
        kAdd,   // 加算合成（光る魔法や炎）
        kAlpha, // 半透明合成（霧や煙、砂埃）
        kDistortion,
    };
    // 圧倒的暴力：10万個のパーティクル
    static const uint32_t kMaxParticles = 100000;

public:
    static GPUParticleManager* GetInstance();

    void Initialize(DirectXCommon* dxCommon);

    // 毎フレームの計算 (Compute Shaderの実行)
    void Update(float deltaTime);

    void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t textureHandle, uint32_t depthSrvHandle = 0);
    void Emit(const Vector3& pos, const Vector3& area, const Vector3& velocity, uint32_t count, float life, float variance, const Vector4& color);
    void SetEnvironmentParams(const Vector3& gravity, float drag, const Vector3& wind, float turbulence) {
        envGravity_ = gravity;
        envDrag_ = drag;
        envWind_ = wind;
        envTurbulence_ = turbulence;
    }

    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }
    void SetSizeParams(float baseSize, float endSize, float rotSpeed) {
        baseSize_ = baseSize;
        endSize_ = endSize;
        rotSpeed_ = rotSpeed;
    }
    void SetEmitterMesh(ID3D12Resource* vertexBuffer, uint32_t vertexCount, uint32_t vertexStride, uint32_t boneSrvIndex) {
        emitterVertexBuffer_ = vertexBuffer;
        emitterVertexCount_ = vertexCount;
        emitterVertexStride_ = vertexStride;
        emitterBoneSrvIndex_ = boneSrvIndex;
    }
    void SetEndColor(const Vector4& endColor) { endColor_ = endColor; }
    // 起動時に全JSONを読み込んでメモリにキャッシュする
    void LoadAllPresets(const std::string& directoryPath = "Resources/json/gpu_particles/");

    // ゲーム側用：名前と座標を渡すだけで即座に再生！
    void Emit(const std::string& presetName, const Vector3& position, const Matrix4x4& emitterWorldMatrix);

    // エディタ側用：コンフィグデータを直接渡して再生！
    void EmitFromConfig(const GPUParticleConfig& config);
private:
    GPUParticleManager() = default;
    ~GPUParticleManager() = default;
    GPUParticleManager(const GPUParticleManager&) = delete;
    GPUParticleManager& operator=(const GPUParticleManager&) = delete;

    void CreateBuffer();
    void CreateComputePipeline();
    void CreateGraphicsPipeline();

private:
    DirectXCommon* dxCommon_ = nullptr;

    // --- Compute Pipeline 関連 ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> computePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> graphicsRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateAdd_;   // 加算用
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateAlpha_; // 霧用
    // --- GPUメモリ (UAV: 読み書き可能バッファ) ---
    Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer_;

    // ディスクリプタヒープのインデックス (SRVManager等で管理)
    uint32_t uavIndex_ = 0;
    uint32_t srvIndex_ = 0;

    // --- 定数バッファ (時間送信用) ---
    Microsoft::WRL::ComPtr<ID3D12Resource> configBuffer_;
    CSConfig* configData_ = nullptr;

    float totalTime_ = 0.0f;;
    uint32_t currentParticleIndex_ = 0;
    //  カメラに送るデータ構造体
    struct CameraData {
        Matrix4x4 viewProj;
        Matrix4x4 billboardMatrix;
        Matrix4x4 projection;
        float softParticleFade;
        int blendMode;       
        Vector2 screenSize;  
    };

    //  カメラ用定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraBuffer_;
    CameraData* cameraData_ = nullptr;
    uint32_t emitCountThisFrame_ = 0;
    Vector3 envGravity_ = { 0.0f, -9.8f, 0.0f };
    float envDrag_ = 0.98f;
    Vector3 envWind_ = { 0.0f, 0.0f, 0.0f };
    float envTurbulence_ = 0.0f;
    float softParticleFade_ = 5.0f;
    // 現在のブレンドモード
    BlendMode blendMode_ = BlendMode::kAdd;
    float baseSize_ = 1.0f;
    float endSize_ = 1.0f;
    float rotSpeed_ = 1.0f; 
    Vector4 endColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    std::map<std::string, GPUParticleConfig> presets_;
    ID3D12Resource* emitterVertexBuffer_ = nullptr;
    uint32_t emitterVertexCount_ = 0;
    uint32_t emitterVertexStride_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> dummyVertexBuffer_; // メッシュが無い時用のダミー
    Microsoft::WRL::ComPtr<ID3D12Resource> dummyBoneBuffer_;
    uint32_t dummyBoneSrvIndex_ = 0;
    uint32_t emitterBoneSrvIndex_ = 0;
};