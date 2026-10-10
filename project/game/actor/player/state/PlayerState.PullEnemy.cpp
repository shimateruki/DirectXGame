#define NOMINMAX
#include "game/actor/player/state/PlayerState.h"
#include "game/actor/player/Player.h"
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
        return {1.0f, 1.0f, 1.0f};
    }

    const Vector3 scale = enemy->GetScale();
    if (!dynamic_cast<BaseEnemy*>(enemy)) {
        return scale;
    }

    const float absX = std::abs(scale.x);
    const float absY = std::abs(scale.y);
    const float absZ = std::abs(scale.z);
    const float minScale = (std::min)({absX, absY, absZ});
    const float maxScale = (std::max)({absX, absY, absZ});
    if (minScale <= 0.0001f || maxScale / minScale < 1.18f) {
        return scale;
    }

    const float stableScale = std::cbrt(absX * absY * absZ);
    return {stableScale, stableScale, stableScale};
}
} // namespace
// 敵を引き寄せる状態をまとめています。

// 右クリックで狙った敵を引き寄せる通常形態の行動です。対象の消失や距離切れでも安全に状態を戻します。

void PlayerStatePullEnemy::Enter(Player* player) {
    if (!player || !targetEnemy_)
        return;

    player->SetIsControlActive(false);
    player->SetVelocity({0.0f, 0.0f, 0.0f}); // プレイヤー自身はその場で停止
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
    player->SetSlimeAnimationDirection(targetEnemy_->GetWorldPosition() - player->GetWorldPosition());
    isHeavyPullTarget_ = dynamic_cast<EnemyGiantSlime*>(targetEnemy_) != nullptr;
    enemyPullStartScale_ = targetEnemy_->GetScale();
    enemyBaseScale_ = GetStablePullEnemyScale(targetEnemy_);
    if (const Transform* enemyTransform = targetEnemy_->GetTransform()) {
        enemyBaseRotation_ = enemyTransform->rotate;
        enemyBaseQuaternion_ = enemyTransform->quaternion;
        enemyBaseQuaternionMaster_ = enemyTransform->isQuaternionMaster;
    }

    phase_ = Phase::kShootHook;
    hookTipPos_ = player->GetWorldPosition();
    heavyPullBasePlayerPos_ = hookTipPos_;
    hasHeavyPullBasePlayerPos_ = false;
    normalPullBasePlayerPos_ = hookTipPos_;
    hasNormalPullBasePlayerPos_ = false;

    Object3d* marker = player->GetHookMarker();
    if (marker) {
        marker->SetIsVisible(true);
        marker->SetColor({0.10f, 0.82f, 1.0f, 1.0f});
        marker->SetTranslate(hookTipPos_);
    }
}

