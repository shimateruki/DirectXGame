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
        Matrix4x4 world;
        Vector4 color;
    };
    // カメラの行列
    struct TransformationMatrix {
        Matrix4x4 viewProjection;
    };

public:
    void Initialize(ParticleCommon* common);
    void Update();
    void Draw();
    void SpawnParticles(const Vector3& position, int count);

private:
    void CreateResources();
    Particle CreateParticle(const Vector3& position);

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