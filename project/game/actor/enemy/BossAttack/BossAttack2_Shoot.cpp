#include "BossAttack2_Shoot.h"
#include "../BossCore.h"
#include "AudioPlayer.h"
#include "./easing.h"
#include <algorithm>
#include <cmath>
#include <numbers>

void BossAttack2_Shoot::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    blockStartPos_.clear();
    blockTargetPos_.clear();
    shotCount_ = 0;
    descendTimer_ = 0.0f;

    animStartPos_ = boss->GetTranslate();
    animPhase_ = 10;
}

void BossAttack2_Shoot::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();
    Object3d* target = boss->GetTarget();

    // --- Phase 10: X = 50.0f へ移動 ---
    if (animPhase_ == 10) {
        animTimer_ += deltaTime;
        float t = std::min(animTimer_ / 2.5f, 1.0f);

        Vector3 pos = boss->GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, 50.0f, Easing::OutExpo(t));
        pos.y = Math::Lerp(animStartPos_.y, animStartPos_.y + 8.0f, Easing::OutExpo(t)); // Y座標も滑らかに持ち上げる
        boss->SetTranslate(pos);

        if (target) {
            Vector3 toPlayer = target->GetWorldPosition() - boss->GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);
            boss->SetRotation({ boss->GetRotation().x, angleY, boss->GetRotation().z });
            boss->GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 11;
            animTimer_ = 0.0f;
            AudioPlayer::GetInstance()->PlaySE(boss->GetSEBossAttack2OpenHandle(), false, 2.0f);

            blockStartPos_.clear();
            blockTargetPos_.clear();

            // ==========================================
            // 手書きの配置データをやめて、自動で綺麗な多角形（円陣）を作る
            // ==========================================
            float turnY = std::numbers::pi_v<float> / 2.0f;
            float radius = 4.0f; // 10角形の半径（迫力を出すために少し広めに設定）

            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());

                // 360度（2π）を現在のブロック数で割って、均等な角度を計算する
                // （10個なら36度ずつ、6個なら60度ずつズレる）
                float angle = (2.0f * std::numbers::pi_v<float> *i) / armorBlocks.size();

                // YZ平面（ボスの正面）に円を描くように座標をセット
                Vector3 targetPos = {
                    -2.0f,                    // X: ボスの少し手前
                    std::sin(angle) * radius, // Y: 上下位置
                    std::cos(angle) * radius  // Z: 左右位置
                };

                blockTargetPos_.push_back(targetPos);

                // スケールと向きも一緒にセット
                armorBlocks[i]->SetScale({ 1.5f, 1.5f, 1.5f });
                armorBlocks[i]->SetRotation({ 0.0f, turnY, 0.0f });
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }
    }
    // --- Phase 11: 射撃陣形へスライド移動 ---
    else if (animPhase_ == 11) {
        animTimer_ += deltaTime;
        float t = std::min(animTimer_ / 1.0f, 1.0f);
        float easeT = Easing::OutExpo(t);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                armorBlocks[i]->SetTranslate(pos);
            }
        }

        if (target) {
            Vector3 toPlayer = target->GetWorldPosition() - boss->GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);
            boss->SetRotation({ boss->GetRotation().x, angleY, boss->GetRotation().z });
            boss->GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 12;
            animTimer_ = 0.0f;
            shotCount_ = 0;
        }
    }
    // --- Phase 12: プレイヤーを向いて、1つずつ飛ばす ---
    else if (animPhase_ == 12) {
        animTimer_ += deltaTime;

        if (target) {
            Vector3 toPlayer = target->GetWorldPosition() - boss->GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);
            boss->SetRotation({ boss->GetRotation().x, angleY, boss->GetRotation().z });
            boss->GetTransform()->isQuaternionMaster = false;
        }

        float nextShotTime = shotCount_ * 0.5f;

        if (animTimer_ >= nextShotTime) {
            int idx = (int)armorBlocks.size() - 1 - shotCount_;
            if (idx >= 0 && idx < armorBlocks.size()) {
                Object3d* block = armorBlocks[idx];
                block->SetAttackDamage(boss->GetAttackParams().damageShoot);

                Vector3 bossPos = boss->GetTranslate();
                float bossRotY = boss->GetRotation().y;
                Vector3 localPos = block->GetTranslate();

                Vector3 worldPos;
                worldPos.x = bossPos.x + (localPos.x * std::cos(bossRotY) + localPos.z * std::sin(bossRotY));
                worldPos.y = bossPos.y + localPos.y;
                worldPos.z = bossPos.z + (-localPos.x * std::sin(bossRotY) + localPos.z * std::cos(bossRotY));

                block->SetParent(nullptr);
                block->SetTranslate(worldPos);
                block->SetCollisionAttribute(0);

                Vector3 currentRot = block->GetRotation();
                block->GetTransform()->isQuaternionMaster = false;

                // ゲッター経由で FlyingBlocks に追加
                boss->GetFlyingBlocks().push_back({ block, {0.0f, 0.0f, 0.0f}, currentRot, 4, idx });
                
                AudioPlayer::GetInstance()->PlaySE(boss->GetSEBossAttack2LaunchHandle(), false, 1.0f);
            }

            shotCount_++;

            if (shotCount_ >= armorBlocks.size()) {
                animPhase_ = 13;
                animTimer_ = 0.0f;
            }
        }
    }
    // --- Phase 13: 待機 ---
    else if (animPhase_ == 13) {
        // 徐々に下に下がる処理
        descendTimer_ += deltaTime;
        float descendDuration = 4.0f; // 4秒かけて元の高さへ下降
        float t = std::min(descendTimer_ / descendDuration, 1.0f);
        float easeT = Easing::InOutQuad(t);

        Vector3 pos = boss->GetTranslate();
        pos.y = Math::Lerp(animStartPos_.y + 8.0f, animStartPos_.y, easeT);
        boss->SetTranslate(pos);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            bool isFlying = false;
            for (auto& fb : boss->GetFlyingBlocks()) {
                if (fb.originalIndex == i) { isFlying = true; break; }
            }
            if (!isFlying) {
                BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
                armorBlocks[i]->SetTranslate(orbit.pos);
                armorBlocks[i]->SetScale(orbit.scale);
                armorBlocks[i]->SetRotation(orbit.rot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        // 弾が全部戻ってきたら攻撃終了
        if (boss->GetFlyingBlocks().empty()) {
            animTimer_ += deltaTime;
            if (animTimer_ >= 1.0f) {
                isFinished_ = true; // これで完了
            }
        }
    }
}
