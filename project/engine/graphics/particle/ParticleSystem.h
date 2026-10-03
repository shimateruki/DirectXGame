#pragma once
#include "engine/utility/math/Math.h"
#include "ParticleCommon.h" 
#include "VFXAuthoring.h"
#include <d3d12.h>
#include <wrl.h>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <random>
#include <string>

/// <summary>CPUパーティクルの発生領域の形。</summary>
enum class EmitterType {
    Box,
    Sphere,
    Cone,
};

/// <summary>
/// CPUで粒子を更新し、GPUインスタンシングで描画する。
/// </summary>
class ParticleSystem {
private:
    // 1粒分のシミュレーション状態。描画用データとは分けて保持する。
    struct Particle {
        Vector3 position;
        Vector3 velocity;
        float lifeTime;       // 生存期間
        float currentTime;    // 現在の時間

        // 寿命に沿って補間する描画属性。
        Vector4 startColor;
        Vector4 endColor;
        float startSize;
        float endSize;
        float rotation;       // 今の角度 (ラジアン)
        float rotationSpeed;  // 回転スピード (ラジアン/秒)
        Vector3 acceleration;

        float hdrIntensity;
        Vector2 baseScale = { 1.0f, 1.0f };
        bool useAuthoringCurves = false;
        VFXBakedFloatCurve sizeOverLife;
        VFXBakedColorGradient colorOverLife;

    };

    /// <summary>頂点シェーダーへ渡す粒子の色とワールド行列。</summary>
    struct ParticleForGPU {
        Vector4 color;
        Matrix4x4 world;
    };

    // 描画時に頂点シェーダーへ渡すカメラ行列。
    struct TransformationMatrix {
        Matrix4x4 viewProjection;
    };

public:
    /// <summary>エディタで編集できる自動発生と粒子の初期値。</summary>
    struct EmitterParams {
        Vector3 spawnPosition = { 0.0f, 0.0f, 0.0f };      // 発生座標
        Vector3 spawnArea = { 1.0f, 1.0f, 1.0f };          // 発生範囲 (ランダム幅)
        Vector3 initialVelocity = { 0.0f, 1.0f, 0.0f };    // 初速
        Vector3 velocityRandomness = { 0.5f, 0.5f, 0.5f }; // 初速のランダム幅
        Vector3 acceleration = { 0.0f, 0.0f, 0.0f };
        int emitCount = 1;                 // 単発発生1回あたりの粒子数
        float particlesPerSecond = 10.0f; // 毎秒の発生数
        float particleLifetime = 2.0f;    // パーティクルの生存期間

        Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 開始時の色
        Vector4 endColor = { 1.0f, 1.0f, 1.0f, 0.0f };   // 終了時の色

        float startSize = 1.0f; // 開始時のサイズ
        float endSize = 0.1f;   // 終了時のサイズ
        float hdrIntensity = 1.0f;

        bool isEmitting = true; // 発生させるかどうか
        float sizeCurve[10] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        bool useAuthoringCurves = false;
        VFXFloatCurve sizeOverLife;
        VFXColorGradient colorOverLife;
        VFXLodSettings lod;
        ParticleBlendMode blendMode = ParticleBlendMode::kAlpha;
        float initialRotationSpeed = 0.0f;      // 基本の回転スピード
        float rotationSpeedRandomness = 0.0f;   // 回転スピードのバラつき
        EmitterType emitterType = EmitterType::Box;

        // Sphere / Cone の発生領域で使用する。
        float spawnRadius = 1.0f; // 半径
        float coneAngle = 30.0f;  // 円錐の広がり角度 (度数法)
        std::string textureName = "Resources/sprite/particle.png";
    };

    /// <summary>粒子の描画リソースと初期テクスチャを用意する。</summary>
    /// <param name="common">描画中に有効な共通リソース。</param>
    /// <param name="texturePath">初期テクスチャのパス。</param>
    void Initialize(ParticleCommon* common, const std::string& texturePath);

    /// <summary>自動発生と生存粒子のシミュレーションを進める。</summary>
    /// <param name="deltaTime">前回更新からの経過秒数。</param>
    void Update(float deltaTime);

    /// <summary>更新済みの粒子を現在の描画先へ描画する。</summary>
    void Draw();

