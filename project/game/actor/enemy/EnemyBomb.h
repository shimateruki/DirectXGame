#pragma once
#include "BaseEnemy.h"

// ボム兵型の敵クラス
class EnemyBomb : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    std::unique_ptr<Object3d> Clone() const override;

private:
    void UpdateColorByHitCount();

    // ボム兵の状態
    enum class State {
        Chase,      // プレイヤーを追いかける
        Ignited,    // 点火（立ち止まって爆発カウントダウン）
        Exploded    // 爆発（ダメージ判定を出して消滅）
    };

    State state_ = State::Chase;

    float igniteTimer_ = 0.0f;           // 点火してからのタイマー
    const float igniteDuration_ = 1.5f;  // 点火から爆発までの時間（秒）
    const float triggerDistance_ = 3.0f; // 点火を開始するプレイヤーとの距離
    Vector3 defaultScale_ = { 1.0f, 1.0f, 1.0f };
    float explosionTimer_ = 0.0f; // 爆発判定を残すためのタイマー
    bool hasExploded_ = false;
    const float explosionRadius_ = 4.0f;    
    const float explosionDuration_ = 0.75f;

    // 吹き飛ばし・跳ね返し用
    int hitCount_ = 0;
    bool isBlownAway_ = false;
    float shakeTimer_ = 0.0f;
    float stunTimer_ = 0.0f;
    Vector3 lastShakeOffset_ = { 0,0,0 };
    float deflectedBossDamage_ = 20.0f; // ボスに与える跳ね返しダメージ

    // --- 爆発SE ---
    uint32_t seExplosionHandle_ = 0;

    // コロコロ転がり挙動用
    bool isRolling_ = true;
    float rollTimer_ = 0.0f;
};