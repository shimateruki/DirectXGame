#define NOMINMAX
#include "PlayerState.h"
#include "Player.h"
#include "InputManager.h"
#include "DebugConsole.h"
#include "engine/graphics/3d/camera/CameraManager.h"
#include "engine/utility/math/Math.h"
#include "BaseEnemy.h"
#include "EnemyGiantSlime.h"
#include "GimmickHookPullBlock.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "HitEffectDirector.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
namespace {
Vector3 GetStablePullEnemyScale(Object3d* enemy) {
    if (!enemy) {
        return { 1.0f, 1.0f, 1.0f };
    }

    const Vector3 scale = enemy->GetScale();
    if (!dynamic_cast<BaseEnemy*>(enemy)) {
        return scale;
    }

    const float absX = std::abs(scale.x);
    const float absY = std::abs(scale.y);
    const float absZ = std::abs(scale.z);
    const float minScale = (std::min)({ absX, absY, absZ });
    const float maxScale = (std::max)({ absX, absY, absZ });
    if (minScale <= 0.0001f || maxScale / minScale < 1.18f) {
        return scale;
    }

    const float stableScale = std::cbrt(absX * absY * absZ);
    return { stableScale, stableScale, stableScale };
}
}
// 敵を引き寄せる状態をまとめています。

void PlayerStatePullEnemy::Enter(Player* player) {
    if (!player || !targetEnemy_) return;

    player->SetIsControlActive(false);
    player->SetVelocity({ 0.0f, 0.0f, 0.0f }); // プレイヤー自身はその場で停止
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
    player->SetSlimeAnimationDirection(targetEnemy_->GetWorldPosition() - player->GetWorldPosition());
    isHeavyPullTarget_ = dynamic_cast<EnemyGiantSlime*>(targetEnemy_) != nullptr;
    enemyPullStartScale_ = targetEnemy_->GetScale();
    enemyBaseScale_ = GetStablePullEnemyScale(targetEnemy_);
    if (Transform* enemyTransform = targetEnemy_->GetTransform()) {
        enemyBaseRotation_ = enemyTransform->rotate;
        enemyBaseQuaternion_ = enemyTransform->quaternion;
        enemyBaseQuaternionMaster_ = enemyTransform->isQuaternionMaster;
    }

    phase_ = Phase::kShootHook;
    hookTipPos_ = player->GetWorldPosition();
    heavyPullBasePlayerPos_ = hookTipPos_;
    hasHeavyPullBasePlayerPos_ = false;

    Object3d* marker = player->GetHookMarker();
    if (marker) {
        marker->SetIsVisible(true);
        marker->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // フックとして使用する際は白色に設定
        marker->GetTransform()->translate = hookTipPos_;
    }
}

