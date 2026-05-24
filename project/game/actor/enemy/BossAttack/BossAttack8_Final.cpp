#include "BossAttack8_Final.h"
#include "../BossCore.h"
#include "Object3d.h" 
#include "./easing.h" 
#include <algorithm>
#include <cmath>
#include <numbers>
#include "DebugConsole.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "CollisionManager.h"
#include "../../MapBlock.h"
#include "../EnemyBomb.h"
#include "CollisionConfig.h"

BossAttack8_Final::~BossAttack8_Final() {
    Finalize();
}

void BossAttack8_Final::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);
    meteors_.clear();
    areaWarnings_.clear();
    activeLasers_.clear();
    activeCoreLasers_.clear();
    coreBeams_.clear();
    coreBeamCores_.clear();
    
    rainTimer_ = 0.0f;
    rainCount_ = 0;
    rushCount_ = 0;
    spawnCount_ = 0;
    spawnTimer_ = 0.0f;
    wallStep_ = 0;
    
    animPhase_ = 80;
    animStartPos_ = boss->GetTranslate();

    // 1. ステージ上のブロック(MapBlock)を全消去（非表示・無効化）
    for (MapBlock* mb : MapBlock::s_activeBlocks) {
        if (mb) {
            mb->SetScale({ 0.0f, 0.0f, 0.0f });
            mb->SetCollisionAttribute(0);
        }
    }

    // 2. ボスの装甲ブロックを強制的に6個の挙動にするため、余分なものは非表示にする
    auto& armorBlocks = boss->GetArmorBlocks();
    if (armorBlocks.size() > 6) {
        // 多すぎる分は非表示にするだけ（isDeadにしたり配列を削ると子オブジェクト等の参照で例外エラーになるため残す）
        for (size_t i = 6; i < armorBlocks.size(); ++i) {
            if (armorBlocks[i]) {
                armorBlocks[i]->SetScale({ 0,0,0 });
                armorBlocks[i]->SetCollisionAttribute(0);
            }
        }
    } else if (armorBlocks.size() < 6) {
        // 足りない分を追加生成
        BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene();
        int need = 6 - (int)armorBlocks.size();
        for (int i = 0; i < need; ++i) {
            auto block = std::make_unique<Object3d>();
            block->Initialize(boss->GetCommon());
            block->SetModel("enemy_block");
            block->SetColliderType(ColliderType::kOBB);
            block->SetCollisionAttribute(kEnemyAttack | kGround);
            block->SetCollisionMask(kPlayer);
            block->SetName("ArmorBlock_Spawned");
            
            Object3d* blockPtr = block.get();
            if (currentScene) currentScene->AddObject(std::move(block));
            boss->AddArmorBlock(blockPtr);
        }
    }

    // すべての armorBlock（先頭6個）を UpgradeToFunnel にかける (二重生成は UpgradeToFunnel 側で防がれているので安全)
    for (size_t i = 0; i < std::min(armorBlocks.size(), (size_t)6); ++i) {
        if (armorBlocks[i]) {
            boss->UpgradeToFunnel(armorBlocks[i]);
        }
    }

    DebugConsole::GetInstance()->AddLog("【最終奥義】 フェーズ80：準備・ボス上昇開始！");
}

