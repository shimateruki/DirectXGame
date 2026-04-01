#include "BossAttack6_Laser.h"
#include "../BossCore.h"
#include "./easing.h" // ※環境に合わせてパスを調整してください
#include <algorithm>
#include <cmath>
#include <numbers>

void BossAttack6_Laser::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    blockStartPos_.clear();
    blockTargetPos_.clear();
    blockStartScale_.clear();
    blockTargetScale_.clear();
    attentionStartRot_.clear();

    animPhase_ = 60;
}

void BossAttack6_Laser::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();

    float maxSpinSpeed = 8.0f;
    float fireSpinSpeed = 0.3f;

    // --- Phase 60: まずはコアが中央(0,0)へスゥーッと移動する ---
    if (animPhase_ == 60) {
        if (animTimer_ == 0.0f) {
            animStartPos_ = boss->GetTranslate();
        }

        animTimer_ += deltaTime;
        float duration = 2.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        Vector3 corePos = boss->GetTranslate();
        corePos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
        corePos.y = Math::Lerp(animStartPos_.y, 2.0f, easeT);
        corePos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        boss->SetTranslate(corePos);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
            armorBlocks[i]->SetTranslate(orbit.pos);
            armorBlocks[i]->SetScale(orbit.scale);
            armorBlocks[i]->SetRotation(orbit.rot);
            armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 61;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 61: 待機軌道のまま、コアがギュイィィンと回転し始める ---
    else if (animPhase_ == 61) {
        if (animTimer_ == 0.0f) {
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
                armorBlocks[i]->SetParent(boss);
                armorBlocks[i]->SetTranslate(orbit.pos);
                armorBlocks[i]->SetScale(orbit.scale);
                armorBlocks[i]->SetRotation(orbit.rot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        animTimer_ += deltaTime;
        float duration = 2.25f;
        float t = std::min(animTimer_ / duration, 1.0f);

        float currentSpinSpeed = Math::Lerp(0.0f, maxSpinSpeed, Easing::InSine(t));

        Vector3 rot = boss->GetRotation();
        rot.y += currentSpinSpeed * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        if (t >= 1.0f) {
            animPhase_ = 62;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 62: 大回転を維持したまま、砲台陣形へ変形！ ---
    else if (animPhase_ == 62) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            blockTargetPos_.clear();
            blockStartScale_.clear();
            blockTargetScale_.clear();
            attentionStartRot_.clear();

            float radius = 12.0f;

            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks[i]->GetScale());
                attentionStartRot_.push_back(armorBlocks[i]->GetRotation());

                float angle = (i * 2.0f * std::numbers::pi_v<float>) / armorBlocks.size();
                Vector3 targetPos = {
                    std::cos(angle) * radius,
                    0.0f,
                    std::sin(angle) * radius
                };
                blockTargetPos_.push_back(targetPos);
                blockTargetScale_.push_back({ 1.5f, 1.5f, 1.5f });
            }
        }

        animTimer_ += deltaTime;
        float duration = 3.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        Vector3 rot = boss->GetRotation();
        rot.y += maxSpinSpeed * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                armorBlocks[i]->SetTranslate(pos);

                Vector3 scale = Math::Lerp(blockStartScale_[i], blockTargetScale_[i], easeT);
                armorBlocks[i]->SetScale(scale);

                float angle = (i * 2.0f * std::numbers::pi_v<float>) / armorBlocks.size();
                Vector3 targetRot = { 0.0f, -angle, 0.0f };

                Vector3 currentRot;
                currentRot.x = Math::Lerp(attentionStartRot_[i].x, targetRot.x, easeT);
                currentRot.y = Math::Lerp(attentionStartRot_[i].y, targetRot.y, easeT);
                currentRot.z = Math::Lerp(attentionStartRot_[i].z, targetRot.z, easeT);

                armorBlocks[i]->SetRotation(currentRot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 63;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 63: 陣形完了後、0.5秒間完全に沈黙する！（予兆レーザー） ---
    else if (animPhase_ == 63) {
        if (animTimer_ == 0.0f) {
            for (Object3d* block : armorBlocks) {
                if (!block) continue;
                for (Object3d* child : block->GetChildren()) {
                    if (child->GetName().find("Beam_Cylinder") != std::string::npos) {
                        child->SetScale({ 0.1f, 80.0f, 0.1f });

                        float rotX90 = std::numbers::pi_v<float> / 2.0f;
                        child->SetRotation({ rotX90, 0.0f, 0.0f });
                        child->GetTransform()->isQuaternionMaster = false;

                        child->SetColor({ 1.0f, 0.0f, 0.0f, 0.5f });
                        child->SetCollisionAttribute(0);
                    }
                }
            }
        }

        animTimer_ += deltaTime;
        float stopDuration = 0.75f;

        if (animTimer_ >= stopDuration) {
            animPhase_ = 64;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 64: 陣形を維持したまま回転し、ビームを撃つ！ ---
    else if (animPhase_ == 64) {
        animTimer_ += deltaTime;
        float spinDuration = 7.5f;

        Vector3 rot = boss->GetRotation();
        rot.y += fireSpinSpeed * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        float expandTime = 0.2f;
        float t = std::min(animTimer_ / expandTime, 1.0f);
        float beamThickness = Math::Lerp(0.1f, 1.0f, Easing::OutExpo(t));

        for (Object3d* block : armorBlocks) {
            if (!block) continue;
            for (Object3d* child : block->GetChildren()) {
                if (child->GetName().find("Beam_Cylinder") != std::string::npos) {
                    child->SetScale({ beamThickness, 80.0f, beamThickness });
                    child->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
                    child->SetCollisionAttribute(kEnemyAttack);
                }
            }
        }

        if (animTimer_ >= spinDuration) {
            animPhase_ = 65;
            animTimer_ = 0.0f;
            animStartRot_ = boss->GetRotation();

            for (Object3d* block : armorBlocks) {
                if (!block) continue;
                for (Object3d* child : block->GetChildren()) {
                    if (child->GetName().find("Beam_Cylinder") != std::string::npos) {
                        child->SetScale({ 0.0f, 0.0f, 0.0f });
                        child->SetCollisionAttribute(0);
                    }
                }
            }

            blockStartPos_.clear();
            blockStartScale_.clear();
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks[i]->GetScale());
            }
        }
    }
    // --- Phase 65: 回転を止め、待機状態のバラバラ軌道へ復帰する ---
    else if (animPhase_ == 65) {
        animTimer_ += deltaTime;
        float duration = 2.25f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        Vector3 rot = animStartRot_;
        rot.y = Math::Lerp(animStartRot_.y, 0.0f, easeT);
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

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
            isFinished_ = true;
        }
    }
}