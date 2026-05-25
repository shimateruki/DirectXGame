#include "BossAttack5_Humanoid.h"
#include "../BossCore.h"
#include "./easing.h"
#include <algorithm>
#include <cmath>
#include <numbers>

void BossAttack5_Humanoid::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    blockStartPos_.clear();
    blockTargetPos_.clear();
    blockStartScale_.clear();
    blockTargetScale_.clear();

    animPhase_ = 50;

    // 攻撃力を適用
    for (auto* block : boss->GetArmorBlocks()) {
        if (block) {
            block->SetAttackDamage(boss->GetAttackParams().damageHumanoid);
        }
    }
}

void BossAttack5_Humanoid::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();
    Object3d* target = boss->GetTarget();

    // --- Phase 50: ボスコアが画面の端（X = -50）まで行く ---
    if (animPhase_ == 50) {
        if (animTimer_ == 0.0f) {
            animStartPos_ = boss->GetTranslate();
        }
        animTimer_ += deltaTime;
        float duration = 2.0f; // 2秒かけて移動
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        Vector3 pos = boss->GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, -50.0f, easeT); // 左端へ
        pos.y = Math::Lerp(animStartPos_.y, 24.0f, easeT);
        boss->SetTranslate(pos);

        if (target) {
            Vector3 toPlayer = target->GetWorldPosition() - boss->GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) + std::numbers::pi_v<float>;
            boss->SetRotation({ boss->GetRotation().x, angleY, boss->GetRotation().z });
            boss->GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 51;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 51: ボスのブロックが人型に変形する ---
    else if (animPhase_ == 51) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            blockTargetPos_.clear();
            blockStartScale_.clear();
            blockTargetScale_.clear();

            animStartRot_ = boss->GetRotation();

            // ==========================================
            // 翼用の新しい角度を追加
            // ==========================================
            float rotZ90 = std::numbers::pi_v<float> / 2.0f; // 90度のラジアン値
            float rotZ30 = std::numbers::pi_v<float> / 6.0f; // 30度のラジアン値
            float rotZ_30 = -std::numbers::pi_v<float> / 6.0f; // -30度のラジアン値

            struct BlockSetting { Vector3 translate; Vector3 scale; Vector3 rotation; };
            std::vector<BlockSetting> settings = {
                // --- 基本の6パーツ（今まで通り：基本の人型） ---
                { { -5.0f, -22.5f,  6.0f }, {  2.0f,  4.0f,  5.0f }, { 0.0f, 0.0f, 0.0f } },   // Block0(左足)
                { {  0.0f,  21.5f,  6.0f }, { 10.0f, 10.0f,  5.0f }, { 0.0f, 0.0f, 0.0f } },   // Block1(頭)
                { {  0.0f,  -3.5f,  6.0f }, {  8.0f, 15.0f,  5.0f }, { 0.0f, 0.0f, 0.0f } },   // Block2(胴体)
                { { 11.0f,   0.0f,  6.0f }, {  7.0f,  3.0f,  5.0f }, { 0.0f, 0.0f, rotZ90 } }, // Block3(右腕)
                { {-11.0f,   0.0f,  6.0f }, {  7.0f,  3.0f,  5.0f }, { 0.0f, 0.0f, rotZ90 } }, // Block4(左腕)
                { {  5.0f, -22.5f,  6.0f }, {  2.0f,  4.0f,  5.0f }, { 0.0f, 0.0f, 0.0f } },   // Block5(右足)

                // ==========================================
                // 7〜10個目の強化パーツ（悪魔のような巨大な4枚羽）
                // ==========================================
                { { 16.0f,  12.0f,  4.0f }, { 12.0f,  3.0f,  3.0f }, { 0.0f, 0.0f, rotZ30 } },  // Block6(右上の翼)
                { {-16.0f,  12.0f,  4.0f }, { 12.0f,  3.0f,  3.0f }, { 0.0f, 0.0f, rotZ_30 } }, // Block7(左上の翼)
                { { 14.0f, -14.0f,  4.0f }, { 10.0f,  2.0f,  3.0f }, { 0.0f, 0.0f, -rotZ30 } }, // Block8(右下の翼)
                { {-14.0f, -14.0f,  4.0f }, { 10.0f,  2.0f,  3.0f }, { 0.0f, 0.0f, -rotZ_30 } } // Block9(左下の翼)
            };

            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                armorBlocks[i]->SetParent(boss); // 親をbossに
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks[i]->GetScale());

                if (i < settings.size()) {
                    blockTargetPos_.push_back(settings[i].translate);
                    blockTargetScale_.push_back(settings[i].scale);
                    armorBlocks[i]->SetRotation(settings[i].rotation);
                    armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
                }
                else {
                    blockTargetPos_.push_back({ 0.0f, 0.0f, 0.0f });
                    blockTargetScale_.push_back({ 0.0f, 0.0f, 0.0f });
                }
            }
        }

        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 targetP = {
                    blockTargetPos_[i].x * 0.2f,
                    blockTargetPos_[i].y * 0.2f,
                    blockTargetPos_[i].z * 0.2f
                };
                Vector3 pos = Math::Lerp(blockStartPos_[i], targetP, easeT);
                armorBlocks[i]->SetTranslate(pos);

                Vector3 targetS = {
                    blockTargetScale_[i].x * 0.2f,
                    blockTargetScale_[i].y * 0.2f,
                    blockTargetScale_[i].z * 0.2f
                };
                Vector3 scale = Math::Lerp(blockStartScale_[i], targetS, easeT);
                armorBlocks[i]->SetScale(scale);
            }
        }

        float spinCount = 3.0f;
        float totalAngle = spinCount * 2.0f * std::numbers::pi_v<float>;
        Vector3 rot = boss->GetRotation();
        rot.y = animStartRot_.y - totalAngle * (1.0f - easeT);
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        if (t >= 1.0f) {
            animPhase_ = 52;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 52: 倒れる前のタメ（巨大化） ---
    else if (animPhase_ == 52) {
        animTimer_ += deltaTime;
        float duration = 2.5f;
        float t = std::min(animTimer_ / duration, 1.0f);

        // ==========================================
        // プレイヤーをゆっくり追いかけてプレッシャーを与える
        // ==========================================
        if (target) {
            Vector3 targetPos = target->GetWorldPosition();
            Vector3 currentPos = boss->GetTranslate();

            // フェーズの最後(t > 0.8)で歩行のブレを収束させ、綺麗に倒れ込めるようにする
            float fade = 1.0f;
            if (t > 0.8f) fade = (1.0f - t) / 0.2f;

            // 歩行サイクル（ドスッ、ドスッというリズム）
            float walkCycle = std::abs(std::sin(animTimer_ * 6.0f)); 

            // 追いかける速度（少し速くして、足を踏み出した時に加速する）
            float baseMoveSpeed = 16.0f * deltaTime;
            float moveSpeed = baseMoveSpeed * (walkCycle + 0.5f);
            
            Vector3 dir = { targetPos.x - currentPos.x, 0.0f, targetPos.z - currentPos.z };
            float length = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            if (length > 0.1f) {
                dir.x /= length;
                dir.z /= length;
                currentPos.x += dir.x * moveSpeed;
                currentPos.z += dir.z * moveSpeed;

                // 左右の揺れ（巨体がのっしのっし歩く横揺れ）
                float sway = std::sin(animTimer_ * 3.0f) * 10.0f * fade; 
                currentPos.x += dir.z * sway * deltaTime; 
                currentPos.z += -dir.x * sway * deltaTime;
            }

            // 歩くたびにY軸がバウンドする（最後は24.0fに着地）
            currentPos.y = 24.0f + walkCycle * 4.0f * fade;

            boss->SetTranslate(currentPos);

            // 常にプレイヤーの方向を向き直す
            float angleY = std::atan2(dir.x, dir.z) + std::numbers::pi_v<float>;
            
            auto LerpAngle = [](float a, float b, float t) {
                float diff = b - a;
                while (diff < -std::numbers::pi_v<float>) diff += 2.0f * std::numbers::pi_v<float>;
                while (diff > std::numbers::pi_v<float>) diff -= 2.0f * std::numbers::pi_v<float>;
                return a + diff * t;
            };
            
            float newRotY = LerpAngle(boss->GetRotation().y, angleY, 5.0f * deltaTime);
            boss->SetRotation({ boss->GetRotation().x, newRotY, boss->GetRotation().z });
            boss->GetTransform()->isQuaternionMaster = false;
        }

        float currentOverallScale = 1.0f;

        if (animTimer_ < 0.15f) {
            currentOverallScale = 0.2f;
        }
        else if (animTimer_ < 0.3f) {
            currentOverallScale = 1.2f;
        }
        else if (animTimer_ < 0.45f) {
            currentOverallScale = 0.5f;
        }
        else if (animTimer_ < 0.6f) {
            currentOverallScale = 1.1f;
        }
        else if (animTimer_ < 0.75f) {
            currentOverallScale = 0.8f;
        }
        else {
            currentOverallScale = 1.0f;
        }

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockTargetPos_.size()) {
                Vector3 pos = { blockTargetPos_[i].x * currentOverallScale, blockTargetPos_[i].y * currentOverallScale, blockTargetPos_[i].z * currentOverallScale };
                armorBlocks[i]->SetTranslate(pos);

                Vector3 scale = { blockTargetScale_[i].x * currentOverallScale, blockTargetScale_[i].y * currentOverallScale, blockTargetScale_[i].z * currentOverallScale };
                armorBlocks[i]->SetScale(scale);
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 53;
            animTimer_ = 0.0f;
            animStartRot_ = boss->GetRotation();
            animStartPos_ = boss->GetTranslate();
            // サンドイッチ対策：叩きつける瞬間だけ地形判定を消す
            for (auto* block : armorBlocks) {
                if (block) block->SetCollisionAttribute(kEnemyAttack);
            }
        }
    }
    // --- Phase 53: 前にぶっ倒れて叩き潰す ---
    else if (animPhase_ == 53) {
        animTimer_ += deltaTime;
        float duration = 2.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = std::pow(t, 3.0f);

        float overallScale = 1.0f;
        float pivotY = -24.5f * overallScale;
        float pivotZ = 8.5f * overallScale;

        float currentRotX = Math::Lerp(animStartRot_.x, -90.0f * (std::numbers::pi_v<float> / 180.0f), easeT);

        Vector3 rot = animStartRot_;
        rot.x = currentRotX;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        float dirX = std::sin(animStartRot_.y);
        float dirZ = std::cos(animStartRot_.y);

        Vector3 pivotWorldPos;
        pivotWorldPos.x = animStartPos_.x + dirX * pivotZ;
        pivotWorldPos.y = animStartPos_.y + pivotY;
        pivotWorldPos.z = animStartPos_.z + dirZ * pivotZ;

        float rotLocalY = pivotY * std::cos(currentRotX) - pivotZ * std::sin(currentRotX);
        float rotLocalZ = pivotY * std::sin(currentRotX) + pivotZ * std::cos(currentRotX);

        Vector3 newPos;
        newPos.x = pivotWorldPos.x - dirX * rotLocalZ;
        newPos.y = pivotWorldPos.y - rotLocalY;
        newPos.z = pivotWorldPos.z - dirZ * rotLocalZ;

        // ==========================================
        // 足がもつれて前に滑りながらこける挙動
        // ==========================================
        float slideDistance = 15.0f; // 前に滑り込む距離
        newPos.x += dirX * slideDistance * easeT;
        newPos.z += dirZ * slideDistance * easeT;

        // 倒れ込んだ時に地面にめり込まないようにY軸を補正
        // ブロックの前面(Z=3.5f)が地面(0.5f)にぴったりつくように調整
        float buryOffset = 6.5f; 
        newPos.y += buryOffset * easeT;

        boss->SetTranslate(newPos);

        float startArmAngle = std::numbers::pi_v<float> / 2.0f;
        float armOffset45 = std::numbers::pi_v<float> / 4.0f;

        float targetArmAngleRight = (3.0f * std::numbers::pi_v<float> / 2.0f) - armOffset45;
        float currentArmAngleRight = Math::Lerp(startArmAngle, targetArmAngleRight, easeT);

        float targetArmAngleLeft = (-std::numbers::pi_v<float> / 2.0f) + armOffset45;
        float currentArmAngleLeft = Math::Lerp(startArmAngle, targetArmAngleLeft, easeT);

        float armHalfLength = 3.5f;
        float shoulderY = 3.5f;

        if (armorBlocks.size() > 3 && armorBlocks[3]) {
            Vector3 pos = armorBlocks[3]->GetTranslate();
            pos.x = 11.0f - armHalfLength * std::cos(currentArmAngleRight);
            pos.y = shoulderY - armHalfLength * std::sin(currentArmAngleRight);
            armorBlocks[3]->SetTranslate(pos);
            armorBlocks[3]->SetRotation({ 0.0f, 0.0f, currentArmAngleRight });
            armorBlocks[3]->GetTransform()->isQuaternionMaster = false;
        }

        if (armorBlocks.size() > 4 && armorBlocks[4]) {
            Vector3 pos = armorBlocks[4]->GetTranslate();
            pos.x = -11.0f - armHalfLength * std::cos(currentArmAngleLeft);
            pos.y = shoulderY - armHalfLength * std::sin(currentArmAngleLeft);
            armorBlocks[4]->SetTranslate(pos);
            armorBlocks[4]->SetRotation({ 0.0f, 0.0f, currentArmAngleLeft });
            armorBlocks[4]->GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 54;
            animTimer_ = 0.0f;
    
        }
    }
    // --- Phase 54: 倒れたまま待機（攻撃チャンス） ---
    else if (animPhase_ == 54) {
        animTimer_ += deltaTime;
        if (animTimer_ >= 3.0f) {
            animPhase_ = 55;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 55: 起き上がって復帰 ---
    else if (animPhase_ == 55) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            blockStartScale_.clear();
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks[i]->GetScale());
            }
            animStartRot_ = boss->GetRotation();
            animStartPos_ = boss->GetTranslate(); // 倒れた位置を記録してワープを防ぐ
            // 地面判定を復活させる
            for (auto* block : armorBlocks) {
                if (block) block->SetCollisionAttribute(kEnemyAttack | kGround);
            }
        }

        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        float overallScale = 1.0f;
        float pivotY = -24.5f * overallScale;
        float pivotZ = 8.5f * overallScale;
        float dirX = std::sin(animStartRot_.y);
        float dirZ = std::cos(animStartRot_.y);

        float startRotLocalY = pivotY * std::cos(animStartRot_.x) - pivotZ * std::sin(animStartRot_.x);
        float startRotLocalZ = pivotY * std::sin(animStartRot_.x) + pivotZ * std::cos(animStartRot_.x);
        Vector3 startToeWorld = {
            animStartPos_.x + dirX * startRotLocalZ,
            animStartPos_.y + startRotLocalY,
            animStartPos_.z + dirZ * startRotLocalZ
        };

        Vector3 targetToeWorld = {
            0.0f + dirX * pivotZ,
            4.0f + pivotY,
            0.0f + dirZ * pivotZ
        };

        Vector3 currentToeWorld = {
            Math::Lerp(startToeWorld.x, targetToeWorld.x, easeT),
            Math::Lerp(startToeWorld.y, targetToeWorld.y, easeT),
            Math::Lerp(startToeWorld.z, targetToeWorld.z, easeT)
        };

        float currentRotX = Math::Lerp(animStartRot_.x, 0.0f, easeT);
        Vector3 bossRot = animStartRot_;
        bossRot.x = currentRotX;
        boss->SetRotation(bossRot);
        boss->GetTransform()->isQuaternionMaster = false;

        float currentRotLocalY = pivotY * std::cos(currentRotX) - pivotZ * std::sin(currentRotX);
        float currentRotLocalZ = pivotY * std::sin(currentRotX) + pivotZ * std::cos(currentRotX);

        Vector3 newPos = {
            currentToeWorld.x - dirX * currentRotLocalZ,
            currentToeWorld.y - currentRotLocalY,
            currentToeWorld.z - dirZ * currentRotLocalZ
        };
        boss->SetTranslate(newPos);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size()) {
                BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
                Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks[i]->SetTranslate(pos);

                Vector3 scale = Math::Lerp(blockStartScale_[i], orbit.scale, easeT);
                armorBlocks[i]->SetScale(scale);

                armorBlocks[i]->SetRotation(orbit.rot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            isFinished_ = true; // これで完了
        }
    }
}
