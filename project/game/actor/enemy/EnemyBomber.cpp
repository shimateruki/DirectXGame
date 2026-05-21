#include "EnemyBomber.h"
#include "EnemyFactory.h"
#include "CollisionConfig.h"
#include <cmath>

void EnemyBomber::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    common_ = common;
    throwTimer_ = throwInterval_;

    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kPlayerAttack);
}

void EnemyBomber::Update(float deltaTime) {
    // プレイヤーがいない場合や倒された後は何もしない
    if (!target_ || isDead) {
        BaseEnemy::Update(deltaTime);
        return;
    }

    // プレイヤーへのベクトルと距離を計算
    Vector3 playerPos = target_->GetTranslate();
    Vector3 myPos = GetTranslate();
    Vector3 toPlayer = playerPos - myPos;
    float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);

    // 常にプレイヤーの方を向く
    if (distance > 0.1f) {
        SetRotationY(std::atan2(toPlayer.x, toPlayer.z));
    }

    // プレイヤーが検知範囲（detectionRange_）内にいたらボムを投げる
    if (distance <= detectionRange_) {
        throwTimer_ -= deltaTime;
        if (throwTimer_ <= 0.0f) {
            ThrowBomb();
            throwTimer_ = throwInterval_; // タイマーリセット
        }
    }

    // 重力処理
    if (param_.has_value()) {
        velocity_.y -= param_.value().gravity * deltaTime;
    }

    BaseEnemy::Update(deltaTime);
}

void EnemyBomber::ThrowBomb() {
    // コールバックが設定されていない場合は投げられない
    if (!spawnCallback_ || !common_ || !target_) return;

    // EnemyFactory を使ってボムを生成
    auto bomb = EnemyFactory::GetInstance()->CreateEnemy("Bomb", common_);

    // ボムの出現位置を自身の少し上（頭上など）に設定
    Vector3 myPos = GetTranslate();
    myPos.y += 2.0f;
    bomb->SetTranslate(myPos);

    // ボムにもプレイヤーを追いかけさせるためにターゲットをセット
    bomb->SetTarget(target_);

    // プレイヤーへ向かって投げるための方向ベクトルを計算
    Vector3 playerPos = target_->GetTranslate();
    Vector3 toPlayer = playerPos - myPos;
    toPlayer.y = 0.0f; // 高低差は一旦無視して水平方向のみで正規化
    float dist = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);

    if (dist > 0.001f) {
        toPlayer.x /= dist;
        toPlayer.z /= dist;
    }

    // 投げられた状態（isThrown_=true）にするため、EnemyBomb側の処理を走らせる
    bomb->SetCarried(false);

    // ボムに初速を与える（斜め上に向かって投げる）
    // ※ 届かない場合や飛びすぎる場合は forwardSpeed と upSpeed を調整してください
    float forwardSpeed = 15.0f;
    float upSpeed = 20.0f;
    bomb->SetVelocity({ toPlayer.x * forwardSpeed, upSpeed, toPlayer.z * forwardSpeed });

    // コールバック経由で生成したボムをシーンの敵リストに登録する
    spawnCallback_(std::move(bomb));
}