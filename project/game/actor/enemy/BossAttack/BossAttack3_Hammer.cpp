#include "BossAttack3_Hammer.h"
#include "../BossCore.h"
#include "./easing.h" // ※環境に合わせてパスを調整してください
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
    struct HammerSetting {
        Vector3 translate; Vector3 scale; Vector3 rotation;
    };
    const std::vector<HammerSetting> kHammerSettings = {
        { {  0.000f,  4.000f,  0.000f }, { 1.500f, 1.000f, 1.000f }, { 0.0f, 0.0f, 0.0f } },
        { {  2.000f,  4.000f,  0.000f }, { 0.700f, 1.500f, 1.000f }, { 0.0f, 0.0f, 0.0f } },
        { {  0.000f,  1.000f,  0.000f }, { 0.400f, 2.200f, 0.400f }, { 0.0f, 0.0f, 0.0f } },
        { { -2.000f,  4.000f,  0.000f }, { 0.700f, 1.500f, 1.000f }, { 0.0f, 0.0f, 0.0f } },
        { {  0.000f, -1.800f, -0.002f }, { 0.500f, 0.500f, 0.800f }, { 0.0f, 0.0f, 0.0f } },
        { {  0.000f,  5.100f,  0.000f }, { 0.500f, 0.250f, 0.500f }, { 0.0f, 0.0f, 0.0f } },
        { {  3.000f,  4.000f,  0.000f }, { 0.500f, 2.000f, 1.200f }, { 0.0f, 0.0f, 0.0f } },
        { { -3.000f,  4.000f,  0.000f }, { 0.500f, 2.000f, 1.200f }, { 0.0f, 0.0f, 0.0f } },
        { {  0.000f,  6.000f,  0.000f }, { 0.300f, 1.000f, 0.300f }, { 0.0f, 0.0f, 0.0f } },
        { {  0.000f, -3.000f,  0.000f }, { 0.600f, 0.800f, 0.800f }, { 0.0f, 0.0f, 0.0f } } 
    };

    float LerpAngle(float a, float b, float t) {
        float diff = b - a;
        while (diff < -std::numbers::pi_v<float>) diff += 2.0f * std::numbers::pi_v<float>;
        while (diff > std::numbers::pi_v<float>) diff -= 2.0f * std::numbers::pi_v<float>;
        return a + diff * t;
    }
}

