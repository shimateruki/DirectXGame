#pragma once
#include <string>
#include "engine/utility/math/Math.h"
#include "Transform.h"

// ==========================================================
// キャラクターや武器に取り付ける「パーティクル発生器」コンポーネント
// ==========================================================
class GPUParticleEmitter {
public:
    GPUParticleEmitter() = default;
    ~GPUParticleEmitter() = default;

    // 初期化：発生させるプリセット名と、追従する対象(Transform)をセット
    void Initialize(const std::string& presetName, Transform* targetTransform = nullptr);

    // 毎フレーム呼ぶ（自動で間隔を計って発生させる）
    void Update(float deltaTime);

    // 再生・停止コントロール
    void Play();
    void Stop();
    void EmitOnce(); // 1回だけ強制発生

    // パラメータ調整
    void SetOffset(const Vector3& offset) { offset_ = offset; }
    void SetInterval(float interval) { emitInterval_ = interval; }
    void SetPresetName(const std::string& presetName) { presetName_ = presetName; }

    bool IsPlaying() const { return isPlaying_; }

private:
    std::string presetName_ = "";
    Transform* targetTransform_ = nullptr; // 追従する対象（剣の先やプレイヤー本体など）
    Vector3 offset_ = { 0.0f, 0.0f, 0.0f }; // 追従対象からのズレ

    bool isPlaying_ = false;
    float emitTimer_ = 0.0f;
    float emitInterval_ = 0.1f; // デフォルトの発生間隔（0.1秒ごと）
};