#pragma once
#include "engine/utility/math/Math.h"
#include <string>
#include "VFXAuthoring.h"


// GPUParticleConfigは、GPUパーティクルの発生形状、色、サイズ、環境影響、衝突をまとめた設定です。
struct GPUParticleConfig {
    int shapeType = 0;         // 0: Box(四角), 1: Sphere(球), 2: Cone(円錐)
    float shapeRadius = 2.0f;  // 球や円錐の時の半径
    float shapeAngle = 30.0f;  // 円錐の時の広がり角度（度数法）
    // --- 発生パラメータ ---
    Vector3 emitPos = { 0.0f, 0.0f, 0.0f };
    Vector3 emitArea = { 0.0f, 0.0f, 0.0f };
    Vector3 emitVelocity = { 0.0f, 1.0f, 0.0f };
    int emitCount = 1000;
    float emitLife = 2.0f;
    float velocityVariance = 1.0f;
    // 0は発生数・寿命・間隔から安全な容量を自動計算します。
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
    float rotSpeed = 0.0f;
    int blendModeIndex = 0;
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
    Vector3 envGravity = { 0.0f, -9.8f, 0.0f };
    float envDrag = 0.0f;
    Vector3 envWind = { 0.0f, 0.0f, 0.0f };
    float envTurbulence = 0.0f;
    int fieldType = 0;         // 0: None, 1: Attract, 2: Vortex, 3: Repulse
    Vector3 fieldPosition = { 0.0f, 0.0f, 0.0f }; // エミッター基準の相対位置
    float fieldStrength = 0.0f;
    float fieldRadius = 5.0f;
    float fieldFalloff = 1.0f;

    // --- エディタ・プレビュー用設定 ---
    bool isLooping = false;
    float emitInterval = 0.1f;
    float softParticleFade = 5.0f;
    int sizeEaseType = 0;  // 0:Linear, 1:EaseIn, 2:EaseOut, 3:EaseInOut
    int colorEaseType = 0;
    int enableCollision = 0;   // 0: 無効, 1: 有効
    float restitution = 0.5f;  // 跳ね返り係数 (0.0=弾まない ~ 1.0=完全弾性)
    Matrix4x4 emitterWorldMatrix = { 1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f };
};
