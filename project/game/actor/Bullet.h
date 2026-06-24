#pragma once
#include "Object3d.h"

// 一定時間で消え、衝突時にも消滅する汎用弾オブジェクト
class Bullet : public Object3d {
public:
    void Initialize(Object3dCommon* common) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    /// <summary>
    /// 弾の初期位置、速度、寿命、当たり判定属性を設定して発射する。
    /// </summary>
    void Fire(const Vector3& position, const Vector3& velocity,
       float life, uint32_t attribute, uint32_t mask);

    bool IsDead() const { return isDead_; }

private:
    Vector3 velocity_ = { 0.0f, 0.0f, 0.0f };
    float lifeTimer_ = 0.0f;
    bool isDead_ = false;
};