void PlayerStatePullEnemy::Update(Player* player, float deltaTime) {
    if (!player) {
        return;
    }
    if (!targetEnemy_) {
        // 対象が消えたフレームでフック状態を終了し、通常操作へ戻します。
        player->ChangeState(std::make_unique<PlayerStateIdle>());
        return;
    }

    Vector3 playerPos = player->GetWorldPosition();

    if (phase_ == Phase::kShootHook) {
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
        player->SetSlimeAnimationDirection(targetEnemy_->GetWorldPosition() - playerPos);
        // フックの先端を対象へ進め、到達した時点で引き寄せ可否を判定します。
        Vector3 enemyPos = targetEnemy_->GetTransform()->translate;
        Vector3 toTarget = enemyPos - hookTipPos_;
        float dist = Math::Length(toTarget);

        if (dist < 5.0f) {
            hookTipPos_ = enemyPos;
            if (auto* enemyBase = dynamic_cast<BaseEnemy*>(targetEnemy_); enemyBase && enemyBase->IsPullImmune()) {
                HitEffectDirector::SpawnPullBindHit(enemyPos);
                if (Object3d* marker = player->GetHookMarker()) {
                    marker->SetIsVisible(false);
                }
                player->SetSlimePullProgress(0.0f);
                player->ChangeState(std::make_unique<PlayerStateIdle>());
                return;
            }
            phase_ = Phase::kPullEnemy;
            enemyStartPos_ = enemyPos;
            HitEffectDirector::SpawnPullBindHit(enemyPos);
            pullTimer_ = -0.12f; // ヒットストップ：命中後0.12秒間タメる

            if (auto* giantSlime = dynamic_cast<EnemyGiantSlime*>(targetEnemy_)) {
                isHeavyPullTarget_ = true;
                giantSlime->BeginHookSplitPull(playerPos);
            } else {
                // 引き寄せ中の敵は運搬状態へ移し、プレイヤーや地形との二重衝突を避けます。
                targetEnemy_->SetCollisionAttribute(0);
                targetEnemy_->SetCollisionMask(0);

                BaseEnemy* enemyBase = dynamic_cast<BaseEnemy*>(targetEnemy_);
                if (enemyBase) {
                    enemyBase->SetCarried(true);
                }
            }

            // 命中演出は拘束状態へ切り替えた後に発生させます。
            if (player->GetParticleSystem()) {
                Vector3 toPlayer = Math::Normalize(playerPos - enemyPos);
                player->GetParticleSystem()->SpawnParticles(enemyPos, 30, 2.0f, &toPlayer, 30.0f,
                                                            {0.55f, 0.95f, 1.0f, 1.0f}, {0.25f, 0.75f, 1.0f, 0.0f},
                                                            0.2f, 0.4f, 0.8f, 0.1f);
            }
        } else {
            Vector3 dir = Math::Normalize(toTarget);
            hookTipPos_ = hookTipPos_ + dir * (150.0f * deltaTime);
        }

        Object3d* marker = player->GetHookMarker();
        if (marker) {
            Vector3 diff = hookTipPos_ - playerPos;
            marker->SetTranslate({playerPos.x + diff.x * 0.5f, playerPos.y + diff.y * 0.5f,
                                                 playerPos.z + diff.z * 0.5f});
            float angleY = std::atan2(diff.x, diff.z);
            float angleX = std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z));
            marker->SetEulerAngles({angleX, angleY, 0.0f});
            marker->SetQuaternionMaster(false);
            marker->SetScale({0.5f, 0.5f, Math::Length(diff)});
            marker->SetColor({0.10f, 0.82f, 1.0f, 1.0f});

            // 長さと向きを更新した後にワールド行列を確定します。
            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    } else if (phase_ == Phase::kPullEnemy) {
        // 命中後は短いヒットストップを挟み、敵をプレイヤーの手元へ移動します。
        pullTimer_ += deltaTime;
        player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::PullEnemy);
        player->SetSlimePullDirection(targetEnemy_->GetWorldPosition() - playerPos);
        player->SetSlimePullProgress(0.0f);

        // タイマーが負の間は引き寄せを止め、拘束の反動だけ見せます。
        if (pullTimer_ < 0.0f) {
            Vector3 tetherEnemyPos = targetEnemy_->GetWorldPosition();
            float shakeX = 0.0f;
            float shakeY = 0.0f;
            float shakeZ = 0.0f;
            if (isHeavyPullTarget_) {
                // 巨大スライムは本体座標を乱数で揺らさず、後端を固定した専用抵抗モーションを使います。
                if (auto* giantSlime = dynamic_cast<EnemyGiantSlime*>(targetEnemy_)) {
                    giantSlime->UpdateHookSplitPull(0.0f, playerPos, player->GetParticleSystem());
                    tetherEnemyPos = giantSlime->GetHookAttachmentPosition();
                }
            } else {
                shakeX = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.4f;
                shakeY = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.4f;
                shakeZ = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.4f;
                tetherEnemyPos = {enemyStartPos_.x + shakeX, enemyStartPos_.y + shakeY, enemyStartPos_.z + shakeZ};
                targetEnemy_->SetTranslate(tetherEnemyPos);
                targetEnemy_->SetQuaternionMaster(false);
                targetEnemy_->UpdateLocalMatrix();
                targetEnemy_->UpdateWorldMatrix();
            }

            Object3d* marker = player->GetHookMarker();
            if (marker) {
                Vector3 diff = tetherEnemyPos - playerPos;
                marker->SetTranslate({playerPos.x + diff.x * 0.5f, playerPos.y + diff.y * 0.5f,
                                                     playerPos.z + diff.z * 0.5f});
                // ヒットストップ中は「ピンッ」と極限まで細く張り詰める
                float len = Math::Length(diff);
                marker->SetScale({0.13f, 0.10f, len});
                marker->SetColor({0.10f, 0.82f, 1.0f, 1.0f});

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
            const Vector3 hookAttachmentPos = giantSlime->GetHookAttachmentPosition();
            Vector3 playerToEnemy = enemyCurrentPos - heavyPullBasePlayerPos_;
            Vector3 pullDir = {playerToEnemy.x, 0.0f, playerToEnemy.z};
            float pullDirLength = Math::Length(pullDir);
            pullDir = pullDirLength > 0.001f ? pullDir / pullDirLength : Vector3{0.0f, 0.0f, 1.0f};
            const float bracePulse = std::abs(std::sin(pullTimer_ * 38.0f));
            const float braceAmount = (0.10f + bracePulse * 0.16f) * (1.0f - progress * 0.25f);
            Vector3 visualPlayerPos = heavyPullBasePlayerPos_ - pullDir * braceAmount;
            visualPlayerPos.y += std::sin(pullTimer_ * 52.0f) * 0.035f * (1.0f - progress * 0.25f);
            player->SetTranslate(visualPlayerPos);
            playerPos = visualPlayerPos;

            player->SetSlimePullDirection(hookAttachmentPos - playerPos);
            player->SetSlimePullProgress(progress);

            Object3d* marker = player->GetHookMarker();
            if (marker) {
                Vector3 diff = hookAttachmentPos - playerPos;
                float len = Math::Length(diff);
                if (len < 0.01f)
                    len = 0.01f;

                marker->SetTranslate(playerPos + diff * 0.5f);
                marker->SetEulerAngles({std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z)),
                                                  std::atan2(diff.x, diff.z), 0.0f});
                marker->SetQuaternionMaster(false);

                float pulse = std::sin(pullTimer_ * 72.0f);
                float tension = pulse * (1.0f - progress * 0.35f) * 0.22f;
                float thickness = Math::Lerp(0.30f, 0.12f, progress);
                marker->SetScale({(std::max)(0.06f, thickness + tension),
                                                 (std::max)(0.06f, thickness - tension * 0.55f),
                                                 len * (1.0f + std::abs(pulse) * 0.018f)});
                marker->SetColor({0.10f, 0.82f + progress * 0.06f, 1.0f, 1.0f});
                marker->UpdateLocalMatrix();
                marker->UpdateWorldMatrix();
            }

            if (hasSplit) {
                if (hasHeavyPullBasePlayerPos_) {
                    player->SetTranslate(heavyPullBasePlayerPos_);
                    player->UpdateLocalMatrix();
                    player->UpdateWorldMatrix();
                }
                player->TriggerSlimeImpulse({2.4f, 0.45f, 2.4f}, 0.24f);
                player->ChangeState(std::make_unique<PlayerStateIdle>());
            }
            return;
        }

        const float kPullDuration = 0.62f; // 少しだけ時間を長くしてタメを作る
        float t = pullTimer_ / kPullDuration;
        if (t > 1.0f)
            t = 1.0f;

        player->SetSlimePullDirection(enemyStartPos_ - playerPos);
        player->SetSlimePullProgress(t);

        if (!hasNormalPullBasePlayerPos_) {
            normalPullBasePlayerPos_ = playerPos;
            hasNormalPullBasePlayerPos_ = true;
        }
        Vector3 flatPullDir = enemyStartPos_ - normalPullBasePlayerPos_;
        flatPullDir.y = 0.0f;
        float flatPullLength = Math::Length(flatPullDir);
        flatPullDir = flatPullLength > 0.001f ? flatPullDir / flatPullLength : Vector3{0.0f, 0.0f, 1.0f};
        const float bracePulse = std::abs(std::sin(pullTimer_ * 36.0f));
        const float yankWave = (std::max)(0.0f, std::sin(pullTimer_ * 22.0f));
        const float yankPulse = yankWave * yankWave * (1.0f - t * 0.35f);
        const float braceAmount = (0.20f + bracePulse * 0.24f + yankPulse * 0.28f) * (1.0f - t * 0.45f);
        Vector3 bracedPlayerPos = normalPullBasePlayerPos_ - flatPullDir * braceAmount;
        bracedPlayerPos.y = normalPullBasePlayerPos_.y;
        player->SetTranslate(bracedPlayerPos);
        player->UpdateLocalMatrix();
        player->UpdateWorldMatrix();
        playerPos = bracedPlayerPos;

        // 【演出1】イージング：最初は重たく、後半一気に飛んでくる（Ease-In）
        float easeT = t * t * t;

        // 【演出2】敵の回転：引き寄せられながら超高速できりもみ回転する
        Vector3 rot = targetEnemy_->GetTransform()->rotate;
        rot.x += 20.0f * deltaTime;
        rot.y += 35.0f * deltaTime;
        rot.z += 15.0f * deltaTime;
        targetEnemy_->SetEulerAngles(rot);
        targetEnemy_->SetQuaternionMaster(false); // 追加: クォータニオンを無視してオイラー角回転を適用

        // 【演出3】敵の縮小：敵本来の大きさを基準に、手元で少しだけ圧縮する
        const float scaleRate = Math::Lerp(1.0f, 0.82f, easeT);
        targetEnemy_->SetScale({enemyBaseScale_.x * scaleRate, enemyBaseScale_.y * scaleRate,
                                               enemyBaseScale_.z * scaleRate});

        // 目標位置（プレイヤーの頭上）
        Vector3 headPos = {playerPos.x, playerPos.y + 1.55f, playerPos.z};

        // easeTを使って開始位置から目標位置への線形補間
        Vector3 basePos = {Math::Lerp(enemyStartPos_.x, headPos.x, easeT),
                           Math::Lerp(enemyStartPos_.y, headPos.y, easeT),
                           Math::Lerp(enemyStartPos_.z, headPos.z, easeT)};

        // サイン波でY軸に放物線のアーチを加える（easeTではなく純粋なtで綺麗なアーチにする）
        float arcHeight = 7.8f;
        basePos.y += std::sin(t * 3.14159265f) * arcHeight;

        targetEnemy_->SetTranslate(basePos);
        player->SetSlimePullDirection(basePos - playerPos);

        if (t >= 1.0f) {
            HitEffectDirector::SpawnPullCatchHit(headPos);

            // 【演出】頭に乗った（キャッチした）瞬間の衝撃エフェクト
            if (player->GetParticleSystem()) {
                Vector3 headPos = {playerPos.x, playerPos.y + 1.55f, playerPos.z};
                player->GetParticleSystem()->SpawnParticles(headPos, 20, 1.5f, nullptr, 20.0f,
                                                            {0.55f, 0.95f, 1.0f, 1.0f}, {0.25f, 0.75f, 1.0f, 0.0f},
                                                            0.2f, 0.4f, 0.6f, 0.05f);
            }

            player->TriggerSlimeImpulse({2.2f, 0.4f, 2.2f}, 0.24f);
            if (hasNormalPullBasePlayerPos_) {
                player->SetTranslate(normalPullBasePlayerPos_);
                player->UpdateLocalMatrix();
                player->UpdateWorldMatrix();
            }

            targetEnemy_->SetScale(enemyBaseScale_);
            player->SetCarriedEnemy(targetEnemy_);
            player->ChangeState(std::make_unique<PlayerStateIdle>());
            return;
        }

        Object3d* marker = player->GetHookMarker();
        if (marker) {
            Vector3 enemyCurrentPos = targetEnemy_->GetTransform()->translate;
            Vector3 diff = enemyCurrentPos - playerPos;
            marker->SetTranslate({playerPos.x + diff.x * 0.5f, playerPos.y + diff.y * 0.5f,
                                                 playerPos.z + diff.z * 0.5f});
            float angleY = std::atan2(diff.x, diff.z);
            float angleX = std::atan2(-diff.y, std::sqrt(diff.x * diff.x + diff.z * diff.z));
            marker->SetEulerAngles({angleX, angleY, 0.0f});
            marker->SetQuaternionMaster(false);
            // 手元に近づくにつれて太く戻る（イージングを利用）
            float len = Math::Length(diff);
            float thickness = Math::Lerp(0.12f, 0.22f, easeT);
            // 引っ張る反動でブルンブルン震える（近づくと収まる）
            float wobble = std::sin(pullTimer_ * 58.0f) * (1.0f - easeT) * 0.050f;
            float tensionPulse = std::abs(std::sin(pullTimer_ * 44.0f)) * (1.0f - easeT) * 0.040f;
            marker->SetScale({(std::max)(0.09f, thickness + wobble + tensionPulse),
                                             (std::max)(0.09f, thickness - wobble * 0.55f),
                                             len * (1.0f + tensionPulse * 0.35f)});
            marker->SetColor({0.10f, 0.82f, 1.0f, 1.0f});

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
                    {
                        targetEnemy_->SetScale(enemyBaseScale_);
                        targetEnemy_->SetEulerAngles(enemyBaseRotation_);
                        targetEnemy_->SetQuaternion(enemyBaseQuaternion_);
                        targetEnemy_->SetQuaternionMaster(enemyBaseQuaternionMaster_);
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
        } else if (hasNormalPullBasePlayerPos_) {
            player->SetTranslate(normalPullBasePlayerPos_);
            player->UpdateLocalMatrix();
            player->UpdateWorldMatrix();
        }

        Object3d* marker = player->GetHookMarker();
        if (marker) {
            marker->SetIsVisible(false);
            marker->SetScale({1.0f, 1.0f, 1.0f});
            marker->SetEulerAngles({0.0f, 0.0f, 0.0f});
            marker->SetQuaternionMaster(true); // 回転モードの復帰
            marker->UpdateLocalMatrix();
            marker->UpdateWorldMatrix();
        }
    }
}