    /// <summary>自動発生とは別に、指定位置から一度だけ粒子を出す。</summary>
    /// <param name="position">発生の中心位置。</param>
    /// <param name="count">発生させる粒子数。</param>
    /// <param name="direction">指定時はこの方向を基準に初速を決める。</param>
    /// <param name="spreadAngle">direction の周囲へ広げる角度。</param>
    void SpawnParticles(const Vector3& position, int count,
        float initialSpeed = 2.0f,
        const Vector3* direction = nullptr,
        float spreadAngle = 0.0f,
        Vector4 initialColor = { 1,1,1,1 }, 
        Vector4 endColor = { 1,1,1,0 },     
        float lifeTimeMin = 1.0f, float lifeTimeMax = 3.0f,
        float startSize = 1.0f,           
        float endSize = 0.1f);            

    /// <summary>現在の粒子を消去する。</summary>
    void Clear();

    /// <summary>エディタの巻き戻し用に粒子・発生端数・乱数系列を初期状態へ戻す。</summary>
    void ResetSimulation(uint32_t randomSeed = 0xC0D3u);

    /// <summary>エディタプレビューで表示位置に加えるオフセットを指定する。</summary>
    void SetEditorPreviewOffset(const Vector3& offset) { editorPreviewOffset_ = offset; }

    /// <summary>シミュレーションの時間倍率を指定する。負値は0として扱う。</summary>
    void SetSimulationTimeScale(float timeScale) { simulationTimeScale_ = (std::max)(0.0f, timeScale); }

    /// <summary>現在生存中の粒子数を返す。</summary>
    size_t GetActiveParticleCount() const { return particles_.size(); }

    /// <summary>前回更新のCPU所要時間をミリ秒で返す。</summary>
    float GetLastUpdateCpuTimeMs() const { return lastUpdateCpuTimeMs_; }

    /// <summary>前回描画のCPU所要時間をミリ秒で返す。</summary>
    float GetLastDrawCpuTimeMs() const { return lastDrawCpuTimeMs_; }

    /// <summary>保持する粒子関連リソースの概算バイト数を返す。</summary>
    size_t GetEstimatedMemoryBytes() const;

    /// <summary>このシステムが保持できる粒子数の上限を返す。</summary>
    static constexpr int GetMaxParticles() { return kMaxParticles; }

    // エディタが直接編集する発生設定。
    EmitterParams params_;

    /// <summary>指定設定で一度だけ粒子を発生させる。</summary>
    void EmitOneShot(const EmitterParams& params, const Vector3& position);

    /// <summary>描画に使用するテクスチャを切り替える。</summary>
    void SetTexture(const std::string& texturePath);

    /// <summary>基本のヒットエフェクトを発生させる。</summary>
    void SpawnPrimitiveHitEffect(const Vector3& position);

    /// <summary>星形のヒットエフェクトを発生させる。</summary>
    void SpawnStarHitEffect(const Vector3& position);

    /// <summary>斬撃エフェクトを発生させる。</summary>
    void SpawnSlashEffect(const Vector3& position, float baseRotation = 0.0f);

    /// <summary>指定回数の連続ヒット用粒子を発生させる。</summary>
    void SpawnContinuousHitEffect(const Vector3& position, int count,
        float initialSpeed,
        Vector4 color,
        float lifeTimeMin, float lifeTimeMax,
        Vector2 baseScale);
private:
    // 初期化時に描画用バッファを作成する。
    void CreateResources();

    // 自動エミッターから発生させる際の設定解決と粒子生成。
    void SpawnFromEmitter();

    /// <summary>指定位置にエミッター設定に従う粒子を発生させる。</summary>
    void SpawnFromParams(const EmitterParams& params, const Vector3& origin);

    /// <summary>カメラ距離による粒子発生倍率を決める。</summary>
    float ResolveEmissionScale(const EmitterParams& params, const Vector3& origin) const;

    /// <summary>設定とLODに基づく生存粒子数の上限を決める。</summary>
    std::size_t ResolveParticleLimit(const EmitterParams& params) const;
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
    float simulationTimeScale_ = 1.0f;
    Vector3 editorPreviewOffset_ = { 0.0f, 0.0f, 0.0f };
    float lastUpdateCpuTimeMs_ = 0.0f;
    float lastDrawCpuTimeMs_ = 0.0f;
};
