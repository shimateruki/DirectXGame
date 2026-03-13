#pragma once
#include "engine/utility/math/Math.h"


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

    // --- 見た目パラメータ ---
    Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 midColor = { 1.0f, 0.5f, 0.0f, 1.0f }; // 途中の色
    float colorMidTime = 0.2f;                     // 色がMidになるタイミング (0.0~1.0)
    Vector4 endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    float baseSize = 1.0f;
    float midSize = 2.0f;                          // 途中の大きさ
    float sizeMidTime = 0.2f;                      // 大きさがMidになるタイミング (0.0~1.0)
    float endSize = 0.0f;
    float rotSpeed = 0.0f;
    int blendModeIndex = 0;

    // --- 環境パラメータ ---
    Vector3 envGravity = { 0.0f, -9.8f, 0.0f };
    float envDrag = 0.0f;
    Vector3 envWind = { 0.0f, 0.0f, 0.0f };
    float envTurbulence = 0.0f;

    // --- エディタ・プレビュー用設定 ---
    bool isLooping = false;
    float emitInterval = 0.1f;
};