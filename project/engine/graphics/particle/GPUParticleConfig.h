#pragma once
#include "engine/utility/math/Math.h"
#include <string>
#include "VFXAuthoring.h"


// GPUParticleConfigは、GPUパーティクルの発生形状、色、サイズ、環境影響、衝突をまとめた設定です。
struct GPUParticleConfig {
    int shapeType = 0;         // 0: Box、1: Sphere、2: Cone、3: Mesh頂点、4: Heart。ParticleCS.hlslと対応。
    float shapeRadius = 2.0f;  // 球や円錐の時の半径
    float shapeAngle = 30.0f;  // 円錐の時の広がり角度（度数法）

    // --- 発生パラメータ ---
    Vector3 emitPos = { 0.0f, 0.0f, 0.0f }; // 発生中心のワールド座標。Mesh形状は頂点をWorld行列で変換する。
    Vector3 emitArea = { 0.0f, 0.0f, 0.0f }; // Boxの各軸の半幅。Heartではxが奥行き、yが輪郭の揺らぎ幅。
    Vector3 emitVelocity = { 0.0f, 1.0f, 0.0f }; // ワールド単位/秒。Coneでは方向と基準速さを兼ねる。
    int emitCount = 1000; // 一回の発生数。空きスロット不足時は全数出るとは限らない。
    float emitLife = 2.0f; // 発生から消滅までの秒数。使用側では正の値を指定する。
    float velocityVariance = 1.0f;

    // 0は発生数・寿命・間隔から容量を推定する。256～10000とLOD上限で制限され、溢れない保証ではない。
    int maxParticles = 0;

    VFXLodSettings lod;

    // --- 見た目パラメータ ---
    Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 midColor = { 1.0f, 0.5f, 0.0f, 1.0f }; // 途中の色
    float colorMidTime = 0.2f;                     // 色がMidになるタイミング (0.0~1.0)
    Vector4 endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    float baseSize = 1.0f;
    float midSize = 2.0f;                          // 途中の大きさ
    float sizeMidTime = 0.2f;                      // 大きさがMidになるタイミング (0.0~1.0)
    float endSize = 0.0f;
    float colorIntensity = 1.0f;
    std::string texturePath = "Resources/sprite/Particle.png";
    float rotSpeed = 0.0f; // 回転速度の乱数範囲の絶対値。ラジアン/秒。
    int blendModeIndex = 0; // 0: 加算、1: 通常α、2: 背景コピーを参照する歪み。
    int spriteSheetColumns = 1;
    int spriteSheetRows = 1;
    int spriteSheetFrameCount = 1;
    float spriteSheetFps = 0.0f;
    int spriteSheetLoop = 0;
    int spriteSheetRandomStart = 0;
    int alignToVelocity = 0;
    float velocityStretch = 0.0f;
    int particleType = 0;       // 0: Billboard, 1: Trail
    float trailLength = 0.15f;
    int receiveLighting = 0;
    Vector3 lightDirection = { -0.4f, -1.0f, 0.3f };
    Vector3 lightColor = { 1.0f, 1.0f, 1.0f };
    float lightingStrength = 1.0f;

    // --- 環境パラメータ ---
    Vector3 envGravity = { 0.0f, -9.8f, 0.0f }; // ワールド単位/秒²。各粒子へ発生時にコピーする。
    float envDrag = 0.0f; // 通常更新では速度にsaturate(1-drag*dt)を掛ける。大きなdtでは速度が0になる。
    Vector3 envWind = { 0.0f, 0.0f, 0.0f }; // 名前は風だが、GPU更新では加速度として加算する。
    float envTurbulence = 0.0f;
    int fieldType = 0;         // 0: None, 1: Attract, 2: Vortex, 3: Repulse
    Vector3 fieldPosition = { 0.0f, 0.0f, 0.0f }; // エミッター基準の相対位置
    float fieldStrength = 0.0f;
    float fieldRadius = 5.0f;
    float fieldFalloff = 1.0f;

    // --- エディタ・プレビュー用設定 ---
    bool isLooping = false;
    float emitInterval = 0.1f;
    float softParticleFade = 5.0f; // 背景との視点空間深度差でαを減衰する距離。ほぼ0なら減衰を無効化。
    int sizeEaseType = 0; // ParticleCS.hlslのApplyEasingの0～30。VFXCurveEasingの0～4とは別の番号体系。
    int colorEaseType = 0;
    int enableCollision = 0;   // 0: 無効, 1: 有効
    float restitution = 0.5f;  // 跳ね返り係数 (0.0=弾まない ~ 1.0=完全弾性)
    Matrix4x4 emitterWorldMatrix = { 1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f };
};
