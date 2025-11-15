#pragma once
#include "Object3d.h"

class Bullet : public Object3d {
public:
    void Initialize(Object3dCommon* common) override;
    void Update() override;
    bool OnCollision(Object3d* other) override;

    /// <summary>
    /// 弾を発射する
    /// </summary>
    void Fire(const Vector3& position, const Vector3& velocity,
        int life, uint32_t attribute, uint32_t mask);

    bool IsDead() const { return isDead_; }

private:
    Vector3 velocity_ = { 0.0f, 0.0f, 0.0f };
    int lifeTimer_ = 0;
    bool isDead_ = false;
};