#include "TrailEmitter.h"
#include "Object3d.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
#include <cmath>

void TrailEmitter::Start(Object3d* target) {
    target_ = target;
    isActive_ = true;
    hasLastPos_ = false; // 位置履歴をリセットして初回は即Emitしない
}

void TrailEmitter::Stop() {
    isActive_ = false;
    hasLastPos_ = false;
}

void TrailEmitter::Update(float /*deltaTime*/) {
    if (!isActive_ || !target_) return;
    if (!config_.emitMesh && !config_.emitParticle) return;

    Vector3 currentPos = target_->GetWorldPosition();

    // 初回フレームは位置を記録するだけ
    if (!hasLastPos_) {
        lastEmitPos_ = currentPos;
        hasLastPos_ = true;
        return;
    }

    // 移動距離を計算
    Vector3 diff = currentPos - lastEmitPos_;
    float dist = Math::Length(diff);

    if (dist < config_.emitDistance) return; // まだ足りない

    // 移動方向からY軸回転を算出 (autoOrient=true のとき使う)
    Vector3 moveDir = Math::Normalize(diff);
    float rotY = atan2f(moveDir.x, moveDir.z);
    Vector3 effectRot = config_.autoOrient ? Vector3{ 0.0f, rotY, 0.0f } : Vector3{ 0.0f, 0.0f, 0.0f };

    // 前回位置と現在位置の中間点にSpawn（より自然な配置）
    Vector3 spawnPos = {
        (lastEmitPos_.x + currentPos.x) * 0.5f,
        (lastEmitPos_.y + currentPos.y) * 0.5f,
        (lastEmitPos_.z + currentPos.z) * 0.5f
    };

    // ── メッシュエフェクト ──
    if (config_.emitMesh && !config_.meshEffectPreset.empty()) {
        std::string path = "Resources/json/effect/" + config_.meshEffectPreset + ".json";
        MeshEffectManager::GetInstance()->SpawnEffectAt(path, spawnPos, effectRot, config_.scale);
    }

    // ── GPUパーティクル ──
    if (config_.emitParticle && !config_.gpuParticlePreset.empty()) {
        Matrix4x4 emitMat = Math::MakeIdentity4x4();
        emitMat.m[3][0] = spawnPos.x;
        emitMat.m[3][1] = spawnPos.y;
        emitMat.m[3][2] = spawnPos.z;
        GPUParticleManager::GetInstance()->Emit(config_.gpuParticlePreset, spawnPos, emitMat);
    }

    lastEmitPos_ = currentPos;
}
