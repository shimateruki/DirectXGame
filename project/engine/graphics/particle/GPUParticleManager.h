#pragma once
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <wrl.h>

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
    };

    // 圧倒的暴力：10万個のパーティクル
    static const uint32_t kMaxParticles = 100000;

public:
    static GPUParticleManager* GetInstance();

    void Initialize(DirectXCommon* dxCommon);

    // 毎フレームの計算 (Compute Shaderの実行)
    void Update(float deltaTime);

    void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t textureHandle);
    void Emit(const Vector3& pos, const Vector3& area, const Vector3& velocity, uint32_t count, float life, float variance, const Vector4& color);
    void SetEnvironmentParams(const Vector3& gravity, float drag, const Vector3& wind, float turbulence) {
        envGravity_ = gravity;
        envDrag_ = drag;
        envWind_ = wind;
        envTurbulence_ = turbulence;
    }
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
    };

    //  カメラ用定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraBuffer_;
    CameraData* cameraData_ = nullptr;
    uint32_t emitCountThisFrame_ = 0;
    Vector3 envGravity_ = { 0.0f, -9.8f, 0.0f };
    float envDrag_ = 0.98f;
    Vector3 envWind_ = { 0.0f, 0.0f, 0.0f };
    float envTurbulence_ = 0.0f;

};