void BossAttack3_Hammer::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);
    blockStartPos_.clear();

    animPhase_ = 20; // フェーズ20からスタート
    attackCount_ = 0; // コンボ回数をリセット
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
            animStartRot_ = boss->GetRotation();
            animStartPos_ = boss->GetTranslate();

            if (target) { 
                animTargetPos_ = target->GetWorldPosition(); 
                Vector3 toP = animTargetPos_ - animStartPos_;
                toP.y = 0.0f;
                float d = std::sqrt(toP.x*toP.x + toP.z*toP.z);
                if (d > 0.01f) { attackDir_ = { toP.x/d, 0.0f, toP.z/d }; }
                else { attackDir_ = { 0.0f, 0.0f, 1.0f }; }
            } else { 
                animTargetPos_ = boss->GetTranslate(); 
                attackDir_ = { 0.0f, 0.0f, 1.0f };
            }

            // 予兆エリアの設定を削除
        }
    }
    // --- Phase 21: 移動＆振りかぶり ---
    else if (animPhase_ == 21) {
        animTimer_ += deltaTime;

        float moveDuration = 1.5f;
        float targetHeight = 4.0f;
        float rotDuration = 1.5f;
        float distToKeep = 4.5f;

        if (attackCount_ == 0) { // ぶん回し
            targetHeight = 2.0f;  
            distToKeep = 1.5f;    
        } else if (attackCount_ == 1) { // 叩きつけ
            targetHeight = 4.0f;
            distToKeep = 4.5f;    
        } else if (attackCount_ == 2) { // 巨大叩きつけ
            moveDuration = 2.0f;
            targetHeight = 8.0f;  
            distToKeep = 6.0f;    
            rotDuration = 2.0f;
            
            // ハンマーを1.5倍に巨大化
            float scaleT = std::min(animTimer_ / 1.0f, 1.0f);
            float currentScale = Math::Lerp(1.0f, 1.5f, scaleT);
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (i < kHammerSettings.size() && armorBlocks[i]) {
                    armorBlocks[i]->SetScale({ kHammerSettings[i].scale.x * currentScale, kHammerSettings[i].scale.y * currentScale, kHammerSettings[i].scale.z * currentScale });
                    armorBlocks[i]->SetTranslate({ kHammerSettings[i].translate.x * currentScale, kHammerSettings[i].translate.y * currentScale, kHammerSettings[i].translate.z * currentScale });
                }
            }
        }

        float moveT = std::min(animTimer_ / moveDuration, 1.0f);
        
        Vector3 targetHoverPos = { animTargetPos_.x - attackDir_.x * distToKeep, targetHeight, animTargetPos_.z - attackDir_.z * distToKeep };
        
        float easeT = std::pow(moveT, 2.0f); // Ease In
        Vector3 currentPos;
        currentPos.x = Math::Lerp(animStartPos_.x, targetHoverPos.x, easeT);
        currentPos.y = Math::Lerp(animStartPos_.y, targetHoverPos.y, easeT);
        currentPos.z = Math::Lerp(animStartPos_.z, targetHoverPos.z, easeT);
        boss->SetTranslate(currentPos);

        float rotT = std::min(animTimer_ / rotDuration, 1.0f);
        float elasticT = 0.0f;
        if (rotT == 0.0f) elasticT = 0.0f;
        else if (rotT == 1.0f) elasticT = 1.0f;
        else {
            float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;
            elasticT = -std::pow(2.0f, 10.0f * rotT - 10.0f) * std::sin((rotT * 10.0f - 10.75f) * c4);
        }

        float angleY = std::atan2(attackDir_.x, attackDir_.z) - (std::numbers::pi_v<float> / 2.0f);

        if (attackCount_ == 0) {
            // ぶん回し: ハンマーを寝かせる (-90度)
            float targetTilt = -90.0f * (std::numbers::pi_v<float> / 180.0f);
            float tiltBack = Math::Lerp(animStartRot_.z, targetTilt, elasticT);
            float targetRotY = angleY - 120.0f * (std::numbers::pi_v<float> / 180.0f); 
            float currentRotY = LerpAngle(animStartRot_.y, targetRotY, elasticT);
            boss->SetRotation({ 0.0f, currentRotY, tiltBack });
        } else {
            // 叩きつけ: ハンマーを後ろに傾ける (-70度)
            float targetTilt = -70.0f * (std::numbers::pi_v<float> / 180.0f);
            float tiltBack = Math::Lerp(animStartRot_.z, targetTilt, elasticT);
            float currentRotY = LerpAngle(animStartRot_.y, angleY, std::min(animTimer_ / 0.5f, 1.0f));
            boss->SetRotation({ 0.0f, currentRotY, tiltBack });
        }
        
        boss->GetTransform()->isQuaternionMaster = false;

        // 予兆エリアの設定を削除

        if (moveT >= 1.0f) {
            animPhase_ = 22;
            animTimer_ = 0.0f;
            animStartRot_ = boss->GetRotation(); 
            for (auto* block : armorBlocks) {
                if (block) {
                    block->SetCollisionAttribute(kEnemyAttack);
                    block->SetAttackDamage(boss->GetAttackParams().damageHammer);
                }
            }
        }
    }
    // --- Phase 22: 一気に振り下ろして叩き潰す！ ---
    else if (animPhase_ == 22) {
        animTimer_ += deltaTime;
        
        float smashDuration = 0.15f;
        if (attackCount_ == 0) smashDuration = 0.35f; // ぶん回し
        else if (attackCount_ == 2) smashDuration = 0.2f;

        float t = std::min(animTimer_ / smashDuration, 1.0f);

        // 予兆エリアの色設定を削除

        if (attackCount_ == 0) {
            // ぶん回し
            float startRotY = animStartRot_.y;
            float endRotY = startRotY + 360.0f * (std::numbers::pi_v<float> / 180.0f); 
            float currentRotY = startRotY + (endRotY - startRotY) * std::pow(t, 2.0f); // EaseIn
            boss->SetRotation({ 0.0f, currentRotY, animStartRot_.z });
        } else {
            // 叩きつけ
            float startRotZ = animStartRot_.z; 
            float endRotZ = 270.0f * (std::numbers::pi_v<float> / 180.0f);
            float currentRotZ = Math::Lerp(startRotZ, endRotZ, std::pow(t, 3.0f));
            
            // 沈み込み
            Vector3 bossPos = boss->GetTranslate();
            if (t > 0.8f) {
                bossPos.y = Math::Lerp(bossPos.y, 0.0f, std::pow(t, 3.0f));
                boss->SetTranslate(bossPos);
            }
            boss->SetRotation({ 0.0f, animStartRot_.y, currentRotZ });
        }

        if (t >= 1.0f) {
            if (attackCount_ == 0) {
                boss->SetRotation({ 0.0f, animStartRot_.y + 360.0f * (std::numbers::pi_v<float> / 180.0f), animStartRot_.z });
            } else {
                boss->SetRotation({ 0.0f, animStartRot_.y, 270.0f * (std::numbers::pi_v<float> / 180.0f) });
            }
            
            // 予兆エリアの非表示設定を削除（そもそも表示していないため）
            
            attackCount_++;

            if (attackCount_ < 3) {
                animPhase_ = 25; 
            } else {
                animPhase_ = 23; 
            }
            
            animTimer_ = 0.0f;
            animStartRot_ = boss->GetRotation(); 
            animStartPos_ = boss->GetTranslate();
            
            for (auto* block : armorBlocks) {
                if (block) block->SetCollisionAttribute(kEnemyAttack | kGround);
            }
        }
    }
    // --- Phase 25: 次の攻撃に向けてハンマーを引き抜く ---
    else if (animPhase_ == 25) {
        animTimer_ += deltaTime;
        float pullUpDuration = 0.6f; 
        float t = std::min(animTimer_ / pullUpDuration, 1.0f);
        float easeT = 1.0f - std::pow(1.0f - t, 3.0f); // EaseOutCubic

        // 叩きつけの後はZ軸を戻す、ぶん回しの後はそのままZを0に戻す
        float startRotZ = animStartRot_.z; 
        float endRotZ = 0.0f; 
        float currentRotZ = Math::Lerp(startRotZ, endRotZ, easeT);
        
        boss->SetRotation({ 0.0f, animStartRot_.y, currentRotZ });
        
        // 高さを少し戻す
        Vector3 bossPos = boss->GetTranslate();
        bossPos.y = Math::Lerp(animStartPos_.y, 4.0f, easeT);
        boss->SetTranslate(bossPos);

        if (t >= 1.0f) {
            animPhase_ = 21; 
            animTimer_ = 0.0f;
            animStartRot_ = boss->GetRotation(); 
            animStartPos_ = boss->GetTranslate();

            // ターゲットを再取得してホーミングし直す
            if (target) { 
                animTargetPos_ = target->GetWorldPosition(); 
                Vector3 dir = animTargetPos_ - animStartPos_;
                dir.y = 0.0f;
                float d = std::sqrt(dir.x*dir.x + dir.z*dir.z);
                if (d > 0.01f) { attackDir_ = { dir.x/d, 0.0f, dir.z/d }; }
                else { attackDir_ = { 0.0f, 0.0f, 1.0f }; }
            } else { 
                animTargetPos_ = boss->GetTranslate(); 
            }

            // 予兆エリアの色設定を削除
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