#include "BossAttack6_Laser.h"
#include "../BossCore.h"
#include "./easing.h" // ※環境に合わせてパスを調整してください
#include <algorithm>
#include <cmath>
#include <numbers>
#include "DebugConsole.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include <CollisionManager.h>
#include "CollisionConfig.h"
#include "GPUParticleManager.h"

void BossAttack6_Laser::Finalize() {
    for (Object3d* beam : activeBeams_) {
        if (beam) {
            CollisionManager::GetInstance()->RemoveObject(beam);
            beam->SetParent(nullptr);
            beam->SetScale({ 0.0f, 0.0f, 0.0f });
            beam->SetCollisionAttribute(0);
        }
    }
    activeBeams_.clear();

    for (Object3d* core : activeCoreBeams_) {
        if (core) {
            CollisionManager::GetInstance()->RemoveObject(core);
            core->SetParent(nullptr);
            core->SetScale({ 0.0f, 0.0f, 0.0f });
            core->SetCollisionAttribute(0);
        }
    }
    activeCoreBeams_.clear();
}

// ※デストラクタは Finalize() を呼ぶだけでOKです
BossAttack6_Laser::~BossAttack6_Laser() {
    Finalize();
}
void BossAttack6_Laser::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    blockStartPos_.clear();
    blockTargetPos_.clear();
    blockStartScale_.clear();
    blockTargetScale_.clear();
    attentionStartRot_.clear();

    // ==========================================
    // 前回の攻撃で作った余分なレーザーをリセット
    // ==========================================
    activeBeams_.clear();
    activeCoreBeams_.clear();
    laserLengths_.clear();
    laserDelayTimers_.clear();

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
    else if (animPhase_ == 63) {
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
                    if (currentScene) currentScene->AddObject(std::move(newLaser));
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

                laser->SetScale({ 0.1f, 80.0f, 0.1f });
                laser->SetCollisionAttribute(0);

                CollisionManager::GetInstance()->RemoveObject(laser);
                CollisionManager::GetInstance()->AddObject(laser);

                laser->SetParent(armorBlocks[i]);
                float rotX90 = std::numbers::pi_v<float> / 2.0f;
                laser->SetRotation({ rotX90, 0.0f, 0.0f });
                laser->SetTranslate({ 0.0f, 0.0f, 80.0f }); // 前方に出す
                laser->GetTransform()->isQuaternionMaster = false;

                activeBeams_.push_back(laser);
                laserLengths_.push_back(160.0f);
                laserDelayTimers_.push_back(0.0f);

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
                    if (currentScene) currentScene->AddObject(std::move(newCore));
                }

                coreLaser->SetBlendMode(BlendMode::kAdd);
                coreLaser->SetEmissive(8.0f); // コアはさらに強く光らせる
                coreLaser->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 真っ白
                coreLaser->SetTexture("Resources/sprite/beamNoice.png");
                coreLaser->SetMaterialType(9);
                coreLaser->SetUVTransform(uvMat);

                coreLaser->SetColliderConfig(cConfig); // 当たり判定は持たないが設定だけ入れておく
                coreLaser->SetScale({ 0.05f, 80.0f, 0.05f }); // 予兆時点ではオーラの半分の細さ
                coreLaser->SetCollisionAttribute(0);

                CollisionManager::GetInstance()->RemoveObject(coreLaser);
                CollisionManager::GetInstance()->AddObject(coreLaser);

                coreLaser->SetParent(armorBlocks[i]);
                coreLaser->SetRotation({ rotX90, 0.0f, 0.0f });
                coreLaser->SetTranslate({ 0.0f, 0.0f, 80.0f }); // 前方に出す
                coreLaser->GetTransform()->isQuaternionMaster = false;

                activeCoreBeams_.push_back(coreLaser);
            }
        }

        animTimer_ += deltaTime;
        float stopDuration = 0.75f;

        if (animTimer_ >= stopDuration) {
            animPhase_ = 64;
            animTimer_ = 0.0f;
            particleTimer_ = 0.0f;
        }
        }
    // --- Phase 64: 陣形を維持したまま回転し、ビームを撃つ！ ---
    else if (animPhase_ == 64) {
        animTimer_ += deltaTime;
        particleTimer_ += deltaTime;
        float spinDuration = 7.5f;

        Vector3 rot = boss->GetRotation();
        rot.y += fireSpinSpeed * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        // ==========================================
        // ★ レーザーの太さと振動（脈動）の計算
        // ==========================================
        float expandTime = 0.01f;
        float t = std::min(animTimer_ / expandTime, 1.0f);

        // 基本の太さ（0.1から1.0へ一瞬で太くなる）
        float baseThickness = Math::Lerp(0.1f, 1.0f, Easing::OutExpo(t));

        // 【振動】animTimer_ を使って、1.0を中心にして ±15% ほど激しく震わせる！
        // ※ 60.0f を大きくすると震えるスピードが上がり、0.15f を大きくすると震幅（太さの差）が大きくなります。
        float pulse = 1.0f + (std::sin(animTimer_ * 60.0f) * 0.05f);

        // 最終的な太さ
        float currentThickness = baseThickness * pulse;

        // 2層のビームをそれぞれ更新する
        for (size_t i = 0; i < activeBeams_.size(); ++i) {
            Object3d* beam = activeBeams_[i];
            Object3d* coreBeam = (i < activeCoreBeams_.size()) ? activeCoreBeams_[i] : nullptr;

            // ★ レイキャストで障害物までの距離を測る
            float maxDistance = 160.0f;
            float actualDistance = maxDistance;
            float parentScaleZ = 1.0f;
            if (i < armorBlocks.size() && armorBlocks[i]) {
                Vector3 startPos = armorBlocks[i]->GetWorldPosition();
                Matrix4x4 blockWorld = armorBlocks[i]->GetWorldMatrix();
                Vector3 dir = { blockWorld.m[2][0], blockWorld.m[2][1], blockWorld.m[2][2] }; // Z軸
                
                parentScaleZ = Math::Length(dir);
                if (parentScaleZ < 0.0001f) parentScaleZ = 1.0f;

                dir = Math::Normalize(dir);
                
                RaycastHit hit = CollisionManager::GetInstance()->Raycast(startPos, dir, maxDistance, kGround | kMapBlock);
                if (hit.isHit) {
                    actualDistance = hit.distance;
                }
            }

            // ★ 追加: ヒット距離を使った遅延付きの長さ復元処理
            if (i < laserLengths_.size()) {
                if (actualDistance < laserLengths_[i]) {
                    // より近い障害物に当たった場合は即座に縮める＆タイマーリセット
                    laserLengths_[i] = actualDistance;
                    laserDelayTimers_[i] = 0.3f; // 0.3秒間キープ
                } else {
                    // 障害物が無くなった（または遠ざかった）場合
                    if (laserDelayTimers_[i] > 0.0f) {
                        laserDelayTimers_[i] -= deltaTime;
                    } else {
                        // タイマー終了後、徐々に長さを戻す (秒間 300 ユニット)
                        laserLengths_[i] += 300.0f * deltaTime;
                        if (laserLengths_[i] > actualDistance) {
                            laserLengths_[i] = actualDistance;
                        }
                    }
                }
                actualDistance = laserLengths_[i];
            }

            float beamScaleY = (actualDistance / 2.0f) / parentScaleZ;
            float beamOffsetZ = beamScaleY;

            static Math math;

            // --- 1. 外側の赤いオーラ ---
            if (beam) {
                beam->SetScale({ currentThickness, beamScaleY, currentThickness });
                beam->SetTranslate({ 0.0f, 0.0f, beamOffsetZ });
                beam->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
                beam->SetCollisionAttribute(kEnemyAttack);
                beam->SetCollisionMask(kPlayer);
                beam->SetAttackDamage(boss->GetAttackParams().damageLaser);
                
                Vector3 uvScale = { 1.0f, 15.0f, 1.0f }; 
                float scrollSpeed = -30.0f; 
                Vector3 uvTranslate = { 0.0f, animTimer_ * scrollSpeed, 0.0f };
                beam->SetUVTransform(math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, uvTranslate));
            }

            // --- 2. 内側の白いコア ---
            if (coreBeam) {
                // コアはオーラの 60% の太さにする
                float coreThickness = currentThickness * 0.6f;
                coreBeam->SetScale({ coreThickness, beamScaleY, coreThickness });
                coreBeam->SetTranslate({ 0.0f, 0.0f, beamOffsetZ });
                coreBeam->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 真っ白
                
                // コアは当たり判定を持たない（オーラに任せる）
                coreBeam->SetCollisionAttribute(0); 
                coreBeam->SetCollisionMask(0);

                // コアはさらに速くスクロールさせて、内側のエネルギーの激しさを強調する
                Vector3 coreUvScale = { 1.0f, 20.0f, 1.0f }; 
                float coreScrollSpeed = -50.0f; 
                Vector3 coreUvTranslate = { 0.0f, animTimer_ * coreScrollSpeed, 0.0f };
                coreBeam->SetUVTransform(math.MakeAffineMatrix(coreUvScale, { 0.0f, 0.0f, 0.0f }, coreUvTranslate));
            }
        }

        // ==========================================
        // ★ GPUパーティクルの発生（ビームの回転に追従させる）
        // ==========================================
        // 0.05秒に1回、全ビームの根元からエミットする
        if (particleTimer_ >= 0.05f) {
            for (Object3d* beam : activeBeams_) {
                if (beam) {
              /*      GPUParticleManager::GetInstance()->Emit("LaserSpark", beam->GetWorldPosition(), beam->GetWorldMatrix());*/
                }
            }
            particleTimer_ = 0.0f;
        }

        if (animTimer_ >= spinDuration) {
            animPhase_ = 65;
            animTimer_ = 0.0f;
            animStartRot_ = boss->GetRotation();
            for (Object3d* beam : activeBeams_) {
                if (beam) {
                    CollisionManager::GetInstance()->RemoveObject(beam); 
                    beam->SetParent(nullptr);                            
                    beam->SetScale({ 0.0f, 0.0f, 0.0f });
                    beam->SetCollisionAttribute(0);
                }
            }
            activeBeams_.clear(); 

            for (Object3d* core : activeCoreBeams_) {
                if (core) {
                    CollisionManager::GetInstance()->RemoveObject(core); 
                    core->SetParent(nullptr);                            
                    core->SetScale({ 0.0f, 0.0f, 0.0f });
                    core->SetCollisionAttribute(0);
                }
            }
            activeCoreBeams_.clear();

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