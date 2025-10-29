#pragma once
#include "engine/base/Math.h"
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include <random>

class ParticleCommon;

/// <summary>
/// パーティクルシステム
/// </summary>
class ParticleSystem {
private:
    struct VertexData {
        Vector3 position;
        Vector4 color;
    };
    struct Particle {
        Vector3 position;
        Vector3 velocity;
        Vector4 color;
        float lifeTime;
        float currentTime;
    };

    // 頂点シェーダーに送る、各パーティクルの情報
    struct ParticleForGPU {
        Vector4 color;
        Matrix4x4 world;
    };
    // カメラの行列
    struct TransformationMatrix {
        Matrix4x4 viewProjection;
    };

public:
    void Initialize(ParticleCommon* common, const std::string& texturePath);
    void Update();
    void Draw();
    void SpawnParticles(const Vector3& position, int count,
        float initialSpeed = 2.0f, // 基本速度
        const Vector3* direction = nullptr, // 方向指定 (nullptrならランダム)
        float spreadAngle = 0.0f, // 方向のばらつき角度 (ラジアン)
        Vector4 initialColor = { 1,1,1,1 }, // 初期色
        float lifeTimeMin = 1.0f, float lifeTimeMax = 3.0f);
    void Clear();
private:
    void CreateResources();
    Particle CreateParticle(const Vector3& position, float speed, const Vector3& dir,
        const Vector4& color, float life);
private:
    static const int kMaxParticles = 1024;
    ParticleCommon* common_ = nullptr;
    uint32_t textureHandle_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    // インデックスバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    // インスタンスデータ用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    D3D12_VERTEX_BUFFER_VIEW instancingBufferView_{};
    ParticleForGPU* instancingData_ = nullptr;

    // カメラ行列用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> matrixResource_;
    TransformationMatrix* matrixData_ = nullptr;

    std::vector<Particle> particles_;
    std::mt19937 randomEngine_;
    UINT particleCount_ = 0; // 現在のパーティクル数
};