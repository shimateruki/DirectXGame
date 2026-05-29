#define NOMINMAX
#include "BossAttack9_Funnels.h"
#include "../BossCore.h"
#include "AudioPlayer.h"
#include "./easing.h"
#include "SceneManager.h"
#include "engine/system/collision/CollisionManager.h"
#include "CollisionConfig.h"
#include <algorithm>
#include <cmath>
#include <numbers>

void BossAttack9_Funnels::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);
    boss_ = boss;

    blockStartPos_.clear();
    blockTargetPos_.clear();
    blockStartScale_.clear();
    activeLasers_.clear();
    activeCoreLasers_.clear();
    laserLengths_.clear();
    laserDelayTimers_.clear();
    funnelStates_.clear();
    funnelTimers_.clear();
    funnelFireCounts_.clear();

    animPhase_ = 90;
    animTimer_ = 0.0f;
}

void BossAttack9_Funnels::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();
    Object3d* target = boss->GetTarget();
    auto restoreFunnelCoreColor = [](Object3d* block) {
        if (!block) return;
        for (auto* child : block->GetChildren()) {
            if (!child) continue;
            const std::string& name = child->GetName();
            if (name.find("_Core") != std::string::npos && name.find("BossAttack") == std::string::npos) {
                child->SetColor({ 0.0f, 0.7f, 1.0f, 1.0f });
                child->SetMaterialType(2);
                child->SetEmissive(4.0f);
            }
        }
    };

    // --- Phase 90: ファンネル射出（空中に展開） ---
    if (animPhase_ == 90) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.assign(armorBlocks.size(), {});
            blockTargetPos_.assign(armorBlocks.size(), {});
            blockStartScale_.assign(armorBlocks.size(), {});

            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (!armorBlocks[i]) continue;

                boss->UpgradeToFunnel(armorBlocks[i]);
                armorBlocks[i]->SetIsVisible(false);
                restoreFunnelCoreColor(armorBlocks[i]);
                
                // ワールド座標で独立して動かすために親を外す
                Vector3 worldPos = armorBlocks[i]->GetWorldPosition();
                armorBlocks[i]->SetParent(nullptr);
                armorBlocks[i]->SetTranslate(worldPos);

                blockStartPos_[i] = worldPos;
                blockStartScale_[i] = armorBlocks[i]->GetScale();

                // プレイヤー周辺の空中にまばらに散らす
                float randomX = -25.0f + ((rand() % 100) / 100.0f) * 50.0f; // -25〜25mの範囲
                float randomZ = -25.0f + ((rand() % 100) / 100.0f) * 50.0f; // -25〜25mの範囲
                float height = 5.0f + ((rand() % 100) / 100.0f) * 15.0f; // 5〜20mの高さ（低めに調整）

                Vector3 targetPos = { randomX, height, randomZ };
                if (target) {
                    targetPos.x += target->GetWorldPosition().x;
                    targetPos.z += target->GetWorldPosition().z;
                }
                blockTargetPos_[i] = targetPos;
            }
        }

        animTimer_ += deltaTime;
        float duration = 2.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (!armorBlocks[i] || i >= blockStartPos_.size()) continue;
            restoreFunnelCoreColor(armorBlocks[i]);

            Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
            armorBlocks[i]->SetTranslate(pos);

            // サイズは大きくせず元のサイズを維持
            armorBlocks[i]->SetScale(blockStartScale_[i]);
            
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
                    if (obj->GetName() == "BossAttack9_Beam_Cylinder") beamPool.push_back(obj.get());
                    else if (obj->GetName() == "BossAttack9_Beam_Core_Cylinder") coreBeamPool.push_back(obj.get());
                }
            }

            activeLasers_.assign(armorBlocks.size(), nullptr);
            activeCoreLasers_.assign(armorBlocks.size(), nullptr);
            laserLengths_.assign(armorBlocks.size(), 160.0f);
            laserDelayTimers_.assign(armorBlocks.size(), 0.0f);
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
                    laser->SetName("BossAttack9_Beam_Cylinder");
                    if (currentScene) currentScene->AddObject(std::move(newLaser));
                }

                laser->SetIsVisible(false);
                laser->SetParent(nullptr);
                laser->SetScale({ 0.0f, 0.0f, 0.0f });
                laser->SetCollisionAttribute(0);
                laser->UpdateWorldMatrix();

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

                laser->SetScale({ 0.0f, 0.0f, 0.0f }); // 最初は見えないように設定
                laser->SetCollisionAttribute(0);

                CollisionManager::GetInstance()->RemoveObject(laser);
                CollisionManager::GetInstance()->AddObject(laser);

                laser->SetParent(armorBlocks[i]);
                float rotX90 = std::numbers::pi_v<float> / 2.0f;
                laser->SetRotation({ rotX90, 0.0f, 0.0f }); // ブロックの+Z方向（下）へ伸ばす
                // ビームのYスケールが80の場合、モデルが中心基準だと長さが160になるため、80だけ前方にずらす
                laser->SetTranslate({ 0.0f, 0.0f, 80.0f });
                laser->GetTransform()->isQuaternionMaster = false;
                laser->UpdateWorldMatrix();

                activeLasers_[i] = laser;

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
                    coreLaser->SetName("BossAttack9_Beam_Core_Cylinder");
                    if (currentScene) currentScene->AddObject(std::move(newCore));
                }

                coreLaser->SetIsVisible(false);
                coreLaser->SetParent(nullptr);
                coreLaser->SetScale({ 0.0f, 0.0f, 0.0f });
                coreLaser->SetCollisionAttribute(0);
                coreLaser->UpdateWorldMatrix();

                coreLaser->SetBlendMode(BlendMode::kAdd);
                coreLaser->SetEmissive(8.0f);
                coreLaser->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                coreLaser->SetTexture("Resources/sprite/beamNoice.png");
                coreLaser->SetMaterialType(9);
                coreLaser->SetUVTransform(uvMat);

                coreLaser->SetColliderConfig(cConfig);
                coreLaser->SetScale({ 0.0f, 0.0f, 0.0f }); // 最初は見えないように設定
                coreLaser->SetCollisionAttribute(0);

                CollisionManager::GetInstance()->RemoveObject(coreLaser);
                CollisionManager::GetInstance()->AddObject(coreLaser);

                coreLaser->SetParent(armorBlocks[i]);
                coreLaser->SetRotation({ rotX90, 0.0f, 0.0f });
                coreLaser->SetTranslate({ 0.0f, 0.0f, 80.0f });
                coreLaser->GetTransform()->isQuaternionMaster = false;
                coreLaser->UpdateWorldMatrix();

                activeCoreLasers_[i] = coreLaser;
            }
        }

        // レーザーの生成が完了したら即座にPhase92へ移行
        animPhase_ = 92;
        animTimer_ = 0.0f;
    }
    // --- Phase 92: ファンネルウェーブ攻撃（最大2個ずつ、計3ウェーブ） ---
    else if (animPhase_ == 92) {
        if (animTimer_ == 0.0f) {
            funnelStates_.clear();
            funnelTimers_.clear();
            funnelFireCounts_.clear();
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                funnelStates_.push_back(0); // 0: デコイ待機
                funnelTimers_.push_back(0.0f);
                funnelFireCounts_.push_back(0);
            }
        }

        // ウェーブ管理（5.0秒ごとに1ウェーブ、合計3ウェーブで終了）
        int currentWave = (int)(animTimer_ / 5.0f);
        int previousWave = (int)((animTimer_ - deltaTime) / 5.0f);

        // 新しいウェーブの開始時に、1〜3個のランダムなファンネルに攻撃命令を下す
        if ((animTimer_ == 0.0f || currentWave > previousWave) && currentWave < 3) {
            int numShooters = 1 + (rand() % 3); // 1〜3個のランダム
            std::vector<int> shooters;
            std::vector<int> validIndices;
            validIndices.reserve(armorBlocks.size());
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (armorBlocks[i]) {
                    validIndices.push_back(static_cast<int>(i));
                }
            }
            int maxRetries = 20;
            while (shooters.size() < numShooters && !validIndices.empty() && maxRetries > 0) {
                int s = validIndices[rand() % validIndices.size()];
                bool exists = false;
                for (int existing : shooters) {
                    if (existing == s) { exists = true; break; }
                }
                if (!exists) shooters.push_back(s);
                maxRetries--;
            }

            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (!armorBlocks[i]) continue;
                bool isShooter = false;
                for (int s : shooters) {
                    if (i == s) isShooter = true;
                }

                // すでに発射中のものはおかしくなるので、0（待機）か1（デコイ移動）のものだけ上書き
                if (isShooter) {
                    funnelStates_[i] = 10; // 10: 攻撃用移動へ
                    funnelTimers_[i] = 0.0f;
                    // 目的地セット
                    blockStartPos_[i] = armorBlocks[i]->GetTranslate();
                    float randomX = -30.0f + ((rand() % 100) / 100.0f) * 60.0f;
                    float randomZ = -30.0f + ((rand() % 100) / 100.0f) * 60.0f;
                    float height = 4.0f + ((rand() % 100) / 100.0f) * 4.0f; // 4m〜8m
                    Vector3 targetP = { randomX, height, randomZ };
                    if (target) {
                        targetP.x += target->GetWorldPosition().x;
                        targetP.z += target->GetWorldPosition().z;
                    }
                    blockTargetPos_[i] = targetP;
                } else if (funnelStates_[i] == 0 || funnelStates_[i] == 1) {
                    // 他のブロックは50%の確率でデコイとして移動
                    if (rand() % 100 < 50) { 
                        funnelStates_[i] = 1; // 1: デコイ移動
                        funnelTimers_[i] = 0.0f;
                        blockStartPos_[i] = armorBlocks[i]->GetTranslate();
                        float randomX = -30.0f + ((rand() % 100) / 100.0f) * 60.0f;
                        float randomZ = -30.0f + ((rand() % 100) / 100.0f) * 60.0f;
                        float height = 4.0f + ((rand() % 100) / 100.0f) * 4.0f; // 4m〜8m
                        Vector3 targetP = { randomX, height, randomZ };
                        if (target) {
                            targetP.x += target->GetWorldPosition().x;
                            targetP.z += target->GetWorldPosition().z;
                        }
                        blockTargetPos_[i] = targetP;
                    }
                }
            }
        }

        animTimer_ += deltaTime;

        // 個別のファンネル処理
        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (!armorBlocks[i]) continue;
            restoreFunnelCoreColor(armorBlocks[i]);
            
            Object3d* laser = (i < activeLasers_.size()) ? activeLasers_[i] : nullptr;
            Object3d* coreLaser = (i < activeCoreLasers_.size()) ? activeCoreLasers_[i] : nullptr;

            auto TrackPlayer = [&](Object3d* block, Object3d* tgt, float speed) {
                if (!tgt) return;
                Vector3 toPlayer = tgt->GetWorldPosition() - block->GetWorldPosition();
                float targetRotY = std::atan2(toPlayer.x, toPlayer.z);
                float distXZ = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
                float targetRotX = -std::atan2(toPlayer.y, distXZ);

                auto LerpAngle = [](float a, float b, float dt) {
                    float diff = b - a;
                    while (diff < -std::numbers::pi_v<float>) diff += 2.0f * std::numbers::pi_v<float>;
                    while (diff > std::numbers::pi_v<float>) diff -= 2.0f * std::numbers::pi_v<float>;
                    return a + diff * dt;
                };
                Vector3 currentRot = block->GetRotation();
                currentRot.y = LerpAngle(currentRot.y, targetRotY, speed);
                currentRot.x = LerpAngle(currentRot.x, targetRotX, speed);
                
                // スタイリッシュな演出：Z軸回転（バレルロール）を追加してドリルっぽく回転しながら飛ぶ
                currentRot.z = animTimer_ * 10.0f; // 常にくるくる回る
                
                block->SetRotation(currentRot);
                block->GetTransform()->isQuaternionMaster = false;
            };

            // レーザーのレイキャスト処理 (フェーズ11〜13で使う)
            float maxDist = 160.0f;
            float actualDist = maxDist;
            float parentScaleZ = 1.0f;
            if (armorBlocks[i] && (funnelStates_[i] >= 11 && funnelStates_[i] <= 13)) {
                Vector3 startPos = armorBlocks[i]->GetWorldPosition();
                Matrix4x4 blockWorld = armorBlocks[i]->GetWorldMatrix();
                Vector3 dir = { blockWorld.m[2][0], blockWorld.m[2][1], blockWorld.m[2][2] };
                
                parentScaleZ = Math::Length(dir);
                if (parentScaleZ < 0.0001f) parentScaleZ = 1.0f;

                dir = Math::Normalize(dir);
                
                RaycastHit hit = CollisionManager::GetInstance()->Raycast(startPos, dir, maxDist, kGround | kMapBlock);
                if (hit.isHit) {
                    actualDist = hit.distance;
                }
            }

            // ヒット距離を使った遅延付きの長さ復元処理
            if (i < laserLengths_.size()) {
                if (actualDist < laserLengths_[i]) {
                    // より近い障害物に当たった場合は即座に縮める＆タイマーリセット
                    laserLengths_[i] = actualDist;
                    laserDelayTimers_[i] = 0.3f; // 0.3秒間キープ
                } else {
                    // 障害物が無くなった（または遠ざかった）場合
                    if (laserDelayTimers_[i] > 0.0f) {
                        laserDelayTimers_[i] -= deltaTime;
                    } else {
                        // タイマー終了後、徐々に長さを戻す (秒間 300 ユニット)
                        laserLengths_[i] += 300.0f * deltaTime;
                        if (laserLengths_[i] > actualDist) {
                            laserLengths_[i] = actualDist;
                        }
                    }
                }
                actualDist = laserLengths_[i];
            }

            float scaleY = (actualDist / 2.0f) / parentScaleZ;
            float offsetZ = scaleY;
            static Math math;

            // 状態に応じた処理
            if (funnelStates_[i] == 0) {
                // 0: デコイ待機
                TrackPlayer(armorBlocks[i], target, 2.0f * deltaTime);
                Vector3 pos = blockTargetPos_[i];
                pos.y += std::sin(animTimer_ * 5.0f + i) * 0.5f;
                armorBlocks[i]->SetTranslate(pos);
            }
            else if (funnelStates_[i] == 1) {
                // 1: デコイ移動
                funnelTimers_[i] += deltaTime;
                float moveDuration = 1.5f; 
                float t = std::min(funnelTimers_[i] / moveDuration, 1.0f);
                float easeT = Easing::InOutSine(t);
                
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                pos.y += std::sin(animTimer_ * 5.0f + i) * 0.5f;
                armorBlocks[i]->SetTranslate(pos);

                TrackPlayer(armorBlocks[i], target, 3.0f * deltaTime);

                if (t >= 1.0f) {
                    funnelStates_[i] = 0; // 移動し終わったら待機へ戻る
                }
            }
            else if (funnelStates_[i] == 10) {
                // 10: 攻撃用移動（1.5秒）
                funnelTimers_[i] += deltaTime;
                float moveDuration = 1.5f;
                float t = std::min(funnelTimers_[i] / moveDuration, 1.0f);
                float easeT = Easing::InOutSine(t);
                
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                pos.y += std::sin(animTimer_ * 5.0f + i) * 0.5f;
                armorBlocks[i]->SetTranslate(pos);

                TrackPlayer(armorBlocks[i], target, 4.0f * deltaTime);

                if (t >= 1.0f) {
                    funnelStates_[i] = 11; // 予兆へ
                    funnelTimers_[i] = 0.0f;

                    // 自傷防止のため、ビームを撃つファンネルの判定を一時的に消す
                    if (armorBlocks[i]) {
                        armorBlocks[i]->SetCollisionAttribute(0);
                    }
                    
                    // ウェーブ内で最初にここを通った時にSEを鳴らす（重複再生を防ぐため）
                    static float lastPlayTime11 = -1.0f;
                    if (lastPlayTime11 != animTimer_) {
                        AudioPlayer::GetInstance()->PlaySE(boss->GetSEBossAttack6PredictionlineHandle(), false, 1.0f);
                        lastPlayTime11 = animTimer_;
                    }
                }
            }
            else if (funnelStates_[i] == 11) {
                // 11: 予兆（1.2秒）
                funnelTimers_[i] += deltaTime;
                
                // 予兆はずっと非常に細い赤い線（スナイパーレーザー）のまま
                if (laser) { 
                    laser->SetIsVisible(true);
                    laser->SetScale({ 0.02f, scaleY, 0.02f }); 
                    laser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    laser->SetColor({ 1.0f, 0.0f, 0.0f, 0.8f });
                    laser->SetCollisionAttribute(0); 
                    Vector3 uvScale = { 1.0f, 15.0f, 1.0f };
                    Vector3 uvTranslate = { 0.0f, funnelTimers_[i] * -30.0f, 0.0f };
                    laser->SetUVTransform(math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, uvTranslate));
                    laser->UpdateWorldMatrix();
                }
                if (coreLaser) { 
                    coreLaser->SetIsVisible(false);
                    coreLaser->SetScale({ 0.0f, scaleY, 0.0f }); 
                    coreLaser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    coreLaser->SetCollisionAttribute(0); 
                    coreLaser->UpdateWorldMatrix();
                }

                // --- 子ブロック（Shard）の展開演出 ---
                float expandT = std::min(funnelTimers_[i] / 1.2f, 1.0f);
                float expandOffset = Easing::OutBack(expandT) * 1.5f; // 最大1.5m外側に展開
                for (auto* child : armorBlocks[i]->GetChildren()) {
                    if (child && child->GetName().find("Shard") != std::string::npos) {
                        Vector3 basePos = child->GetTranslate();
                        // 原点(0,0,0)からの方向を正規化して、展開方向にオフセットをかける
                        Vector3 dir = Math::Normalize(basePos);
                        if (Math::Length(dir) < 0.1f) dir = { 0.0f, 1.0f, 0.0f }; // 安全策
                        child->SetTranslate(Math::Normalize(basePos) * (0.35f + expandOffset));
                    }
                }

                if (funnelTimers_[i] < 0.7f) {
                    // 最初の0.7秒間はプレイヤーをしっかり追従
                    TrackPlayer(armorBlocks[i], target, 8.0f * deltaTime);
                } else {
                    // 撃つ直前（0.5秒間）：追従を停止（照準固定）して発射が来ることを知らせる
                }
                
                Vector3 pos = blockTargetPos_[i];
                if (funnelTimers_[i] < 0.7f) {
                    pos.y += std::sin(animTimer_ * 5.0f + i) * 0.5f; // 追従中はゆらゆら
                }
                // ロックオン完了後はゆらゆらを完全に止めて静止する
                armorBlocks[i]->SetTranslate(pos);

                if (funnelTimers_[i] >= 1.2f) {
                    funnelStates_[i] = 12; // 発射（当たり判定ON）へ
                    funnelTimers_[i] = 0.0f;
                    
                    static float lastPlayTime12 = -1.0f;
                    if (lastPlayTime12 != animTimer_) {
                        AudioPlayer::GetInstance()->PlaySE(boss->GetSEBossAttack6BeamHandle(), false, 2.0f);
                        lastPlayTime12 = animTimer_;
                    }
                }
            }
            else if (funnelStates_[i] == 12) {
                // 12: 発射（1.5秒）
                funnelTimers_[i] += deltaTime;
                
                if (laser) {
                    laser->SetIsVisible(true);
                    laser->SetScale({ 1.0f, scaleY, 1.0f });
                    laser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    laser->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
                    laser->SetCollisionAttribute(kEnemyAttack);
                    laser->SetAttackDamage(boss->GetAttackParams().damageFunnels);
                    Vector3 uvScale = { 1.0f, 15.0f, 1.0f };
                    Vector3 uvTranslate = { 0.0f, funnelTimers_[i] * -30.0f, 0.0f };
                    laser->SetUVTransform(math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, uvTranslate));
                    laser->UpdateWorldMatrix();
                }
                if (coreLaser) { 
                    coreLaser->SetIsVisible(true);
                    coreLaser->SetScale({ 0.4f, scaleY, 0.4f }); 
                    coreLaser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    coreLaser->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    coreLaser->SetCollisionAttribute(0); 
                    Vector3 coreUvScale = { 1.0f, 20.0f, 1.0f };
                    Vector3 coreUvTranslate = { 0.0f, funnelTimers_[i] * -50.0f, 0.0f };
                    coreLaser->SetUVTransform(math.MakeAffineMatrix(coreUvScale, { 0.0f, 0.0f, 0.0f }, coreUvTranslate));
                    coreLaser->UpdateWorldMatrix();
                }

                // --- 子ブロック（Shard）の展開を維持（少し震わせる） ---
                float shake = std::sin(funnelTimers_[i] * 50.0f) * 0.1f;
                for (auto* child : armorBlocks[i]->GetChildren()) {
                    if (child && child->GetName().find("Shard") != std::string::npos) {
                        Vector3 basePos = child->GetTranslate();
                        child->SetTranslate(Math::Normalize(basePos) * (0.35f + 1.5f + shake));
                    }
                }

                // 追従はロック（固定）、上下のゆらゆらも完全に停止
                Vector3 pos = blockTargetPos_[i];
                armorBlocks[i]->SetTranslate(pos);

                if (funnelTimers_[i] >= 1.5f) {
                    funnelStates_[i] = 13; // 縮小へ
                    funnelTimers_[i] = 0.0f;
                }
            }
            else if (funnelStates_[i] == 13) {
                // 13: 縮小（0.5秒）
                funnelTimers_[i] += deltaTime;
                float t = std::min(funnelTimers_[i] / 0.5f, 1.0f);
                float shrinkT = 1.0f - t;
                
                if (laser) { 
                    laser->SetIsVisible(true);
                    laser->SetScale({ 1.0f * shrinkT, scaleY, 1.0f * shrinkT }); 
                    laser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    laser->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
                    laser->SetCollisionAttribute(0); 
                    Vector3 uvScale = { 1.0f, 15.0f, 1.0f };
                    Vector3 uvTranslate = { 0.0f, funnelTimers_[i] * -30.0f, 0.0f };
                    laser->SetUVTransform(math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, uvTranslate));
                    laser->UpdateWorldMatrix();
                }
                if (coreLaser) { 
                    coreLaser->SetIsVisible(true);
                    coreLaser->SetScale({ 0.4f * shrinkT, scaleY, 0.4f * shrinkT }); 
                    coreLaser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    coreLaser->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    coreLaser->SetCollisionAttribute(0); 
                    Vector3 coreUvScale = { 1.0f, 20.0f, 1.0f };
                    Vector3 coreUvTranslate = { 0.0f, funnelTimers_[i] * -50.0f, 0.0f };
                    coreLaser->SetUVTransform(math.MakeAffineMatrix(coreUvScale, { 0.0f, 0.0f, 0.0f }, coreUvTranslate));
                    coreLaser->UpdateWorldMatrix();
                }

                // --- 子ブロック（Shard）の収束演出 ---
                for (auto* child : armorBlocks[i]->GetChildren()) {
                    if (child && child->GetName().find("Shard") != std::string::npos) {
                        Vector3 basePos = child->GetTranslate();
                        child->SetTranslate(Math::Normalize(basePos) * (0.35f + 1.5f * shrinkT));
                    }
                }

                Vector3 pos = blockTargetPos_[i];
                // 縮小中もゆらゆらは停止したまま
                armorBlocks[i]->SetTranslate(pos);

                if (t >= 1.0f) {
                    // 発射シーケンス終了、デコイ待機状態へ戻る
                    funnelStates_[i] = 0;
                    if (laser) { laser->SetIsVisible(false); laser->SetScale({ 0.0f, 0.0f, 0.0f }); laser->SetCollisionAttribute(0); laser->UpdateWorldMatrix(); }
                    if (coreLaser) { coreLaser->SetIsVisible(false); coreLaser->SetScale({ 0.0f, 0.0f, 0.0f }); coreLaser->SetCollisionAttribute(0); coreLaser->UpdateWorldMatrix(); }

                    // ビーム終了：地形属性判定を元に戻す
                    if (armorBlocks[i]) {
                        armorBlocks[i]->SetCollisionAttribute(kGround);
                    }
                }
            }
        }

        // 3ウェーブ（5秒×3）が終わったらフェーズ移行
        if (animTimer_ >= 15.0f) {
            animPhase_ = 93;
            animTimer_ = 0.0f;
            for (auto* laser : activeLasers_) {
                if (laser) {
                    laser->SetIsVisible(false);
                    laser->SetScale({ 0.0f, 0.0f, 0.0f }); // 見えなくする
                    laser->SetParent(nullptr); // 親子関係解除
                    CollisionManager::GetInstance()->RemoveObject(laser);
                    laser->UpdateWorldMatrix();
                }
            }
            for (auto* core : activeCoreLasers_) {
                if (core) {
                    core->SetIsVisible(false);
                    core->SetScale({ 0.0f, 0.0f, 0.0f });
                    core->SetParent(nullptr);
                    CollisionManager::GetInstance()->RemoveObject(core);
                    core->UpdateWorldMatrix();
                }
            }
            activeLasers_.clear();
            activeCoreLasers_.clear();
            
            // 帰還のための初期位置を保存
            blockStartPos_.assign(armorBlocks.size(), {});
            blockStartScale_.assign(armorBlocks.size(), {});
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (!armorBlocks[i]) continue;
                blockStartPos_[i] = armorBlocks[i]->GetTranslate();
                blockStartScale_[i] = armorBlocks[i]->GetScale();
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
    if (boss_) {
        for (auto* block : boss_->GetArmorBlocks()) {
            if (block) {
                block->SetCollisionAttribute(kGround);
            }
        }
    }

    for (auto* laser : activeLasers_) {
        if (laser) {
            laser->SetIsVisible(false);
            laser->SetScale({ 0.0f, 0.0f, 0.0f });
            laser->SetCollisionAttribute(0);
            laser->SetParent(nullptr);
            CollisionManager::GetInstance()->RemoveObject(laser);
            laser->UpdateWorldMatrix();
        }
    }
    for (auto* core : activeCoreLasers_) {
        if (core) {
            core->SetIsVisible(false);
            core->SetScale({ 0.0f, 0.0f, 0.0f });
            core->SetCollisionAttribute(0);
            core->SetParent(nullptr);
            CollisionManager::GetInstance()->RemoveObject(core);
            core->UpdateWorldMatrix();
        }
    }
    activeLasers_.clear();
    activeCoreLasers_.clear();
}
