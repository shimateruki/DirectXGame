#pragma once
#include "BaseEnemy.h"

/// <summary>
/// チュートリアル用の攻撃練習人形
/// 攻撃を受けると震え、HPが切れると消滅し、一定時間後に復活する。
/// </summary>
class TutorialDoll : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    bool HasBeenDefeatedAtLeastOnce() const { return hasBeenDefeatedAtLeastOnce_; }


private:
    void Respawn();

    float respawnTimer_ = 0.0f;   // 復活までの待ち時間タイマー
    bool isDead_ = false;         // 倒されている状態か
    bool isInitialized_ = false;  // 初回Updateでの座標・スケール保持用

    Vector3 basePosition_ = { 0.0f, 0.0f, 0.0f }; // 初期座標を記憶
    Vector3 baseRotation_ = { 0.0f, 0.0f, 0.0f }; // 初期回転を記憶
    Vector3 baseScale_ = { 1.0f, 1.0f, 1.0f };    // 初期スケールを記憶
    float deathAnimTimer_ = 0.0f; // 死亡時の縮小演出用

    bool hasBeenDefeatedAtLeastOnce_ = false;
};
