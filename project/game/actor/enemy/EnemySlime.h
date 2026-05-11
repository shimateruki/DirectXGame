#pragma once
#include "BaseEnemy.h"

// スライム型の敵クラス
class BossCore; // 前方宣言

// スライム型の敵クラス
class EnemySlime : public BaseEnemy {
public:
    // 初期化と更新
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;

    // 衝突判定の追加
    bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    // 色の更新
    void UpdateColorByHitCount();

private:
    int hitCount_ = 0;           // 被弾回数
    bool isFirstUpdate_ = true;  // 初回更新フラグ
    bool isBlownAway_ = false;   // 吹き飛びフラグ
    float shakeTimer_ = 0.0f;    // シェイクタイマー
    float stunTimer_ = 0.0f;     // ノックバック等による硬直タイマー
    float jumpTimer_ = 0.0f;     // ジャンプ移動用の待機タイマー
    Vector3 lastShakeOffset_ = { 0,0,0 }; // 前回のシェイクオフセット
    BossCore* bossTarget_ = nullptr; // ホーミング対象
};