void BossAttack8_Final::Update(BossCore* boss, float deltaTime) {
    BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene();
    auto& armorBlocks = boss->GetArmorBlocks();
    Object3d* target = boss->GetTarget();

    // ボス本体の高速回転（オーラ演出） (Phase 80〜84の間)
    if (animPhase_ < 86 && animPhase_ >= 80 && animPhase_ != 811) {
        Vector3 bossRot = boss->GetRotation();
        bossRot.y += 15.0f * deltaTime;
        boss->SetRotation(bossRot);
        boss->GetTransform()->isQuaternionMaster = false;
    }

    // --- Phase 80: ボス上昇 ＆ 巨大メテオ1個だけ事前生成 ---
    if (animPhase_ == 80) {
        if (animTimer_ == 0.0f) {
            auto meteor = std::make_unique<Object3d>();
            meteor->Initialize(boss->GetCommon());
            if (!armorBlocks.empty() && armorBlocks[0]) {
                meteor->SetModel(armorBlocks[0]->GetModelName());
            } else {
                meteor->SetModel("enemy_block");
            }
            meteor->SetName("Giant_Meteor");
            meteor->SetTranslate({ 0.0f, 1000.0f, 0.0f }); // 上空待機
            meteor->SetScale({ 15.0f, 15.0f, 15.0f });
            meteor->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
            meteor->SetColliderType(ColliderType::kOBB);
            meteor->SetCollisionAttribute(kEnemyAttack);
            meteor->SetCollisionMask(kPlayer | kGround);
            meteor->SetAttackDamage(boss->GetAttackParams().damageFinal);
            
            CollisionManager::GetInstance()->AddObject(meteor.get());
            meteors_.push_back(meteor.get());
            if (currentScene) currentScene->AddObject(std::move(meteor));
        }

        animTimer_ += deltaTime;
        float duration = 2.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        Vector3 pos = boss->GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
        pos.y = Math::Lerp(animStartPos_.y, 25.0f, easeT); // はるか上空へ
        pos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        boss->SetTranslate(pos);

        float flash = (std::sin(animTimer_ * 20.0f) + 1.0f) * 0.5f;
        boss->SetColor({ 1.0f, flash * 0.3f, flash * 0.3f, 1.0f });

        if (t >= 1.0f) {
            animPhase_ = 810; // 突進構えへ
            animTimer_ = 0.0f;
            rushCount_ = 0;
            DebugConsole::GetInstance()->AddLog("【最終奥義】 突進3連撃！");
        }
    }
    // --- Phase 81: 突進3連撃 (810: 構え, 811: 突進) ---
    else if (animPhase_ == 810) { // 構え
        if (animTimer_ == 0.0f) {
            animStartPos_ = boss->GetTranslate();
            Vector3 targetPos = target ? target->GetWorldPosition() : Vector3{0,0,0};
            Vector3 toPlayer = { targetPos.x - animStartPos_.x, 0.0f, targetPos.z - animStartPos_.z };
            if (Math::Length(toPlayer) < 0.1f) toPlayer = {0,0,1};
            toPlayer = Math::Normalize(toPlayer);
            
            // プレイヤーの反対側へ回り込む
            animStartPos_ = { targetPos.x - toPlayer.x * 60.0f, 2.0f, targetPos.z - toPlayer.z * 60.0f };
            boss->SetTranslate(animStartPos_);

            // 突進目標地点
            animTargetPos_ = { animStartPos_.x + toPlayer.x * 120.0f, 2.0f, animStartPos_.z + toPlayer.z * 120.0f };

            // 予測線表示
            Object3d* warning = boss->GetWarningArea();
            if (warning) {
                warning->SetParent(nullptr);
                warning->SetCollisionAttribute(0);
                warning->SetMaterialType(0);
                warning->SetEmissive(3.0f);
                warning->SetTexture("Resources/sprite/yazirusi1.png");
                
                float angleY = std::atan2(toPlayer.x, toPlayer.z);
                warning->SetRotation({0.0f, angleY, 0.0f});
                warning->SetScale({ 10.0f, 0.1f, 120.0f });
                warning->SetTranslate({ animStartPos_.x + toPlayer.x * 60.0f, 0.7f, animStartPos_.z + toPlayer.z * 60.0f });
                warning->GetTransform()->isQuaternionMaster = false;
                warning->SetColor({ 1.0f, 0.5f, 0.0f, 0.8f });
            }

            // ブロックを剣のように配置 (先頭6個のみ使用)
            size_t useCount = std::min(armorBlocks.size(), (size_t)6);
            for (size_t i = 0; i < useCount; ++i) {
                if (!armorBlocks[i]) continue;
                armorBlocks[i]->SetAttackDamage(boss->GetAttackParams().damageRush);
                armorBlocks[i]->SetParent(nullptr);
                float offset = (float)i * 3.0f;
                Vector3 localPos = { 0.0f, 0.0f, offset };
                float angleY = std::atan2(toPlayer.x, toPlayer.z);
                Vector3 worldPos = {
                    animStartPos_.x + localPos.z * std::sin(angleY),
                    animStartPos_.y,
                    animStartPos_.z + localPos.z * std::cos(angleY)
                };
                armorBlocks[i]->SetTranslate(worldPos);
                armorBlocks[i]->SetRotation({0, angleY, 0});
                armorBlocks[i]->SetScale({ 2.0f, 2.0f, 4.0f });
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        animTimer_ += deltaTime;
        float duration = 2.0f; // 突進するまでのタメ時間（予測線表示）を2倍に延長
        
        Object3d* warning = boss->GetWarningArea();
        if (warning) {
            static Math math;
            Vector3 uvScale = { 3.0f, 20.0f, 1.0f };
            Vector3 uvTranslate = { 0.0f, animTimer_ * 10.0f, 0.0f };
            warning->SetUVTransform(math.MakeAffineMatrix(uvScale, {0,0,0}, uvTranslate));
        }

        if (animTimer_ >= duration) {
            animPhase_ = 811;
            animTimer_ = 0.0f;
            if (warning) warning->SetColor({ 1.0f, 0.0f, 0.0f, 0.9f });
        }
    }
    else if (animPhase_ == 811) { // 突進
        animTimer_ += deltaTime;
        float duration = 0.6f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = std::pow(t, 2.0f);

        Vector3 pos = Math::Lerp(animStartPos_, animTargetPos_, easeT);
        boss->SetTranslate(pos);
        boss->UpdateWorldMatrix();
        
        Vector3 toPlayer = { animTargetPos_.x - animStartPos_.x, 0.0f, animTargetPos_.z - animStartPos_.z };
        if (Math::Length(toPlayer) > 0.001f) toPlayer = Math::Normalize(toPlayer);
        float angleY = std::atan2(toPlayer.x, toPlayer.z);

        boss->SetRotation({0, angleY, animTimer_ * 30.0f});
        boss->GetTransform()->isQuaternionMaster = false;

        size_t useCount = std::min(armorBlocks.size(), (size_t)6);
        for (size_t i = 0; i < useCount; ++i) {
            if (!armorBlocks[i]) continue;
            float offset = (float)i * 3.0f;
            Vector3 localPos = { 0.0f, 0.0f, offset };
            Vector3 worldPos = {
                pos.x + localPos.z * std::sin(angleY),
                pos.y,
                pos.z + localPos.z * std::cos(angleY)
            };
            armorBlocks[i]->SetTranslate(worldPos);
            armorBlocks[i]->SetRotation({0, angleY, animTimer_ * 30.0f});
            armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            rushCount_++;
            if (rushCount_ < 3) {
                animPhase_ = 810; 
                animTimer_ = 0.0f;
            } else {
                Object3d* warning = boss->GetWarningArea();
                if (warning) warning->SetScale({0,0,0});
                
                animPhase_ = 82; 
                animTimer_ = 0.0f;
                DebugConsole::GetInstance()->AddLog("【最終奥義】 ファンネル＆ボム召喚！");
            }
        }
    }
    // --- Phase 82: ファンネル＆ボム召喚 ---
    else if (animPhase_ == 82) {
        if (animTimer_ == 0.0f) {
            animStartPos_ = boss->GetTranslate();
            
            funnelStates_.clear();
            funnelTimers_.clear();
            activeLasers_.clear();
            activeCoreLasers_.clear();
            laserLengths_.clear();
            laserDelayTimers_.clear();
            
            size_t useCount = std::min(armorBlocks.size(), (size_t)6);
            for (size_t i = 0; i < useCount; ++i) {
                if (!armorBlocks[i]) continue;
                
                // ワールド座標で独立して動かすために親を外す
                Vector3 worldPos = armorBlocks[i]->GetWorldPosition();
                armorBlocks[i]->SetParent(nullptr);
                armorBlocks[i]->SetTranslate(worldPos);

                // ユーザー指定: ブロックサイズを 1x1x1 に縮小する
                armorBlocks[i]->SetScale({ 1.0f, 1.0f, 1.0f }); 
                
                funnelStates_.push_back(0); 
                funnelTimers_.push_back(0.0f);
                
                // ==========================================
                // 1. 赤いオーラ（外側のビーム）の生成
                // ==========================================
                auto laser = std::make_unique<Object3d>();
                laser->Initialize(boss->GetCommon());
                laser->SetModel("Cylinder");
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
                laser->SetScale({ 0.0f, 0.0f, 0.0f });
                laser->SetCollisionAttribute(0);
                
                CollisionManager::GetInstance()->RemoveObject(laser.get());
                CollisionManager::GetInstance()->AddObject(laser.get());
                
                laser->SetParent(armorBlocks[i]);
                float rotX90 = std::numbers::pi_v<float> / 2.0f;
                laser->SetRotation({ rotX90, 0.0f, 0.0f });
                laser->SetTranslate({ 0.0f, 0.0f, 80.0f });
                laser->GetTransform()->isQuaternionMaster = false;
                
                activeLasers_.push_back(laser.get());
                laserLengths_.push_back(160.0f);
                laserDelayTimers_.push_back(0.0f);
                if (currentScene) currentScene->AddObject(std::move(laser));

                // ==========================================
                // 2. 白いコア（内側のビーム）の生成
                // ==========================================
                auto coreLaser = std::make_unique<Object3d>();
                coreLaser->Initialize(boss->GetCommon());
                coreLaser->SetModel("Cylinder");
                coreLaser->SetBlendMode(BlendMode::kAdd);
                coreLaser->SetEmissive(8.0f);
                coreLaser->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                coreLaser->SetTexture("Resources/sprite/beamNoice.png");
                coreLaser->SetMaterialType(9);
                coreLaser->SetUVTransform(uvMat);
                
                coreLaser->SetColliderConfig(cConfig);
                coreLaser->SetScale({ 0.0f, 0.0f, 0.0f });
                coreLaser->SetCollisionAttribute(0);
                
                CollisionManager::GetInstance()->RemoveObject(coreLaser.get());
                CollisionManager::GetInstance()->AddObject(coreLaser.get());
                
                coreLaser->SetParent(armorBlocks[i]);
                coreLaser->SetRotation({ rotX90, 0.0f, 0.0f });
                coreLaser->SetTranslate({ 0.0f, 0.0f, 80.0f });
                coreLaser->GetTransform()->isQuaternionMaster = false;
                
                activeCoreLasers_.push_back(coreLaser.get());
                if (currentScene) currentScene->AddObject(std::move(coreLaser));
            }
            
            spawnCount_ = 0;
            spawnTimer_ = 0.0f;
        }
        
        animTimer_ += deltaTime;
        
        // コアの移動
        Vector3 coreTarget = target ? target->GetWorldPosition() : Vector3{0,0,0};
        coreTarget.y = 18.0f; // プレイヤーの頭上
        Vector3 corePos = boss->GetTranslate();
        corePos = Math::Lerp(corePos, coreTarget, 3.0f * deltaTime);
        boss->SetTranslate(corePos);
        
        // ボム召喚 (1.5秒ごとに合計4体)
        spawnTimer_ += deltaTime;
        if (spawnTimer_ >= 1.5f && spawnCount_ < 4) {
            spawnTimer_ = 0.0f;
            spawnCount_++;
            
            auto bomb = std::make_unique<EnemyBomb>();
            bomb->Initialize(boss->GetCommon(), "sphere");
            Object3d::EntityParameter param;
            param.gravity = 50.0f;
            param.maxFallSpeed = 60.0f;
            bomb->param_ = param;
            bomb->SetTarget(target);
            
            Vector3 spawnPos = boss->GetTranslate();
            float angle = spawnCount_ * (3.1415f * 2.0f / 4.0f);
            spawnPos.x += std::cos(angle) * 3.0f;
            spawnPos.z += std::sin(angle) * 3.0f;
            bomb->SetTranslate(spawnPos);
            
            Vector3 initialVel = { std::cos(angle) * 10.0f, 5.0f, std::sin(angle) * 10.0f };
            bomb->SetVelocity(initialVel);
            
            if (currentScene) currentScene->AddObject(std::move(bomb));
        }
        
        // ファンネル処理
        size_t useCount = std::min(armorBlocks.size(), (size_t)6);
        for (size_t i = 0; i < useCount; ++i) {
            if (!armorBlocks[i]) continue;
            
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
                currentRot.z = animTimer_ * 10.0f;
                
                block->SetRotation(currentRot);
                block->GetTransform()->isQuaternionMaster = false;
            };

            // ★ レーザーのレイキャスト処理 (フェーズ11〜13で使う)
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

            // ★ ヒット距離を使った遅延付きの長さ復元処理
            if (i < laserLengths_.size()) {
                if (actualDist < laserLengths_[i]) {
                    laserLengths_[i] = actualDist;
                    laserDelayTimers_[i] = 0.3f;
                } else {
                    if (laserDelayTimers_[i] > 0.0f) {
                        laserDelayTimers_[i] -= deltaTime;
                    } else {
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

            // 状態に応じた処理
            if (funnelStates_[i] == 0) { // 待機
                TrackPlayer(armorBlocks[i], target, 2.0f * deltaTime);
                
                float angle = animTimer_ * 2.0f + i * (3.1415f * 2.0f / useCount);
                Vector3 tgtPos = target ? target->GetWorldPosition() : Vector3{0,0,0};
                tgtPos.x += std::cos(angle) * 15.0f;
                tgtPos.z += std::sin(angle) * 15.0f;
                tgtPos.y += 5.0f + std::sin(animTimer_ * 5.0f + i) * 2.0f;
                
                Vector3 pos = Math::Lerp(armorBlocks[i]->GetTranslate(), tgtPos, 5.0f * deltaTime);
                armorBlocks[i]->SetTranslate(pos);

                if (laser) { laser->SetScale({ 0.0f, 0.0f, 0.0f }); laser->SetCollisionAttribute(0); }
                if (coreLaser) { coreLaser->SetScale({ 0.0f, 0.0f, 0.0f }); coreLaser->SetCollisionAttribute(0); }
                
                if (animTimer_ > 2.0f + i * 0.8f) {
                    funnelStates_[i] = 11; 
                    funnelTimers_[i] = 0.0f;

                    // 自傷防止のため、ビームを撃つファンネルの判定を一時的に消す
                    if (armorBlocks[i]) {
                        armorBlocks[i]->SetCollisionAttribute(0);
                    }
                }
            }
            else if (funnelStates_[i] == 11) { // 予兆（1.0秒）
                funnelTimers_[i] += deltaTime;
                
                if (laser) {
                    laser->SetScale({ 0.02f, scaleY, 0.02f });
                    laser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    laser->SetCollisionAttribute(0);
                }
                if (coreLaser) {
                    coreLaser->SetScale({ 0.0f, scaleY, 0.0f });
                    coreLaser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    coreLaser->SetCollisionAttribute(0);
                }

                // --- 子ブロック（Shard）の展開演出 ---
                float expandT = std::min(funnelTimers_[i] / 1.0f, 1.0f);
                float expandOffset = Easing::OutBack(expandT) * 1.5f; // 最大1.5m外側に展開
                for (auto* child : armorBlocks[i]->GetChildren()) {
                    if (child && child->GetName().find("Shard") != std::string::npos) {
                        Vector3 basePos = child->GetTranslate();
                        Vector3 dir = Math::Normalize(basePos);
                        if (Math::Length(dir) < 0.1f) dir = { 0.0f, 1.0f, 0.0f };
                        child->SetTranslate(dir * (0.35f + expandOffset));
                    }
                }

                // 最初の0.5秒間はプレイヤーを追従
                if (funnelTimers_[i] < 0.5f) {
                    TrackPlayer(armorBlocks[i], target, 8.0f * deltaTime);
                }

                if (funnelTimers_[i] >= 1.0f) {
                    funnelStates_[i] = 12; 
                    funnelTimers_[i] = 0.0f;
                }
            }
            else if (funnelStates_[i] == 12) { // 発射（1.0秒）
                funnelTimers_[i] += deltaTime;
                
                if (laser) {
                    laser->SetScale({ 1.0f, scaleY, 1.0f });
                    laser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    laser->SetCollisionAttribute(kEnemyAttack);
                    laser->SetAttackDamage(boss->GetAttackParams().damageFunnels);
                    
                    static Math math;
                    Vector3 uvScale = { 1.0f, 15.0f, 1.0f };
                    Matrix4x4 uvMat = math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, { 0.0f, funnelTimers_[i]*5.0f, 0.0f });
                    laser->SetUVTransform(uvMat);
                }
                if (coreLaser) {
                    coreLaser->SetScale({ 0.4f, scaleY, 0.4f });
                    coreLaser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    coreLaser->SetCollisionAttribute(0);
                    
                    static Math math;
                    Vector3 uvScale = { 1.0f, 15.0f, 1.0f };
                    Matrix4x4 uvMat = math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, { 0.0f, funnelTimers_[i]*5.0f, 0.0f });
                    coreLaser->SetUVTransform(uvMat);
                }

                // --- 子ブロック（Shard）の展開を維持（少し震わせる） ---
                float shake = std::sin(funnelTimers_[i] * 50.0f) * 0.1f;
                for (auto* child : armorBlocks[i]->GetChildren()) {
                    if (child && child->GetName().find("Shard") != std::string::npos) {
                        Vector3 basePos = child->GetTranslate();
                        child->SetTranslate(Math::Normalize(basePos) * (0.35f + 1.5f + shake));
                    }
                }
                
                if (funnelTimers_[i] >= 1.0f) {
                    funnelStates_[i] = 13; 
                    funnelTimers_[i] = 0.0f;
                }
            }
            else if (funnelStates_[i] == 13) { // 縮小（0.5秒）
                funnelTimers_[i] += deltaTime;
                float t = std::min(funnelTimers_[i] / 0.5f, 1.0f);
                float shrinkT = 1.0f - t;

                if (laser) {
                    laser->SetScale({ 1.0f * shrinkT, scaleY, 1.0f * shrinkT });
                    laser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    laser->SetCollisionAttribute(0);
                }
                if (coreLaser) {
                    coreLaser->SetScale({ 0.4f * shrinkT, scaleY, 0.4f * shrinkT });
                    coreLaser->SetTranslate({ 0.0f, 0.0f, offsetZ });
                    coreLaser->SetCollisionAttribute(0);
                }

                // --- 子ブロック（Shard）の収束演出 ---
                for (auto* child : armorBlocks[i]->GetChildren()) {
                    if (child && child->GetName().find("Shard") != std::string::npos) {
                        Vector3 basePos = child->GetTranslate();
                        child->SetTranslate(Math::Normalize(basePos) * (0.35f + 1.5f * shrinkT));
                    }
                }

                if (t >= 1.0f) {
                    funnelStates_[i] = 0; 
                    if (laser) { laser->SetScale({ 0.0f, 0.0f, 0.0f }); laser->SetCollisionAttribute(0); }
                    if (coreLaser) { coreLaser->SetScale({ 0.0f, 0.0f, 0.0f }); coreLaser->SetCollisionAttribute(0); }

                    // ビーム終了：地形属性判定を元に戻す
                    if (armorBlocks[i]) {
                        armorBlocks[i]->SetCollisionAttribute(kGround);
                    }
                }
            }
        }
        
        if (animTimer_ >= 8.5f) {
            for (auto* laser : activeLasers_) {
                if (laser) {
                    laser->SetScale({0,0,0});
                    laser->SetCollisionAttribute(0);
                    laser->SetParent(nullptr);
                    CollisionManager::GetInstance()->RemoveObject(laser);
                }
            }
            activeLasers_.clear();

            for (auto* core : activeCoreLasers_) {
                if (core) {
                    core->SetScale({0,0,0});
                    core->SetCollisionAttribute(0);
                    core->SetParent(nullptr);
                    CollisionManager::GetInstance()->RemoveObject(core);
                }
            }
            activeCoreLasers_.clear();

            // 装甲ブロックのShardモデルの座標をデフォルト位置に収束させてクリーンアップ
            float offset = 0.175f;
            Vector3 offsets[8] = {
                {-offset, -offset, -offset}, {offset, -offset, -offset},
                {-offset,  offset, -offset}, {offset,  offset, -offset},
                {-offset, -offset,  offset}, {offset, -offset,  offset},
                {-offset,  offset,  offset}, {offset,  offset,  offset}
            };
            for (size_t i = 0; i < useCount; ++i) {
                if (!armorBlocks[i]) continue;
                armorBlocks[i]->SetCollisionAttribute(kEnemyAttack | kGround);
                int shardIdx = 0;
                for (auto* child : armorBlocks[i]->GetChildren()) {
                    if (child && child->GetName().find("Shard") != std::string::npos && shardIdx < 8) {
                        child->SetTranslate(offsets[shardIdx++]);
                    }
                }
            }
            
            animPhase_ = 83; 
            animTimer_ = 0.0f;
            DebugConsole::GetInstance()->AddLog("【最終奥義】 壁迫り＆4分割ビーム！");
        }
    }
    // --- Phase 83: 壁迫り＆4分割ビーム ---
    else if (animPhase_ == 83) {
        if (animTimer_ == 0.0f) {
            float blockWidth = 30.0f;
            size_t useCount = std::min(armorBlocks.size(), (size_t)6);
            for (size_t i = 0; i < useCount; ++i) {
                if (!armorBlocks[i]) continue;
                armorBlocks[i]->SetParent(nullptr);
                
                float centerIndex = (useCount - 1.0f) / 2.0f;
                float offset = -(static_cast<float>(i) - centerIndex) * blockWidth;
                
                float zPos = (i % 2 == 0) ? 120.0f : -120.0f;
                Vector3 targetP = { offset, 2.0f, zPos };
                
                armorBlocks[i]->SetTranslate(targetP);
                armorBlocks[i]->SetScale({ blockWidth, 10.0f, 4.0f });
                armorBlocks[i]->SetRotation({0,0,0});
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
                armorBlocks[i]->SetCollisionAttribute(kEnemyAttack);
                armorBlocks[i]->SetAttackDamage(boss->GetAttackParams().damageWall);
            }
            
            coreBeams_.clear();
            areaWarnings_.clear(); 
            for (int i = 0; i < 4; ++i) {
                auto laser = std::make_unique<Object3d>();
                laser->Initialize(boss->GetCommon());
                laser->SetModel("Cylinder");
                laser->SetBlendMode(BlendMode::kAdd);
                laser->SetEmissive(8.0f);
                laser->SetColor({ 1.0f, 1.0f, 0.0f, 0.9f }); 
                laser->SetTexture("Resources/sprite/beamNoice.png");
                laser->SetMaterialType(9);
                laser->SetColliderType(ColliderType::kOBB);
                laser->SetScale({ 0.0f, 0.0f, 0.0f });
                laser->SetCollisionAttribute(0);
                
                CollisionManager::GetInstance()->AddObject(laser.get());
                coreBeams_.push_back(laser.get());
                if (currentScene) currentScene->AddObject(std::move(laser));
                
                auto warn = std::make_unique<Object3d>();
                warn->Initialize(boss->GetCommon());
                warn->SetModel("yazirusi.gltf");
                warn->SetBlendMode(BlendMode::kNormal);
                warn->SetScale({ 0, 0, 0 });
                warn->SetCollisionAttribute(0);
                warn->SetEmissive(2.0f);
                areaWarnings_.push_back(warn.get());
                if (currentScene) currentScene->AddObject(std::move(warn));
            }
        }
        
        animTimer_ += deltaTime;
        
        // 壁迫り
        float wallProgress = std::min(animTimer_ / 6.0f, 1.0f);
        float wallEase = Easing::InOutSine(wallProgress);
        size_t useCount = std::min(armorBlocks.size(), (size_t)6);
        for (size_t i = 0; i < useCount; ++i) {
            if (!armorBlocks[i]) continue;
            Vector3 pos = armorBlocks[i]->GetTranslate();
            float startZ = (i % 2 == 0) ? 120.0f : -120.0f;
            float endZ = (i % 2 == 0) ? 10.0f : -10.0f; 
            pos.z = Math::Lerp(startZ, endZ, wallEase);
            armorBlocks[i]->SetTranslate(pos);
        }
        
        // コアは上空で待機
        Vector3 corePos = boss->GetTranslate();
        corePos.x = 0; corePos.y = 20.0f; corePos.z = 0;
        boss->SetTranslate(corePos);
        
        // 4分割ビーム (-45, -15, 15, 45)
        for (int i = 0; i < 4; ++i) {
            float laneX = -45.0f + i * 30.0f;
            float startTime = 1.0f + i * 1.0f; 
            
            if (animTimer_ >= startTime && animTimer_ < startTime + 2.0f) {
                float localTime = animTimer_ - startTime;
                Object3d* laser = coreBeams_[i];
                Object3d* warn = areaWarnings_[i];
                
                if (localTime < 1.0f) {
                    if (warn) {
                        warn->SetTranslate({ laneX, 0.1f, 0.0f });
                        warn->SetScale({ 30.0f, 0.1f, 150.0f });
                        warn->SetTexture("Resources/sprite/yazirusi1.png");
                        warn->SetColor({ 1.0f, 0.0f, 0.0f, 0.5f });
                    }
                } else if (localTime >= 1.0f && localTime < 1.8f) {
                    if (warn) warn->SetScale({ 0,0,0 });
                    if (laser) {
                        laser->SetTranslate({ laneX, 50.0f, 0.0f }); 
                        laser->SetRotation({ 0.0f, 0.0f, 0.0f }); 
                        laser->SetScale({ 15.0f, 100.0f, 15.0f });
                        laser->SetCollisionAttribute(kEnemyAttack);
                        laser->SetAttackDamage(boss->GetAttackParams().damageFinal);
                        
                        static Math math;
                        Vector3 uvScale = { 1.0f, 15.0f, 1.0f };
                        Matrix4x4 uvMat = math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, { 0.0f, localTime*5.0f, 0.0f });
                        laser->SetUVTransform(uvMat);
                    }
                } else {
                    if (laser) {
                        laser->SetScale({0,0,0});
                        laser->SetCollisionAttribute(0);
                    }
                }
            } else {
                if (coreBeams_[i]) coreBeams_[i]->SetScale({0,0,0});
                if (areaWarnings_[i]) areaWarnings_[i]->SetScale({0,0,0});
            }
        }
        
        if (animTimer_ >= 7.0f) {
            for (auto* laser : coreBeams_) {
                if (laser) { laser->SetScale({0,0,0}); laser->SetCollisionAttribute(0); /* laser->isDead = true; */ }
            }
            coreBeams_.clear();
            for (auto* warn : areaWarnings_) {
                if (warn) { warn->SetScale({0,0,0}); warn->SetCollisionAttribute(0); /* warn->isDead = true; */ }
            }
            areaWarnings_.clear();
            
            animPhase_ = 84; 
            animTimer_ = 0.0f;
            DebugConsole::GetInstance()->AddLog("【最終奥義】 巨大ブロック落下！！");
        }
    }
    // --- Phase 84: 巨大ブロック落下（トドメ）---
    else if (animPhase_ == 84) {
        Object3d* warning = boss->GetWarningArea();
        if (warning) {
            warning->SetTranslate({ 0.0f, 0.05f, 0.0f });
            warning->SetScale({ 30.0f, 0.7f, 30.0f }); 
            warning->SetColor({ 1.0f, 0.0f, 0.0f, 0.9f });
            warning->UpdateWorldMatrix();
        }
        
        if (animTimer_ == 0.0f) {
            if (!meteors_.empty() && meteors_[0]) {
                meteors_[0]->SetTranslate({ 0.0f, 150.0f, 0.0f }); 
                meteors_[0]->SetScale({ 30.0f, 30.0f, 30.0f });
                meteors_[0]->SetCollisionAttribute(kEnemyAttack);
            }
        }
        
        animTimer_ += deltaTime;
        
        if (!meteors_.empty() && meteors_[0]) {
            Vector3 mPos = meteors_[0]->GetTranslate();
            if (animTimer_ > 1.0f && mPos.y > 0.0f) {
                mPos.y -= 150.0f * deltaTime; 
                if (mPos.y <= 0.0f) {
                    mPos.y = 0.0f; // 着地
                    meteors_[0]->SetTranslate(mPos);
                    
                    if (warning) warning->SetScale({0,0,0});
                    
                    // 攻撃を終了するフラグを立てる
                    isFinished_ = true;
                    DebugConsole::GetInstance()->AddLog("【最終奥義】巨大ブロック直撃！コアが剥き出しになり、中央へ落下する！");
                    
                    // プレイヤーの攻撃を待つためにHPを1にする
                    if (boss->param_.has_value()) {
                        boss->param_->hp = 1.0f;
                    }
                    
                    // トドメ待ちモードを有効にする
                    boss->SetWaitingForFinisher(true);
                    boss->SetWaitingForDeath(true);
                    
                    // 中央の上空へワープ
                    boss->SetTranslate({ 0.0f, 25.0f, 0.0f });
                    boss->SetRotation({ 0.0f, 0.0f, 0.0f });
                    boss->SetScale({ 1.0f, 1.0f, 1.0f });
                    boss->UpdateWorldMatrix();
                    
                    // 落下フラグと速度の初期化
                    boss->StartFinisherFall();
                    
                    // 巨大メテオは消去（非表示・当たり判定無効）
                    if (!meteors_.empty() && meteors_[0]) {
                        meteors_[0]->SetScale({ 0.0f, 0.0f, 0.0f });
                        meteors_[0]->SetCollisionAttribute(0);
                    }
                    
                    return; // 即座に関数を抜ける
                }
                meteors_[0]->SetTranslate(mPos);
            }
        }
    }
}

void BossAttack8_Final::Finalize() {
    for (auto* laser : activeLasers_) {
        if (laser) {
            laser->SetScale({ 0.0f, 0.0f, 0.0f });
            laser->SetCollisionAttribute(0);
            laser->SetParent(nullptr); // 追加: 行列更新時の親アクセス違反防止
            // laser->isDead = true; // 例外エラー対策: 削除せずに非表示化
        }
    }
    activeLasers_.clear();

    for (auto* core : activeCoreLasers_) {
        if (core) {
            core->SetScale({ 0.0f, 0.0f, 0.0f });
            core->SetCollisionAttribute(0);
            core->SetParent(nullptr);
        }
    }
    activeCoreLasers_.clear();

    for (auto* beam : coreBeams_) {
        if (beam) {
            beam->SetScale({ 0.0f, 0.0f, 0.0f });
            beam->SetCollisionAttribute(0);
            beam->SetParent(nullptr); // 追加: 行列更新時の親アクセス違反防止
            // beam->isDead = true;
        }
    }
    coreBeams_.clear();
    for (auto* warn : areaWarnings_) {
        if (warn) {
            warn->SetScale({ 0.0f, 0.0f, 0.0f });
            warn->SetCollisionAttribute(0);
            warn->SetParent(nullptr); // 追加
            // warn->isDead = true;
        }
    }
    areaWarnings_.clear();
    for (auto* meteor : meteors_) {
        if (meteor) {
            meteor->SetScale({ 0.0f, 0.0f, 0.0f });
            meteor->SetCollisionAttribute(0);
            meteor->SetParent(nullptr); // 追加
            // meteor->isDead = true;
        }
    }
    meteors_.clear();
}