#include "BossAttack9_Funnels.h"
#include "../BossCore.h"
#include "./easing.h"
#include "SceneManager.h"
#include "engine/system/collision/CollisionManager.h"
#include <algorithm>
#include <cmath>
#include <numbers>

void BossAttack9_Funnels::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    blockStartPos_.clear();
    blockTargetPos_.clear();
    blockStartScale_.clear();
    activeLasers_.clear();

    animPhase_ = 90;
    animTimer_ = 0.0f;
}

void BossAttack9_Funnels::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();
    Object3d* target = boss->GetTarget();

    // --- Phase 90: ファンネル射出（空中に展開） ---
    if (animPhase_ == 90) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            blockTargetPos_.clear();
            blockStartScale_.clear();

            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (!armorBlocks[i]) continue;
                
                // ワールド座標で独立して動かすために親を外す
                Vector3 worldPos = armorBlocks[i]->GetWorldPosition();
                armorBlocks[i]->SetParent(nullptr);
                armorBlocks[i]->SetTranslate(worldPos);

                blockStartPos_.push_back(worldPos);
                blockStartScale_.push_back(armorBlocks[i]->GetScale());

                // プレイヤーの上空を中心に、円状＋ランダムな高さに展開
                float angle = (i / (float)armorBlocks.size()) * 2.0f * std::numbers::pi_v<float>;
                float radius = 15.0f + ((rand() % 100) / 100.0f) * 10.0f; // 15〜25mの円
                float height = 20.0f + ((rand() % 100) / 100.0f) * 15.0f; // 20〜35mの高さ

                Vector3 targetPos = { 0, height, 0 };
                if (target) {
                    targetPos.x = target->GetWorldPosition().x + std::cos(angle) * radius;
                    targetPos.z = target->GetWorldPosition().z + std::sin(angle) * radius;
                }
                blockTargetPos_.push_back(targetPos);
            }
        }

        animTimer_ += deltaTime;
        float duration = 2.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (!armorBlocks[i] || i >= blockStartPos_.size()) continue;

            Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
            armorBlocks[i]->SetTranslate(pos);

            // コアと同じくらいのサイズに揃える
            Vector3 targetScale = { 2.0f, 2.0f, 2.0f };
            Vector3 scale = Math::Lerp(blockStartScale_[i], targetScale, easeT);
            armorBlocks[i]->SetScale(scale);
            
            // 少し回転させながら飛ばす
            Vector3 rot = armorBlocks[i]->GetRotation();
            rot.y += 10.0f * deltaTime;
            armorBlocks[i]->SetRotation(rot);
            armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 91;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 91: ロックオン（照準レーザー照射） ---
    else if (animPhase_ == 91) {
        if (animTimer_ == 0.0f) {
            BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene();
            std::vector<Object3d*> beamPool;
            std::vector<Object3d*> coreBeamPool;
            if (currentScene) {
                for (auto& obj : currentScene->GetObjects()) {
                    if (obj->GetName() == "Beam_Cylinder") beamPool.push_back(obj.get());
                    else if (obj->GetName() == "Beam_Core_Cylinder") coreBeamPool.push_back(obj.get());
                }
            }

            activeLasers_.clear();
            activeCoreLasers_.clear();
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (!armorBlocks[i]) continue;

                // ==========================================
                // 1. 赤いオーラ（外側のビーム）の生成
                // ==========================================
                Object3d* laser = nullptr;
                if (i < beamPool.size()) {
                    laser = beamPool[i];
                } else {
                    auto newLaser = std::make_unique<Object3d>();
                    laser = newLaser.get();
                    laser->Initialize(boss->GetCommon());
                    laser->SetModel("Cylinder");
                    laser->SetName("Beam_Cylinder");
                    if (currentScene) currentScene->GetObjects().push_back(std::move(newLaser));
                }

                laser->SetBlendMode(BlendMode::kAdd);
                laser->SetEmissive(5.0f);
                laser->SetColor({ 1.0f, 0.0f, 0.0f, 0.8f });
                laser->SetTexture("Resources/sprite/beamNoice.png");
                laser->SetMaterialType(9);

                static Math math;
                Vector3 uvScale = { 1.0f, 15.0f, 1.0f };
                Matrix4x4 uvMat = math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });
                laser->SetUVTransform(uvMat);

                Object3d::ColliderConfig cConfig = laser->GetColliderConfig();
                cConfig.type = ColliderType::kOBB;
                cConfig.size = { 1.0f, 1.0f, 1.0f };
                laser->SetColliderConfig(cConfig);

                laser->SetScale({ 0.1f, 80.0f, 0.1f }); // 予兆時点の細さ
                laser->SetCollisionAttribute(0);

                CollisionManager::GetInstance()->RemoveObject(laser);
                CollisionManager::GetInstance()->AddObject(laser);

                laser->SetParent(armorBlocks[i]);
                float rotX90 = std::numbers::pi_v<float> / 2.0f;
                laser->SetRotation({ rotX90, 0.0f, 0.0f }); // ブロックの+Z方向（下）へ伸ばす
                laser->GetTransform()->isQuaternionMaster = false;

                activeLasers_.push_back(laser);

                // ==========================================
                // 2. 白いコア（内側のビーム）の生成
                // ==========================================
                Object3d* coreLaser = nullptr;
                if (i < coreBeamPool.size()) {
                    coreLaser = coreBeamPool[i];
                } else {
                    auto newCore = std::make_unique<Object3d>();
                    coreLaser = newCore.get();
                    coreLaser->Initialize(boss->GetCommon());
                    coreLaser->SetModel("Cylinder");
                    coreLaser->SetName("Beam_Core_Cylinder");
                    if (currentScene) currentScene->GetObjects().push_back(std::move(newCore));
                }

                coreLaser->SetBlendMode(BlendMode::kAdd);
                coreLaser->SetEmissive(8.0f);
                coreLaser->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                coreLaser->SetTexture("Resources/sprite/beamNoice.png");
                coreLaser->SetMaterialType(9);
                coreLaser->SetUVTransform(uvMat);

                coreLaser->SetColliderConfig(cConfig);
                coreLaser->SetScale({ 0.05f, 80.0f, 0.05f });
                coreLaser->SetCollisionAttribute(0);

                CollisionManager::GetInstance()->RemoveObject(coreLaser);
                CollisionManager::GetInstance()->AddObject(coreLaser);

                coreLaser->SetParent(armorBlocks[i]);
                coreLaser->SetRotation({ rotX90, 0.0f, 0.0f });
                coreLaser->GetTransform()->isQuaternionMaster = false;

                activeCoreLasers_.push_back(coreLaser);
            }
        }

        animTimer_ += deltaTime;
        float duration = 2.0f;
        float t = std::min(animTimer_ / duration, 1.0f);

        // プレイヤーの方を向く（徐々に追従）
        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (!armorBlocks[i]) continue;
            
            if (target) {
                Vector3 toPlayer = target->GetWorldPosition() - armorBlocks[i]->GetWorldPosition();
                
                // ランダムな揺らぎを入れて、少し不気味に照準を合わせる
                float noise = std::sin(animTimer_ * 10.0f + i) * 2.0f * (1.0f - t);
                toPlayer.x += noise;
                toPlayer.z += noise;

                float targetRotY = std::atan2(toPlayer.x, toPlayer.z);
                float distXZ = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
                float targetRotX = -std::atan2(toPlayer.y, distXZ);

                auto LerpAngle = [](float a, float b, float dt) {
                    float diff = b - a;
                    while (diff < -std::numbers::pi_v<float>) diff += 2.0f * std::numbers::pi_v<float>;
                    while (diff > std::numbers::pi_v<float>) diff -= 2.0f * std::numbers::pi_v<float>;
                    return a + diff * dt;
                };

                Vector3 currentRot = armorBlocks[i]->GetRotation();
                currentRot.y = LerpAngle(currentRot.y, targetRotY, 5.0f * deltaTime);
                currentRot.x = LerpAngle(currentRot.x, targetRotX, 5.0f * deltaTime);
                
                armorBlocks[i]->SetRotation(currentRot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 92;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 92: 一斉発射（極太レーザー） ---
    else if (animPhase_ == 92) {
        if (animTimer_ == 0.0f) {
            for (auto* laser : activeLasers_) {
                if (!laser) continue;
                // 極太レーザーに変化！
                laser->SetScale({ 1.0f, 80.0f, 1.0f });
                laser->SetCollisionAttribute(kEnemyAttack); // 当たり判定ON
            }
            for (auto* core : activeCoreLasers_) {
                if (!core) continue;
                core->SetScale({ 0.4f, 80.0f, 0.4f });
                core->SetCollisionAttribute(0);
            }
        }

        animTimer_ += deltaTime;
        float duration = 2.5f;
        float t = std::min(animTimer_ / duration, 1.0f);

        // 発射中も少しだけプレイヤーを追いかける（薙ぎ払い効果）
        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (!armorBlocks[i]) continue;
            
            if (target) {
                Vector3 toPlayer = target->GetWorldPosition() - armorBlocks[i]->GetWorldPosition();
                float targetRotY = std::atan2(toPlayer.x, toPlayer.z);
                float distXZ = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
                float targetRotX = -std::atan2(toPlayer.y, distXZ);

                auto LerpAngle = [](float a, float b, float dt) {
                    float diff = b - a;
                    while (diff < -std::numbers::pi_v<float>) diff += 2.0f * std::numbers::pi_v<float>;
                    while (diff > std::numbers::pi_v<float>) diff -= 2.0f * std::numbers::pi_v<float>;
                    return a + diff * dt;
                };

                Vector3 currentRot = armorBlocks[i]->GetRotation();
                // 追従速度は遅め（逃げ切れるように）
                currentRot.y = LerpAngle(currentRot.y, targetRotY, 1.5f * deltaTime);
                currentRot.x = LerpAngle(currentRot.x, targetRotX, 1.5f * deltaTime);
                
                armorBlocks[i]->SetRotation(currentRot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        // 終了間近でレーザーを細くしていく
        if (t > 0.8f) {
            float shrinkT = (1.0f - t) / 0.2f; // 1.0 -> 0.0
            for (auto* laser : activeLasers_) {
                if (!laser) continue;
                laser->SetScale({ 1.0f * shrinkT, 80.0f, 1.0f * shrinkT });
                laser->SetCollisionAttribute(0);
            }
            for (auto* core : activeCoreLasers_) {
                if (!core) continue;
                core->SetScale({ 0.4f * shrinkT, 80.0f, 0.4f * shrinkT });
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 93;
            animTimer_ = 0.0f;
            for (auto* laser : activeLasers_) {
                if (laser) {
                    laser->SetScale({ 0.0f, 0.0f, 0.0f }); // 見えなくする
                    laser->SetParent(nullptr); // 親子関係解除
                    CollisionManager::GetInstance()->RemoveObject(laser);
                }
            }
            for (auto* core : activeCoreLasers_) {
                if (core) {
                    core->SetScale({ 0.0f, 0.0f, 0.0f });
                    core->SetParent(nullptr);
                    CollisionManager::GetInstance()->RemoveObject(core);
                }
            }
            activeLasers_.clear();
            activeCoreLasers_.clear();
            
            // 帰還のための初期位置を保存
            blockStartPos_.clear();
            blockStartScale_.clear();
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (!armorBlocks[i]) continue;
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks[i]->GetScale());
            }
        }
    }
    // --- Phase 93: ファンネル帰還 ---
    else if (animPhase_ == 93) {
        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (!armorBlocks[i] || i >= blockStartPos_.size()) continue;

            // ボスの元の軌道位置を取得
            BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
            
            // 帰還するため、再びボスを親に設定し直す必要があるが、
            // Lerp中はワールド座標で動かした方が自然なのでギリギリまでワールドで動かす。
            // orbit.pos はローカル座標なので、ボスのワールド座標に変換して向かわせる。
            
            // ボスの回転を考慮しない単純なワールド位置計算
            Vector3 targetWorldPos = boss->GetWorldPosition();
            targetWorldPos.x += orbit.pos.x;
            targetWorldPos.y += orbit.pos.y;
            targetWorldPos.z += orbit.pos.z;

            Vector3 pos = Math::Lerp(blockStartPos_[i], targetWorldPos, easeT);
            armorBlocks[i]->SetTranslate(pos);

            Vector3 scale = Math::Lerp(blockStartScale_[i], orbit.scale, easeT);
            armorBlocks[i]->SetScale(scale);

            // 回転も元に戻す
            Vector3 currentRot = armorBlocks[i]->GetRotation();
            currentRot.x = Math::Lerp(currentRot.x, orbit.rot.x, easeT);
            currentRot.y = Math::Lerp(currentRot.y, orbit.rot.y, easeT);
            currentRot.z = Math::Lerp(currentRot.z, orbit.rot.z, easeT);
            armorBlocks[i]->SetRotation(currentRot);
            armorBlocks[i]->GetTransform()->isQuaternionMaster = false;

            if (t >= 1.0f) {
                // 親に戻す
                armorBlocks[i]->SetParent(boss);
                armorBlocks[i]->SetTranslate(orbit.pos);
                armorBlocks[i]->SetRotation(orbit.rot);
            }
        }

        if (t >= 1.0f) {
            isFinished_ = true;
        }
    }
}

void BossAttack9_Funnels::Finalize() {
    for (auto* laser : activeLasers_) {
        if (laser) {
            laser->SetScale({ 0.0f, 0.0f, 0.0f });
            laser->SetCollisionAttribute(0);
            laser->SetParent(nullptr);
            CollisionManager::GetInstance()->RemoveObject(laser);
        }
    }
    for (auto* core : activeCoreLasers_) {
        if (core) {
            core->SetScale({ 0.0f, 0.0f, 0.0f });
            core->SetCollisionAttribute(0);
            core->SetParent(nullptr);
            CollisionManager::GetInstance()->RemoveObject(core);
        }
    }
    activeLasers_.clear();
    activeCoreLasers_.clear();
}
