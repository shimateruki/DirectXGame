#include "BossAttack2_Shoot.h"
#include "../BossCore.h"
#include "./easing.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

bool IsShootBlockAvailable(BossCore* boss, const std::vector<Object3d*>& armorBlocks, size_t index) {
    return boss && index < armorBlocks.size() && armorBlocks[index] && !boss->IsArmorBlockBroken(index);
}

} // namespace

void BossAttack2_Shoot::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    blockStartPos_.clear();
    blockTargetPos_.clear();
    activeBlockIndices_.clear();
    shotCount_ = 0;
    descendTimer_ = 0.0f;

    animStartPos_ = boss->GetTranslate();
    animPhase_ = 10;
}

void BossAttack2_Shoot::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();
    Object3d* target = boss->GetTarget();

    if (animPhase_ == 10) {
        animTimer_ += deltaTime;
        float t = std::min(animTimer_ / 2.5f, 1.0f);

        Vector3 pos = boss->GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, 50.0f, Easing::OutExpo(t));
        pos.y = Math::Lerp(animStartPos_.y, animStartPos_.y + 8.0f, Easing::OutExpo(t));
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

            blockStartPos_.clear();
            blockTargetPos_.clear();
            activeBlockIndices_.clear();

            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (IsShootBlockAvailable(boss, armorBlocks, i)) {
                    activeBlockIndices_.push_back(i);
                }
            }

            if (activeBlockIndices_.empty()) {
                animPhase_ = 13;
                animTimer_ = 0.0f;
                descendTimer_ = 0.0f;
                return;
            }

            float turnY = std::numbers::pi_v<float> / 2.0f;
            float radius = 4.0f;

            for (size_t order = 0; order < activeBlockIndices_.size(); ++order) {
                size_t blockIndex = activeBlockIndices_[order];
                Object3d* block = armorBlocks[blockIndex];

                blockStartPos_.push_back(block->GetTranslate());

                float angle = (2.0f * std::numbers::pi_v<float> * static_cast<float>(order)) /
                    static_cast<float>(activeBlockIndices_.size());

                Vector3 targetPos = {
                    -2.0f,
                    std::sin(angle) * radius,
                    std::cos(angle) * radius
                };

                blockTargetPos_.push_back(targetPos);

                block->SetScale({ 1.5f, 1.5f, 1.5f });
                block->SetRotation({ 0.0f, turnY, 0.0f });
                block->GetTransform()->isQuaternionMaster = false;
            }
        }
    }
    else if (animPhase_ == 11) {
        animTimer_ += deltaTime;
        float t = std::min(animTimer_ / 1.0f, 1.0f);
        float easeT = Easing::OutExpo(t);

        for (size_t order = 0; order < activeBlockIndices_.size(); ++order) {
            if (order >= blockStartPos_.size() || order >= blockTargetPos_.size()) {
                continue;
            }

            size_t blockIndex = activeBlockIndices_[order];
            if (!IsShootBlockAvailable(boss, armorBlocks, blockIndex)) {
                continue;
            }

            Vector3 pos = Math::Lerp(blockStartPos_[order], blockTargetPos_[order], easeT);
            armorBlocks[blockIndex]->SetTranslate(pos);
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
    else if (animPhase_ == 12) {
        if (activeBlockIndices_.empty()) {
            animPhase_ = 13;
            animTimer_ = 0.0f;
            return;
        }

        animTimer_ += deltaTime;

        if (target) {
            Vector3 toPlayer = target->GetWorldPosition() - boss->GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);
            boss->SetRotation({ boss->GetRotation().x, angleY, boss->GetRotation().z });
            boss->GetTransform()->isQuaternionMaster = false;
        }

        float nextShotTime = shotCount_ * 0.5f;

        if (animTimer_ >= nextShotTime) {
            int fireOrder = static_cast<int>(activeBlockIndices_.size()) - 1 - shotCount_;
            if (fireOrder >= 0 && fireOrder < static_cast<int>(activeBlockIndices_.size())) {
                size_t blockIndex = activeBlockIndices_[fireOrder];

                if (IsShootBlockAvailable(boss, armorBlocks, blockIndex)) {
                    Object3d* block = armorBlocks[blockIndex];
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

                    boss->GetFlyingBlocks().push_back(
                        { block, { 0.0f, 0.0f, 0.0f }, currentRot, 4, static_cast<int>(blockIndex) });
                }
            }

            shotCount_++;

            if (shotCount_ >= static_cast<int>(activeBlockIndices_.size())) {
                animPhase_ = 13;
                animTimer_ = 0.0f;
            }
        }
    }
    else if (animPhase_ == 13) {
        descendTimer_ += deltaTime;
        float descendDuration = 4.0f;
        float t = std::min(descendTimer_ / descendDuration, 1.0f);
        float easeT = Easing::InOutQuad(t);

        Vector3 pos = boss->GetTranslate();
        pos.y = Math::Lerp(animStartPos_.y + 8.0f, animStartPos_.y, easeT);
        boss->SetTranslate(pos);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            bool isFlying = false;
            for (auto& fb : boss->GetFlyingBlocks()) {
                if (fb.originalIndex == static_cast<int>(i)) {
                    isFlying = true;
                    break;
                }
            }

            if (!isFlying && IsShootBlockAvailable(boss, armorBlocks, i)) {
                BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
                armorBlocks[i]->SetTranslate(orbit.pos);
                armorBlocks[i]->SetScale(orbit.scale);
                armorBlocks[i]->SetRotation(orbit.rot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f && boss->GetFlyingBlocks().empty()) {
            animTimer_ += deltaTime;
            if (animTimer_ >= 1.0f) {
                isFinished_ = true;
            }
        }
    }
}
