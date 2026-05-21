#pragma once
#include "engine/utility/math/Math.h"
#include "ParticleCommon.h" 
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include <random>
#include <string>

enum class EmitterType {
    Box,    // 従来の四角形
    Sphere, // 球体（全方向爆発）
    Cone,   // 円錐（ジェット噴射）
};
/// <summary>
/// パーティクルシステム (GPUインスタンシング対応)
/// </summary>
class ParticleSystem {
private:

    struct Particle {
        Vector3 position;
        Vector3 velocity;
        float lifeTime;       // 生存期間
        float currentTime;    // 現在の時間

        // --- 補間用のデータ ---
        Vector4 startColor;
        Vector4 endColor;
        float startSize;
        float endSize;
        float rotation;       // 今の角度 (ラジアン)
        float rotationSpeed;  // 回転スピード (ラジアン/秒)
        Vector3 acceleration;

        float hdrIntensity;

    };

    struct ParticleForGPU {
        Vector4 color;
        Matrix4x4 world;
    };
    // カメラの行列
    struct TransformationMatrix {
        Matrix4x4 viewProjection;
    };



public:
    // エディタで編集したい全パラメータ
    struct EmitterParams {
        Vector3 spawnPosition = { 0.0f, 0.0f, 0.0f };      // 発生座標
        Vector3 spawnArea = { 1.0f, 1.0f, 1.0f };          // 発生範囲 (ランダム幅)
        Vector3 initialVelocity = { 0.0f, 1.0f, 0.0f };    // 初速
        Vector3 velocityRandomness = { 0.5f, 0.5f, 0.5f }; // 初速のランダム幅
        Vector3 acceleration = { 0.0f, 0.0f, 0.0f };
        float particlesPerSecond = 10.0f; // 毎秒の発生数
        float particleLifetime = 2.0f;    // パーティクルの生存期間

        Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 開始時の色
        Vector4 endColor = { 1.0f, 1.0f, 1.0f, 0.0f };   // 終了時の色

        float startSize = 1.0f; // 開始時のサイズ
        float endSize = 0.1f;   // 終了時のサイズ
        float hdrIntensity = 1.0f;

        bool isEmitting = false; // 発生させるかどうか
        float sizeCurve[10] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        ParticleBlendMode blendMode = ParticleBlendMode::kAlpha;
        float initialRotationSpeed = 0.0f;      // 基本の回転スピード
        float rotationSpeedRandomness = 0.0f;   // 回転スピードのバラつき
        EmitterType emitterType = EmitterType::Box;

        // Sphere / Cone用
        float spawnRadius = 1.0f; // 半径
        float coneAngle = 30.0f;  // 円錐の広がり角度 (度数法)
        std::string textureName = "Resources/sprite/particle.png";
    };

    void Initialize(ParticleCommon* common, const std::string& texturePath);


    void Update(float deltaTime);


    void Draw();

    // 【使い方A】手動で（単発で）発生させる関数
    void SpawnParticles(const Vector3& position, int count,
        float initialSpeed = 2.0f,
        const Vector3* direction = nullptr,
        float spreadAngle = 0.0f,
        Vector4 initialColor = { 1,1,1,1 }, 
        Vector4 endColor = { 1,1,1,0 },     
        float lifeTimeMin = 1.0f, float lifeTimeMax = 3.0f,
        float startSize = 1.0f,           
        float endSize = 0.1f);            

    void Clear();

    EmitterParams params_;
    void EmitOneShot(const EmitterParams& params, const Vector3& position);

    void SetTexture(const std::string& texturePath);
private:
    void CreateResources();



    // 自動エミッターが呼ぶ内部ヘルパー
    void SpawnFromEmitter();

private:
    static const int kMaxParticles = 1024;
    ParticleCommon* common_ = nullptr;
    uint32_t textureHandle_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    // インスタンスデータ用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource_;
    D3D12_VERTEX_BUFFER_VIEW instancingBufferView_{};
    ParticleForGPU* instancingData_ = nullptr;

    // カメラ行列用リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> matrixResource_;
    TransformationMatrix* matrixData_ = nullptr;

    std::vector<Particle> particles_; 
    std::mt19937 randomEngine_;
    UINT particleCount_ = 0;

    float spawnTimer_ = 0.0f;


};