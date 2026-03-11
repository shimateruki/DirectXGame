#pragma once
#include "IEditable.h"
#include "GPUParticleManager.h"
#include "engine/utility/math/Math.h"
#include <string>

class GPUParticleEditor : public IEditable {
public:
 

    void Initialize();

    // 連続発生（ループ）用の更新処理
    void Update(float deltaTime);

    // IEditableのオーバーライド
    void DrawImGui() override;

    // JSONによる保存と読み込み
    void Save(const std::string& presetName);
    void Load(const std::string& presetName);
    std::string GetName() override { return "GPU Particle Editor"; }
private:
    // Compute Shaderに送るパラメータ群
    Vector3 emitPos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 emitVelocity_ = { 0.0f, 1.0f, 0.0f };
    int emitCount_ = 1000;
    float emitLife_ = 2.0f;
    float velocityVariance_ = 1.0f;
    Vector4 baseColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };

    // エディタ専用の便利機能
    bool isLooping_ = false;       // 連続発生させるか
    float emitInterval_ = 0.1f;    // 発生間隔（秒）
    float emitTimer_ = 0.0f;
    Vector3 envGravity_ = { 0.0f, -0.98f, 0.0f }; // 少し弱めを初期値に
    float envDrag_ = 0.98f;
    Vector3 envWind_ = { 0.0f, 0.0f, 0.0f };
    float envTurbulence_ = 0.0f;
    Vector3 emitArea_ = { 0.0f, 0.0f, 0.0f }; // 発生範囲
    // 保存するJSONのファイル名
    char presetName_[64] = "BossExplosion";
    int blendModeIndex_ = 0; // 0: Add, 1: Alpha
};