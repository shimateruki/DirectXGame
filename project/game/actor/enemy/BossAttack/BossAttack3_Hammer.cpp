#include "BossAttack3_Hammer.h"
#include "../BossCore.h"
#include "./easing.h" // ※環境に合わせてパスを調整してください
#include <algorithm>
#include <cmath>
#include <numbers>

void BossAttack3_Hammer::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);
    blockStartPos_.clear();

    animPhase_ = 20; // フェーズ20からスタート
}

void BossAttack3_Hammer::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();
    Object3d* target = boss->GetTarget();
    Object3d* warningArea = boss->GetWarningArea(); // 予兆エリアを取得

    // --- Phase 20: 瞬時にハンマー形態へ変形 ---
    if (animPhase_ == 20) {
        if (animTimer_ == 0.0f) {
            struct HammerSetting {
                Vector3 translate; Vector3 scale; Vector3 rotation;
            };
            std::vector<HammerSetting> hammerSettings = {
                // --- 基本の6パーツ（今まで通り） ---
                { {  0.000f,  4.000f,  0.000f }, { 1.500f, 1.000f, 1.000f }, { 0.0f, 0.0f, 0.0f } }, // 頭の中心
                { {  2.000f,  4.000f,  0.000f }, { 0.700f, 1.500f, 1.000f }, { 0.0f, 0.0f, 0.0f } }, // 頭の右側
                { {  0.000f,  1.000f,  0.000f }, { 0.400f, 2.200f, 0.400f }, { 0.0f, 0.0f, 0.0f } }, // 柄(上)
                { { -2.000f,  4.000f,  0.000f }, { 0.700f, 1.500f, 1.000f }, { 0.0f, 0.0f, 0.0f } }, // 頭の左側
                { {  0.000f, -1.800f, -0.002f }, { 0.500f, 0.500f, 0.800f }, { 0.0f, 0.0f, 0.0f } }, // 柄(下)
                { {  0.000f,  5.100f,  0.000f }, { 0.500f, 0.250f, 0.500f }, { 0.0f, 0.0f, 0.0f } }, // 頭の上のポッチ

                // ==========================================
                // ★ 新規追加：7〜10個目の強化パーツ（超巨大ハンマー化！）
                // ==========================================
                { {  3.000f,  4.000f,  0.000f }, { 0.500f, 2.000f, 1.200f }, { 0.0f, 0.0f, 0.0f } }, // さらに右側の巨大な打撃面
                { { -3.000f,  4.000f,  0.000f }, { 0.500f, 2.000f, 1.200f }, { 0.0f, 0.0f, 0.0f } }, // さらに左側の巨大な打撃面
                { {  0.000f,  6.000f,  0.000f }, { 0.300f, 1.000f, 0.300f }, { 0.0f, 0.0f, 0.0f } }, // 上に鋭く伸びるトゲ
                { {  0.000f, -3.000f,  0.000f }, { 0.600f, 0.800f, 0.800f }, { 0.0f, 0.0f, 0.0f } }  // 柄の底の重り（ポンメル）
            };
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (i < hammerSettings.size()) {
                    armorBlocks[i]->SetTranslate(hammerSettings[i].translate);
                    armorBlocks[i]->SetScale(hammerSettings[i].scale);
                    armorBlocks[i]->SetRotation(hammerSettings[i].rotation);
                    armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
                }
            }
        }

        animTimer_ += deltaTime;

        if (target) {
            Vector3 toPlayer = target->GetWorldPosition() - boss->GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);
            boss->SetRotation({ 0.0f, angleY, 0.0f });
            boss->GetTransform()->isQuaternionMaster = false;
        }

        if (animTimer_ >= 1.5f) {
            animPhase_ = 21;
            animTimer_ = 0.0f;

            if (target) { animTargetPos_ = target->GetWorldPosition(); }
            else { animTargetPos_ = boss->GetTranslate(); }

            if (warningArea) {
                if (armorBlocks.size() > 1 && armorBlocks[1]) {
                    Vector3 block2Scale = armorBlocks[1]->GetScale();
                    warningArea->SetScale({ block2Scale.x, 2.0f, block2Scale.z });
                }
                warningArea->SetTranslate({ animTargetPos_.x, 1.0f, animTargetPos_.z });

                float finalRotY = boss->GetRotation().y + (std::numbers::pi_v<float> / 2.0f);
                warningArea->SetRotation({ 0.0f, finalRotY, 0.0f });
                warningArea->GetTransform()->isQuaternionMaster = false;
                warningArea->SetColor({ 1.0f, 1.0f, 0.0f, 0.9f });
            }
        }
    }
    // --- Phase 21: ロックオンした位置へ移動 ＆ 振りかぶる！ ---
    else if (animPhase_ == 21) {
        animTimer_ += deltaTime;

        float moveDuration = 4.5f;
        float moveT = std::min(animTimer_ / moveDuration, 1.0f);

        if (warningArea) {
            float currentGreen = Math::Lerp(1.0f, 0.0f, moveT);
            warningArea->SetColor({ 1.0f, currentGreen, 0.0f, 0.9f });
        }

        Vector3 targetPos = animTargetPos_;
        Vector3 currentPos = boss->GetTranslate();
        Vector3 toBoss = currentPos - targetPos;
        toBoss.y = 0.0f;
        float dist = std::sqrt(toBoss.x * toBoss.x + toBoss.z * toBoss.z);
        if (dist > 0.0f) { toBoss.x /= dist; toBoss.z /= dist; }

        Vector3 targetHoverPos = { targetPos.x + toBoss.x * 4.5f, 4.0f, targetPos.z + toBoss.z * 4.5f };

        //Vector3 targetHoverPos = { targetPos.x + toBoss.x * 4.5f, targetPos.y + 1.0f, targetPos.z + toBoss.z * 4.5f };
        float easeT = moveT;
        currentPos.x = Math::Lerp(currentPos.x, targetHoverPos.x, easeT);
        currentPos.y = Math::Lerp(currentPos.y, targetHoverPos.y, easeT);
        currentPos.z = Math::Lerp(currentPos.z, targetHoverPos.z, easeT);
        boss->SetTranslate(currentPos);

        float rotT = std::min(animTimer_ / 3.0f, 1.0f);
        Vector3 toPlayer = targetPos - currentPos;
        float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);

        float elasticT = 0.0f;
        if (rotT == 0.0f) elasticT = 0.0f;
        else if (rotT == 1.0f) elasticT = 1.0f;
        else {
            float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;
            elasticT = -std::pow(2.0f, 10.0f * rotT - 10.0f) * std::sin((rotT * 10.0f - 10.75f) * c4);
        }

        float targetTilt = -70.0f * (std::numbers::pi_v<float> / 180.0f);
        float tiltBack = Math::Lerp(0.0f, targetTilt, elasticT);
        boss->SetRotation({ 0.0f, angleY, tiltBack });
        boss->GetTransform()->isQuaternionMaster = false;

        if (moveT >= 1.0f) {
            animPhase_ = 22;
            animTimer_ = 0.0f;
            // ★ サンドイッチ対策：叩きつける瞬間だけ地形判定を消す
            for (auto* block : armorBlocks) {
                if (block) block->SetCollisionAttribute(kEnemyAttack);
            }
        }
    }
    // --- Phase 22: 一気に振り下ろして叩き潰す！ ---
    else if (animPhase_ == 22) {
        animTimer_ += deltaTime;
        float smashDuration = 0.15f;
        float t = std::min(animTimer_ / smashDuration, 1.0f);

        if (warningArea) {
            warningArea->SetColor({ 1.0f, 0.0f, 0.0f, 0.9f });
        }

        float startRotZ = -70.0f * (std::numbers::pi_v<float> / 180.0f);
        float endRotZ = 270.0f * (std::numbers::pi_v<float> / 180.0f);
        float currentRotZ = Math::Lerp(startRotZ, endRotZ, std::pow(t, 3.0f));
        boss->SetRotation({ 0.0f, boss->GetRotation().y, currentRotZ });

        if (t >= 1.0f) {
            boss->SetRotation({ 0.0f, boss->GetRotation().y, endRotZ });
            if (warningArea) { warningArea->SetScale({ 0.0f, 0.0f, 0.0f }); }
            animPhase_ = 23;
            animTimer_ = 0.0f;
            // ★ 地面判定を復活させる
            for (auto* block : armorBlocks) {
                if (block) block->SetCollisionAttribute(kEnemyAttack | kGround);
            }
        }
    }
    // --- Phase 23: 地面に倒れたまま3秒待機 ---
    else if (animPhase_ == 23) {
        if (animTimer_ == 0.0f) {
            animStartRot_ = boss->GetRotation();
        }
        animTimer_ += deltaTime;
        if (animTimer_ >= 3.0f) {
            animPhase_ = 24;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 24: 待機軌道に向かって復帰する ---
    else if (animPhase_ == 24) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());
            }
        }

        animTimer_ += deltaTime;
        float duration = 1.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        Vector3 bossPos = boss->GetTranslate();
        bossPos.y = Math::Lerp(bossPos.y, 4.0f, easeT);
        boss->SetTranslate(bossPos);

        Vector3 currentRot;
        currentRot.x = Math::Lerp(animStartRot_.x, 0.0f, easeT);
        currentRot.y = Math::Lerp(animStartRot_.y, 0.0f, easeT);
        currentRot.z = Math::Lerp(animStartRot_.z, 0.0f, easeT);
        boss->SetRotation(currentRot);
        boss->GetTransform()->isQuaternionMaster = false;

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size()) {
                BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
                Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks[i]->SetTranslate(pos);
                armorBlocks[i]->SetScale(orbit.scale);
                armorBlocks[i]->SetRotation(orbit.rot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            isFinished_ = true; // 完了！
        }
    }
}