void PlayerStatePullEnemy::Update(Player* player, float deltaTime) {
    if (!player || !targetEnemy_) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
        return;
    }

    Vector3 playerPos = player->GetWorldPosition();

    if (phase_ == Phase::kShootHook) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
        player->SetSlimeAnimationDirection(targetEnemy_->GetWorldPosition() - playerPos);
        // 腕が敵に向かって伸びる
        Vector3 enemyPos = targetEnemy_->GetTransform()->translate;
        Vector3 toTarget = enemyPos - hookTipPos_;
        float dist = Math::Length(toTarget);

        if (dist < 5.0f) {
            hookTipPos_ = enemyPos;
            phase_ = Phase::kPullEnemy;
            enemyStartPos_ = enemyPos;
            HitEffectDirector::SpawnPullBindHit(enemyPos);
            pullTimer_ = -0.12f; // ヒットストップ：命中後0.12秒間タメる
            
            if (auto* giantSlime = dynamic_cast<EnemyGiantSlime*>(targetEnemy_)) {
                isHeavyPullTarget_ = true;
                giantSlime->BeginHookSplitPull(playerPos);
            } else {
                // 命中時に敵を「Carried（持ち運び）」状態へ移行させ、衝突判定を無効化
                targetEnemy_->SetCollisionAttribute(0);
                targetEnemy_->SetCollisionMask(0);

                BaseEnemy* enemyBase = dynamic_cast<BaseEnemy*>(targetEnemy_);
                if (enemyBase) {
                    enemyBase->SetCarried(true);
                }
            }

            // 命中した瞬間の火花（パーティクル）
            if (player->GetParticleSystem()) {
                Vector3 toPlayer = Math::Normalize(playerPos - enemyPos);
                player->GetParticleSystem()->SpawnParticles(
                    enemyPos, 30, 2.0f, &toPlayer, 30.0f,
                    {1.0f, 1.0f, 0.8f, 1.0f}, {1.0f, 0.8f, 0.2f, 0.0f},
                    0.2f, 0.4f, 0.8f, 0.1f
                );
            }
        }
        else {
            Vector3 dir = Math::Normalize(toTarget);
            hookTipPos_ = hookTipPos_ + dir * (150.0f * deltaTime);
        }

        Object3d* marker = player->GetHookMarker();
        if (marker) {
            Vector3 diff = hookTipPos_ - playerPos;
            marker->GetTransform()->translate = {
                playerPos.x + diff.x * 0.5f, playerPos.y + diff.y * 0.5f, playerPos.z + diff.z * 0.5f
            };
            float angleY = std::atan2(diff.x, diff.z);
            float angleX = std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z));
            marker->GetTransform()->rotate = { angleX, angleY, 0.0f };
            marker->GetTransform()->isQuaternionMaster = false;
            marker->GetTransform()->scale = { 0.5f, 0.5f, Math::Length(diff) };

            // フェーズ1のフック描画更新を行う
            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    }
    else if (phase_ == Phase::kPullEnemy) {
        // --- 敵を自分の手元へ引っ張る（放物線＆巻き取り） ---
        pullTimer_ += deltaTime;
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::PullEnemy);
        player->SetSlimePullDirection(targetEnemy_->GetWorldPosition() - playerPos);
        player->SetSlimePullProgress(0.0f);

        // 【演出】ヒットストップ：時間がマイナスの間は引き寄せず、お互いに激しくブルブル震える
        if (pullTimer_ < 0.0f) {
            float shakeX = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.4f;
            float shakeY = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.4f;
            float shakeZ = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.4f;
            
            targetEnemy_->GetTransform()->translate = {
                enemyStartPos_.x + shakeX, enemyStartPos_.y + shakeY, enemyStartPos_.z + shakeZ
            };
            targetEnemy_->GetTransform()->isQuaternionMaster = false;
            targetEnemy_->UpdateLocalMatrix();
            targetEnemy_->UpdateWorldMatrix();
            
            Object3d* marker = player->GetHookMarker();
            if (marker) {
                Vector3 diff = enemyStartPos_ - playerPos;
                marker->GetTransform()->translate = {
                    playerPos.x + diff.x * 0.5f + shakeX * 0.5f, 
                    playerPos.y + diff.y * 0.5f + shakeY * 0.5f, 
                    playerPos.z + diff.z * 0.5f + shakeZ * 0.5f
                };
                // ヒットストップ中は「ピンッ」と極限まで細く張り詰める
                float len = Math::Length(diff);
                marker->GetTransform()->scale = { 0.15f, 0.15f, len };

                marker->UpdateLocalMatrix();
                marker->UpdateWorldMatrix();
            }
            return; // これ以上は更新しない（時を止める）
        }

        if (isHeavyPullTarget_) {
            auto* giantSlime = dynamic_cast<EnemyGiantSlime*>(targetEnemy_);
            if (!giantSlime || giantSlime->HasSplit()) {
                player->ChangeState(std::make_unique<PlayerStateIdle>());
                return;
            }

            bool hasSplit = giantSlime->UpdateHookSplitPull(deltaTime, playerPos, player->GetParticleSystem());
            float progress = giantSlime->GetHookSplitProgress();
            if (!hasHeavyPullBasePlayerPos_) {
                heavyPullBasePlayerPos_ = playerPos;
                hasHeavyPullBasePlayerPos_ = true;
            }

            Vector3 enemyCurrentPos = targetEnemy_->GetTransform()->translate;
            Vector3 playerToEnemy = enemyCurrentPos - heavyPullBasePlayerPos_;
            Vector3 pullDir = { playerToEnemy.x, 0.0f, playerToEnemy.z };
            float pullDirLength = Math::Length(pullDir);
            pullDir = pullDirLength > 0.001f ? pullDir / pullDirLength : Vector3{ 0.0f, 0.0f, 1.0f };
            const float bracePulse = std::abs(std::sin(pullTimer_ * 38.0f));
            const float braceAmount = (0.10f + bracePulse * 0.16f) * (1.0f - progress * 0.25f);
            Vector3 visualPlayerPos = heavyPullBasePlayerPos_ - pullDir * braceAmount;
            visualPlayerPos.y += std::sin(pullTimer_ * 52.0f) * 0.035f * (1.0f - progress * 0.25f);
            player->SetTranslate(visualPlayerPos);
            playerPos = visualPlayerPos;

            player->SetSlimePullDirection(enemyCurrentPos - playerPos);
            player->SetSlimePullProgress(progress);

            Object3d* marker = player->GetHookMarker();
            if (marker) {
                Vector3 diff = enemyCurrentPos - playerPos;
                float len = Math::Length(diff);
                if (len < 0.01f) len = 0.01f;

                marker->GetTransform()->translate = playerPos + diff * 0.5f;
                marker->GetTransform()->rotate = {
                    std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z)),
                    std::atan2(diff.x, diff.z),
                    0.0f
                };
                marker->GetTransform()->isQuaternionMaster = false;

                float pulse = std::sin(pullTimer_ * 72.0f);
                float tension = pulse * (1.0f - progress * 0.35f) * 0.22f;
                float thickness = Math::Lerp(0.30f, 0.12f, progress);
                marker->GetTransform()->scale = {
                    (std::max)(0.06f, thickness + tension),
                    (std::max)(0.06f, thickness - tension * 0.55f),
                    len * (1.0f + std::abs(pulse) * 0.018f)
                };
                marker->SetColor({ 0.25f + progress * 0.75f, 0.95f - progress * 0.22f, 1.0f, 1.0f });
                marker->UpdateLocalMatrix();
                marker->UpdateWorldMatrix();
            }

            if (hasSplit) {
                if (hasHeavyPullBasePlayerPos_) {
                    player->SetTranslate(heavyPullBasePlayerPos_);
                    player->UpdateLocalMatrix();
                    player->UpdateWorldMatrix();
                }
                player->TriggerSlimeImpulse({ 2.4f, 0.45f, 2.4f }, 0.24f);
                player->ChangeState(std::make_unique<PlayerStateIdle>());
            }
            return;
        }

        const float kPullDuration = 0.45f; // 少しだけ時間を長くしてタメを作る
        float t = pullTimer_ / kPullDuration;
        if (t > 1.0f) t = 1.0f;

        player->SetSlimePullDirection(enemyStartPos_ - playerPos);
        player->SetSlimePullProgress(t);

        // 【演出1】イージング：最初は重たく、後半一気に飛んでくる（Ease-In）
        float easeT = t * t * t; 

        // 【演出2】敵の回転：引き寄せられながら超高速できりもみ回転する
        Vector3 rot = targetEnemy_->GetTransform()->rotate;
        rot.x += 20.0f * deltaTime;
        rot.y += 35.0f * deltaTime;
        rot.z += 15.0f * deltaTime;
        targetEnemy_->GetTransform()->rotate = rot;
        targetEnemy_->GetTransform()->isQuaternionMaster = false; // 追加: クォータニオンを無視してオイラー角回転を適用

        // 【演出3】敵の縮小：敵本来の大きさを基準に、手元で少しだけ圧縮する
        const float scaleRate = Math::Lerp(1.0f, 0.82f, easeT);
        targetEnemy_->GetTransform()->scale = {
            enemyBaseScale_.x * scaleRate,
            enemyBaseScale_.y * scaleRate,
            enemyBaseScale_.z * scaleRate
        };

        // 目標位置（プレイヤーの頭上）
        Vector3 headPos = { playerPos.x, playerPos.y + 2.5f, playerPos.z };

        // easeTを使って開始位置から目標位置への線形補間
        Vector3 basePos = {
            Math::Lerp(enemyStartPos_.x, headPos.x, easeT),
            Math::Lerp(enemyStartPos_.y, headPos.y, easeT),
            Math::Lerp(enemyStartPos_.z, headPos.z, easeT)
        };
        
        // サイン波でY軸に放物線のアーチを加える（easeTではなく純粋なtで綺麗なアーチにする）
        float arcHeight = 6.0f; 
        basePos.y += std::sin(t * 3.14159265f) * arcHeight;

        targetEnemy_->GetTransform()->translate = basePos;
        player->SetSlimePullDirection(basePos - playerPos);

        if (t >= 1.0f) {
            HitEffectDirector::SpawnPullCatchHit(headPos);

            // 【演出】頭に乗った（キャッチした）瞬間の衝撃エフェクト
            if (player->GetParticleSystem()) {
                Vector3 headPos = { playerPos.x, playerPos.y + 2.5f, playerPos.z };
                player->GetParticleSystem()->SpawnParticles(
                    headPos, 20, 1.5f, nullptr, 20.0f,
                    {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f},
                    0.2f, 0.4f, 0.6f, 0.05f
                );
            }

            player->TriggerSlimeImpulse({ 2.2f, 0.4f, 2.2f }, 0.24f);

            targetEnemy_->GetTransform()->scale = enemyBaseScale_;
            player->SetCarriedEnemy(targetEnemy_);
            player->ChangeState(std::make_unique<PlayerStateIdle>());
            return;
        }

        Object3d* marker = player->GetHookMarker();
        if (marker) {
            Vector3 enemyCurrentPos = targetEnemy_->GetTransform()->translate;
            Vector3 diff = enemyCurrentPos - playerPos;
            marker->GetTransform()->translate = {
                playerPos.x + diff.x * 0.5f, playerPos.y + diff.y * 0.5f, playerPos.z + diff.z * 0.5f
            };
            float angleY = std::atan2(diff.x, diff.z);
            float angleX = std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z));
            marker->GetTransform()->rotate = { angleX, angleY, 0.0f };
            marker->GetTransform()->isQuaternionMaster = false;
            // 手元に近づくにつれて太く戻る（イージングを利用）
            float len = Math::Length(diff);
            float thickness = Math::Lerp(0.15f, 1.0f, easeT);
            // 引っ張る反動でブルンブルン震える（近づくと収まる）
            float wobble = std::sin(pullTimer_ * 50.0f) * (1.0f - easeT) * 0.2f;
            marker->GetTransform()->scale = { thickness + wobble, thickness - wobble, len };

            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    }
}
void PlayerStatePullEnemy::Exit(Player* player) {
    if (!player) {
        return;
    }
    player->SetIsControlActive(true);
    {
        if (targetEnemy_ && player->GetCarriedEnemy() != targetEnemy_) {
            if (auto* enemyBase = dynamic_cast<BaseEnemy*>(targetEnemy_)) {
                if (enemyBase->IsCarried()) {
                    enemyBase->SetCarried(false);
                    if (Transform* enemyTransform = targetEnemy_->GetTransform()) {
                        enemyTransform->scale = enemyBaseScale_;
                        enemyTransform->rotate = enemyBaseRotation_;
                        enemyTransform->quaternion = enemyBaseQuaternion_;
                        enemyTransform->isQuaternionMaster = enemyBaseQuaternionMaster_;
                        targetEnemy_->UpdateLocalMatrix();
                        targetEnemy_->UpdateWorldMatrix();
                    }
                }
            }
        }

        if (isHeavyPullTarget_) {
            if (hasHeavyPullBasePlayerPos_) {
                player->SetTranslate(heavyPullBasePlayerPos_);
                player->UpdateLocalMatrix();
                player->UpdateWorldMatrix();
            }
            if (auto* giantSlime = dynamic_cast<EnemyGiantSlime*>(targetEnemy_)) {
                if (!giantSlime->HasSplit()) {
                    giantSlime->CancelHookSplitPull();
                }
            }
        }

        Object3d* marker = player->GetHookMarker();
        if (marker) {
            marker->SetIsVisible(false);
            marker->GetTransform()->scale = { 1.0f, 1.0f, 1.0f };
            marker->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f };
            marker->GetTransform()->isQuaternionMaster = true; // 回転モードの復帰
            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    }
}
