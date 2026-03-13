#include "GPUParticleEmitter.h"
#include "GPUParticleManager.h"

void GPUParticleEmitter::Initialize(const std::string& presetName, Transform* targetTransform) {
    presetName_ = presetName;
    targetTransform_ = targetTransform;
    isPlaying_ = false;
    emitTimer_ = 0.0f;
}

void GPUParticleEmitter::Update(float deltaTime) {
    if (!isPlaying_ || presetName_.empty()) return;

    emitTimer_ += deltaTime;

    // 指定した間隔（Interval）ごとに継続して発生させる！
    if (emitTimer_ >= emitInterval_) {
        EmitOnce();

        // タイマーをリセット（少し過ぎた分は次回に持ち越すことで正確な間隔を保つ）
        emitTimer_ = fmod(emitTimer_, emitInterval_);
    }
}

void GPUParticleEmitter::Play() {
    isPlaying_ = true;
    emitTimer_ = emitInterval_; // Playした瞬間に1回目を出させるための細工
}

void GPUParticleEmitter::Stop() {
    isPlaying_ = false;
    emitTimer_ = 0.0f;
}

void GPUParticleEmitter::EmitOnce() {
    Vector3 spawnPos = { 0, 0, 0 };

    // 追従対象が設定されていれば、そのワールド座標を取得
    if (targetTransform_) {
        // =======================================================
        // ★ 修正： GetWorldMatrix() 関数ではなく、matWorld 変数を直接読む！
        // =======================================================
        Matrix4x4 worldMat = targetTransform_->matWorld;

        Math math;
        // ローカルのオフセットをワールドの向きに変換して足し合わせる
        Vector3 worldOffset = math.TransformNormal(offset_, worldMat);

        spawnPos.x = worldMat.m[3][0] + worldOffset.x;
        spawnPos.y = worldMat.m[3][1] + worldOffset.y;
        spawnPos.z = worldMat.m[3][2] + worldOffset.z;
    } else {
        spawnPos = offset_; // 追従対象がなければただのオフセット座標を絶対座標とする
    }

    // マネージャーの「魔法の1行」を呼び出す！
    GPUParticleManager::GetInstance()->Emit(presetName_, spawnPos);
}