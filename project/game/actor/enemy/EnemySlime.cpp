#include "EnemySlime.h"
#include "engine/utility/math/Math.h" // Vector3の計算用
#include <cmath> // atan2用
#include <PlayerState.h>
#include"Player.h"
#include <DebugConsole.h>
#include <algorithm>
namespace {
// モデル正面と移動方向のズレを合わせるための補正角
constexpr float kSlimeModelYawOffset = 3.1415926535f;
}

// 基本スライムの追跡ジャンプと待機徘徊
void EnemySlime::Update(float deltaTime) {
    if (ShouldHandleDefeatEffect()) {
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (isCarried_) {
        return;
    }
    if (IsThrowRecovering()) {
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (!target_ || !param_.has_value()) {
        BaseEnemy::Update(deltaTime);
        return;
    }
    if (!hasBaseScale_) {
        baseScale_ = GetScale();
        const float maxScale = (std::max)({ std::abs(baseScale_.x), std::abs(baseScale_.y), std::abs(baseScale_.z) });
        if (maxScale < 1.2f) {
            baseScale_ = { 2.0f, 2.0f, 2.0f };
            SetScale(baseScale_);
        }
        hasBaseScale_ = true;
    }

    static Math math;
    Vector3 myPos = transform_.translate;
    Vector3 targetPos = target_->GetWorldPosition();
    Vector3 toTarget = targetPos - myPos;
    toTarget.y = 0.0f;
    float length = math.Length(toTarget);

    // --- 1. 接地状態での待機 & 跳躍準備 ---
    if (isGrounded_) {
        // 地面に着いている間は水平速度を徐々に落とす（摩擦の代わり）
        velocity_.x *= 0.8f;
        velocity_.z *= 0.8f;

        // 検知範囲内なら跳躍タイマーを進める
        if (length < detectionRange_ && length > 1.0f) {
            jumpTimer_ += deltaTime;

            // 向きだけはプレイヤーの方をじわじわ向く
            Vector3 dir = math.Normalize(toTarget);
            float targetRotY = std::atan2(dir.x, dir.z) + kSlimeModelYawOffset;
            // 簡易的な線形補間で回転（パッと向かないように）
            SetRotationY(math.LerpShortAngle(GetRotation().y, targetRotY, 0.1f));

            // 一定時間経過したらジャンプ！
            if (jumpTimer_ > 1.0f) {
                float speed = param_->speed;
                float jumpPower = param_->jumpPower > 0.0f ? param_->jumpPower : 15.0f;

                velocity_.x = dir.x * speed * 5.0f; // 跳ねる瞬間に勢いをつける
                velocity_.z = dir.z * speed * 5.0f;
                velocity_.y = jumpPower;

                jumpTimer_ = 0.0f;
                isHopping_ = true;
            }
        } else if (length >= detectionRange_) {
            jumpTimer_ += deltaTime * 0.75f;

            const float wanderSpeed = (std::max)(0.5f, param_->speed * 4.0f);
            Vector3 wanderVelocity = CalculateWanderVelocity(deltaTime, wanderSpeed, 0.65f);
            Vector3 wanderDirection = { wanderVelocity.x, 0.0f, wanderVelocity.z };
            const float wanderLength = math.Length(wanderDirection);
            if (wanderLength > 0.001f) {
                wanderDirection = wanderDirection / wanderLength;
                float targetRotY = std::atan2(wanderDirection.x, wanderDirection.z) + kSlimeModelYawOffset;
                SetRotationY(math.LerpShortAngle(GetRotation().y, targetRotY, 0.08f));
            }

            if (jumpTimer_ > 1.35f && wanderLength > 0.05f) {
                float jumpPower = param_->jumpPower > 0.0f ? param_->jumpPower * 0.65f : 10.0f;
                velocity_.x = wanderDirection.x * wanderSpeed;
                velocity_.z = wanderDirection.z * wanderSpeed;
                velocity_.y = jumpPower;

                jumpTimer_ = 0.0f;
                isHopping_ = true;
            }
        } else {
            jumpTimer_ = 0.0f;
        }
    } else {
        // 空中にいる間は向きを変えない、または移動方向に合わせる
    }

    // --- 3. 見た目の演出 (スライムらしい伸縮) ---
    // 本来のスケールを基準に変形させる
    Vector3 baseScale = baseScale_;
    if (param_.has_value()) {
        // JSONで設定されたスケールがあればそれをベースにする（今は一旦固定値でデモ）
    }

    if (!isGrounded_) {
        // 空中：縦に伸びる (Stretch)
        transform_.scale.y = math.Lerp(transform_.scale.y, baseScale.y * 1.3f, 0.1f);
        transform_.scale.x = math.Lerp(transform_.scale.x, baseScale.x * 0.8f, 0.1f);
        transform_.scale.z = transform_.scale.x;
    } else {
        if (jumpTimer_ > 0.8f) {
            // ジャンプ直前：力を溜めて潰れる (Squash)
            transform_.scale.y = math.Lerp(transform_.scale.y, baseScale.y * 0.6f, 0.2f);
            transform_.scale.x = math.Lerp(transform_.scale.x, baseScale.x * 1.4f, 0.2f);
            transform_.scale.z = transform_.scale.x;
        } else {
            // 通常時：元のサイズに戻る
            transform_.scale.y = math.Lerp(transform_.scale.y, baseScale.y, 0.2f);
            transform_.scale.x = math.Lerp(transform_.scale.x, baseScale.x, 0.2f);
            transform_.scale.z = transform_.scale.x;
        }
    }

    // 2. 最後に親クラスを呼んで、重力適用と座標更新を実行！
    BaseEnemy::Update(deltaTime); 
}

std::unique_ptr<Object3d> EnemySlime::Clone() const {
    auto newSlime = std::make_unique<EnemySlime>();
    // 初期化
    newSlime->Initialize(common_, this->GetModelName());
    // 2. 親クラス(Object3d)の機能を使って、座標やモデル設定をコピーしてもらう
    newSlime->CopyFrom(this);
    newSlime->SetTarget(this->target_);
    newSlime->SetDetectionRange(this->detectionRange_);
    return newSlime;
}

// 持ち運び中に使う、プレイヤーを大きく跳ね上げる能力
void EnemySlime::ExecuteAbility(Player* player) {
    if (!player) return;

    // ① プレイヤーを遥か上空へ吹っ飛ばす！（通常のジャンプ力を超える値）
    Vector3 v = player->GetVelocity();
    v.y = 35.0f; // トランポリンジャンプ！
    player->SetVelocity(v);

    // ジャンプ状態へ移行
    player->ChangeState(std::make_unique<PlayerStateJump>());

    DebugConsole::GetInstance()->AddLog("Ability Activated: Slime Super Jump!");

    // ② 能力を使ったら、スライム自身は力を使い果たして消滅する
    player->SetCarriedEnemy(nullptr);
    if (param_.has_value()) {
        param_->hp = 0.0f;
    }
    SetIsVisible(false);
}
