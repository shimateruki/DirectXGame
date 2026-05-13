#pragma once
#include <string>
#include "engine/utility/math/Math.h"

//  前方宣言
class Object3d;

// ==========================================================
// キャラクターや武器に取り付ける「パーティクル発生器」コンポーネント
// ==========================================================
class GPUParticleEmitter {
public:
    GPUParticleEmitter() = default;
    ~GPUParticleEmitter() = default;


    void Initialize(const std::string& presetName, Object3d* targetObject = nullptr);

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
    const std::string& GetName() const { return presetName_; }

private:
    std::string presetName_ = "";
    Object3d* targetObject_ = nullptr; // ★ 修正: Object3dへのポインタを保持する
    Vector3 offset_ = { 0.0f, 0.0f, 0.0f };

    bool isPlaying_ = false;
    float emitTimer_ = 0.0f;
    float emitInterval_ = 0.1f;
};