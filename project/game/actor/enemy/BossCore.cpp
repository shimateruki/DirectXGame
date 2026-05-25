#include "BossCore.h"
#include "InputManager.h"
#include "imgui.h"
#include "easing.h"
#include "DebugConsole.h"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <numbers>
#include <ctime>
#include <cstdlib>
#include "GPUParticleManager.h"
#include <algorithm>
#undef max

#include "CameraManager.h"
#include "CameraEditor.h"
#include "PostEffect.h"
#include "GhostRecorder.h"

// ==========================================
// 攻撃クラスを読み込む
// ==========================================
#include "BossAttack/BossAttack1_Rush.h"
#include "BossAttack/BossAttack2_Shoot.h"
#include "BossAttack/BossAttack3_Hammer.h"
#include "BossAttack/BossAttack4_Wall.h"
#include "BossAttack/BossAttack5_Humanoid.h"
#include "BossAttack/BossAttack6_Laser.h"
#include "BossAttack/BossAttack7_Absorb.h"
#include "BossAttack/BossAttack8_Final.h"
#include "BossAttack/BossAttack9_Funnels.h"
#include "BossAttack/BossAttack9_Spawn.h"
#include "MeshEffectManager.h"
#include "game/system/BulletManager.h"
#include "Player.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "MapBlock.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"


// =================================================================
// ★ 待機アニメーション用のタイマーと軌道計算関数
// =================================================================
namespace {
    float s_globalIdleTimer = 0.0f;
    int s_debugForceAttack = 0;

    Object3d* FindWeaponRecursive(Object3d* node) {
        if (!node) return nullptr;
        if (node->GetCollisionAttribute() & kPlayerAttack) {
            return node;
        }
        for (Object3d* child : node->GetChildren()) {
            Object3d* result = FindWeaponRecursive(child);
            if (result) return result;
        }
        return nullptr;
    }

    Object3d* FindObjectByNameRecursive(Object3d* node, const std::string& name) {
        if (!node) return nullptr;
        if (node->GetName() == name) return node;
        for (Object3d* child : node->GetChildren()) {
            if (Object3d* found = FindObjectByNameRecursive(child, name)) return found;
        }
        return nullptr;
    }
}

BossCore::OrbitData BossCore::GetIdleOrbit(size_t index) {
    BossCore::OrbitData data;

    static std::vector<Vector3> randomScales;
    if (randomScales.empty()) {
        for (int i = 0; i < 10; ++i) {
            float randomVal = 0.5f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f;
            randomScales.push_back({ randomVal, randomVal, randomVal });
        }
    }

    Vector3 basePos;
    switch (index % 10) {
    case 0: basePos = { 2.0f,  2.0f,  0.5f }; break;
    case 1: basePos = { -1.5f, -1.5f,  1.8f }; break;
    case 2: basePos = { 0.6f,  2.5f, -1.8f }; break;
    case 3: basePos = { -2.0f,  0.5f, -1.2f }; break;
    case 4: basePos = { 1.5f, -2.0f,  1.2f }; break;
    case 5: basePos = { -0.8f, -0.8f, -2.0f }; break;
    case 6: basePos = { 2.5f, -0.5f, -1.5f }; break;
    case 7: basePos = { -2.5f,  1.0f,  1.5f }; break;
    case 8: basePos = { 0.0f,  3.0f,  1.5f }; break;
    case 9: basePos = { 0.0f, -3.0f,  -1.5f }; break;
    }

    float angle = s_globalIdleTimer * 0.8f;
    float cosY = std::cos(angle);
    float sinY = std::sin(angle);

    Vector3 rotatedPos = {
        basePos.x * cosY - basePos.z * sinY,
        basePos.y,
        basePos.x * sinY + basePos.z * cosY
    };

    float hover = std::sin(s_globalIdleTimer * 1.5f) * 0.3f;
    rotatedPos.y += hover;

    data.pos = rotatedPos;

    float rotY = std::atan2(-data.pos.x, -data.pos.z);
    float xzLen = std::sqrt(data.pos.x * data.pos.x + data.pos.z * data.pos.z);
    float rotX = std::atan2(data.pos.y, xzLen);

    data.rot = { rotX, rotY, 0.0f };
    data.scale = randomScales[index % 10];

    return data;
}

// ==========================================
// ★ マップブロックを自分の装甲として取り込む（同化する）
// ==========================================
bool BossCore::AssimilateBlock(Object3d* newBlock) {
    for (Object3d* block : armorBlocks_) {
        if (block == newBlock) return true; // 既に同化済み
    }

    // ==========================================
    // ★ 修正：吸収した瞬間にファンネル仕様へアップグレード
    // ==========================================
    UpgradeToFunnel(newBlock);

    newBlock->SetParent(this);
    newBlock->GetTransform()->isQuaternionMaster = false;
    newBlock->SetIsVisible(false); // 内部パーツが表示されるため親は隠す

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (blockBroken_[i]) {
            armorBlocks_[i] = newBlock;
            blockHps_[i] = attackParams_.maxArmorBlockHp;
            blockBroken_[i] = false;

            newBlock->SetCollisionAttribute(kEnemyAttack);
            newBlock->SetCollisionMask(kPlayer);
            newBlock->SetEnemyType("BossArmor");
            return true;
        }
    }

    if (armorBlocks_.size() < 10) {
        AddArmorBlock(newBlock);
        return true;
    }

    // ==========================================
    // ★ 修正：10個満タンの時は吸収しないので、親子関係を解除して元に戻す！
    // ==========================================
    newBlock->SetParent(nullptr);
    return false;
}

// ==========================================
// 装甲が10個満タン（壊れてもいない）かどうかを判定！
// ==========================================
bool BossCore::IsArmorFull() const {
    if (armorBlocks_.size() < 10) return false; // 10個未満ならまだ吸える

    for (bool broken : blockBroken_) {
        if (broken) return false; // 壊れている箇所があればまだ吸える
    }

    return true; // 10個あって、1つも壊れていないなら完全体！
}

// ==========================================
// ★ 10個満タンになるまで、あと何個ブロックが必要か計算する
// ==========================================
int BossCore::GetNeededBlockCount() const {
    int validCount = 0;
    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (!blockBroken_[i]) {
            validCount++; // 壊れていない装甲の数を数える
        }
    }
    int needed = 10 - validCount;
    return (needed > 0) ? needed : 0; // 必要な数を返す（満タンなら0）
}

// =================================================================
// 初期化・更新
// =================================================================

void BossCore::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetClassName("BossCore");

    LoadAttackParams();
    SetAttackDamage(attackParams_.damageRush); // 突進攻撃力を標準接触ダメージとしてセット

    barrierHp_ = attackParams_.maxBarrierHp;
    maxBarrierHp_ = attackParams_.maxBarrierHp;

    // 現在登録されているブロックのHPを読み込んだパラメータに初期化！
    for (size_t i = 0; i < blockHps_.size(); ++i) {
        blockHps_[i] = attackParams_.maxArmorBlockHp;
    }

    // パラメータが未初期化ならデフォルト値で初期化（アクセス違反防止）
    if (!param_.has_value()) {
        param_ = EntityParameter();
        param_->hp = 1000.0f; // デフォルトHP
        param_->maxHp = 1000.0f;
    }

    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    director_ = std::make_unique<GhostDirector>();
    if (sceneManager_) {
        director_->Initialize(sceneManager_);
    }
    // 登場演出用のアニメーション(JSON)を読み込む
    if (director_) {
        director_->LoadScenario("EntranceAnimation");
    }

    // 色の初期設定 (緑色で統一)
    originalColor_ = { 0.2f, 0.8f, 1.0f, 1.0f };
    SetColor(greenColor_);
    defaultColor_ = greenColor_;

    // --- 1. 紫 ---
    auto BossParticle1 = std::make_unique<GPUParticleEmitter>();
    BossParticle1->Initialize("Boss1", this);
    BossParticle1->Play();
    //particleEmitters_.push_back(std::move(BossParticle1)); // 配列に追加

    // --- 2. 黄 ---
    auto BossParticle2 = std::make_unique<GPUParticleEmitter>();
    BossParticle2->Initialize("Boss2", this);
    BossParticle2->Play();
    particleEmitters_.push_back(std::move(BossParticle2)); // 配列に追加

    // --- 3. 水色 ---
    auto BossParticle3 = std::make_unique<GPUParticleEmitter>();
    BossParticle3->Initialize("Boss3", this);
    BossParticle3->Play();
    particleEmitters_.push_back(std::move(BossParticle3)); // 配列に追加

    // --- 4. 赤 ---
    auto BossParticle4 = std::make_unique<GPUParticleEmitter>();
    BossParticle4->Initialize("Boss4", this);
    BossParticle4->Play();
    //particleEmitters_.push_back(std::move(BossParticle4)); // 配列に追加

    // --- 5. 黄色 ---
    auto BossParticle5 = std::make_unique<GPUParticleEmitter>();
    BossParticle5->Initialize("Boss5", this);
    BossParticle5->Play();
    //particleEmitters_.push_back(std::move(BossParticle5)); // 配列に追加

    // --- 6. 緑色 ---
    auto BossParticle6 = std::make_unique<GPUParticleEmitter>();
    BossParticle6->Initialize("Boss6", this);
    BossParticle6->Play();
    //particleEmitters_.push_back(std::move(BossParticle6)); // 配列に追加

    isFinalPhase_ = false;
    isWaitingForDeath_ = false;
    isWaitingForFinisher_ = false;
    isFinisherFalling_ = false;
    finisherFallVelocity_ = 0.0f;
    finisherBounceCount_ = 0;
    deathPhase_ = 0;
    isCompletelyDead_ = false;
    isShardSpawnRequested_ = false;

    isHpHalfTriggered_ = false;
    isHpHalfEventActive_ = false;
    hpHalfPhase_ = HpHalfEventPhase::None;
    hpHalfEffectTimer_ = 0.0f;
}

void BossCore::Update(float deltaTime) {
    // 💥 JSON/ImGuiで動的に設定した最大バリアHPを同期
    maxBarrierHp_ = attackParams_.maxBarrierHp;

    // アクション速度（移動や回転など）用の補正DeltaTime
    float actionDelta = deltaTime * kBaseSpeedMultiplier;

    InputManager* input = InputManager::GetInstance();

#ifdef USE_IMGUI
    if (SceneManager::GetInstance()->IsPlaying()) {
        // 0キーで時間停止
        if (input->IsKeyTriggered(DIK_0)) {
            s_isTimeStopped_ = !s_isTimeStopped_;
            if (s_isTimeStopped_) {
                DebugConsole::GetInstance()->AddLog("[TIME STOP] ボスの時間が止まった…！");
            }
            else {
                DebugConsole::GetInstance()->AddLog("[TIME RESUME] 時が動き出す！");
            }
        }

        int triggerAttack = 0;
        if (input->IsKeyTriggered(DIK_1)) triggerAttack = 1;
        if (input->IsKeyTriggered(DIK_2)) triggerAttack = 2;
        if (input->IsKeyTriggered(DIK_3)) triggerAttack = 3;
        if (input->IsKeyTriggered(DIK_4)) triggerAttack = 4;
        if (input->IsKeyTriggered(DIK_5)) triggerAttack = 5;
        if (input->IsKeyTriggered(DIK_6)) triggerAttack = 6;
        if (input->IsKeyTriggered(DIK_7)) triggerAttack = 7;
        if (input->IsKeyTriggered(DIK_8)) {
            if (param_.has_value()) {
                param_->hp = 1.0f; // 8キーを押したらHPを1にする
            }
        }
        if (input->IsKeyTriggered(DIK_Y)) triggerAttack = 9; // Yキーでファンネル攻撃！

        // 9キーで即座にボスを爆散させるデバッグ機能！
        if (input->IsKeyTriggered(DIK_9)) {
            if (!isCoreBroken_) {
                DebugConsole::GetInstance()->AddLog("【DEBUG】 9キー入力：ボスを強制爆散させます！！！💥");
                param_->hp = 0.0f;
                StartDeathSequence();
            }
        }

        // HキーでHP半分時の演出を強制発動させるデバッグ機能！
        if (input->IsKeyTriggered(DIK_H)) {
            DebugConsole::GetInstance()->AddLog("【DEBUG】 Hキー入力：HP半減演出を強制発動します！");
            
            // 強制的にフラグをリセットしてダメージを与えることで正規のルートで演出を開始する
            isHpHalfTriggered_ = false;
            isHpHalfEventActive_ = false;
            hpHalfPhase_ = HpHalfEventPhase::None;
            
            if (param_.has_value()) {
                param_->hp = param_->maxHp; // 一旦満タンにして確実に発動条件を満たす
                TakeBodyDamage(param_->maxHp * 0.5f); 
            }
        }

        if (triggerAttack != 0) {
            DebugConsole::GetInstance()->AddLog("【DEBUG】 攻撃 " + std::to_string(triggerAttack) + " を予約！待機に戻ります！");
            s_debugForceAttack = triggerAttack;

            if (currentAttack_) {
                currentAttack_->Finalize();
                currentAttack_.reset();
            }
            ChangeState(State::Idle);
            animTimer_ = 0.0f;

            SetTranslate({ 0.0f, 4.0f, 0.0f });
            SetRotation({ 0.0f, 0.0f, 0.0f });
            SetColor(originalColor_);

            flyingBlocks_.clear();
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (armorBlocks_[i]) {
                    armorBlocks_[i]->SetParent(this);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }

            if (warningArea_) {
                warningArea_->SetScale({ 0.0f, 0.0f, 0.0f });
                warningArea_->SetCollisionAttribute(0);
                if (warningArea_->GetParent() == nullptr) {
                    warningArea_->SetParent(this);
                }
            }
        }
    }
#endif

    if (s_isTimeStopped_) {
        deltaTime = 0.0f;
        actionDelta = 0.0f;
    }

    float preTimer = colorResetTimer_;

    // ベースクラスの更新
    BaseEnemy::Update(actionDelta);

    // ==========================================
    // ★ HPが1以下の時の最終奥義発動チェック
    // ==========================================
    if (param_.has_value() && param_->hp <= 1.0f) {
        if (!isFinalPhase_) {
            param_->hp = 1.0f;
            isFinalPhase_ = true;
            DebugConsole::GetInstance()->AddLog("[LAST STAND] ボスが最後の大技を準備している…！！");

            if (currentAttack_) {
                currentAttack_->Finalize();
                currentAttack_.reset();
            }
            ChangeState(State::Idle);   // 一度Idle状態にしてパラメータ等をリセット
            ChangeState(State::Attack); // 自動で最終奥義(ID: 8)が選ばれる
        }
        else if (!isWaitingForFinisher_ && deathPhase_ == 0) {
            // 大技の最中などは絶対に死なない！HP1を維持！
            param_->hp = 1.0f;
        }
    }

    // ==========================================
    // ★ HP半分時の演出更新 (崩壊・復帰・強化シークエンス)
    // ==========================================
    if (isHpHalfEventActive_) {
        hpHalfEffectTimer_ += deltaTime; // 演出タイマーは実時間

        // ★ 追加：カメラ演出の終了を監視し、終わった瞬間に向きをボスに合わせる
        if (!isPlayerRotated_) {
            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                // カメラの再生が終わり、補間も含めてプレイヤーに完全に位置が戻った瞬間 (Weightが0)
                // ※戻り中に SetRotation しても Camera::Update 内で逆算上書きされるため、0になるのを待つ
                if (!camera->IsOverridden() && camera->GetOverrideWeight() <= 0.001f) {
                    if (target_) {
                        Vector3 playerPos = target_->GetWorldPosition();
                        Vector3 bossPos = this->GetWorldPosition();
                        Vector3 toBoss = bossPos - playerPos;
                        float distXZ = std::sqrt(toBoss.x * toBoss.x + toBoss.z * toBoss.z);

                        // 角度の計算
                        float angleY = std::atan2(toBoss.x, toBoss.z);
                        float angleX = std::atan2(-toBoss.y, distXZ);

                        // プレイヤーとカメラの向きを強制同期
                        target_->SetRotation({ 0.0f, angleY, 0.0f });
                        camera->SetRotation({ angleX, angleY, 0.0f });
                        
                        isPlayerRotated_ = true; // 一度だけ実行
                        DebugConsole::GetInstance()->AddLog("【EVENT】 カメラの復帰を確認。視点をボスに固定しました。");
                    }
                }
            }
        }

        // ブロックの落下・散乱物理（Falling〜Pulsingの間、常に更新し続ける）
        if (hpHalfPhase_ >= HpHalfEventPhase::Falling && hpHalfPhase_ < HpHalfEventPhase::Reassembling) {
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (armorBlocks_[i] && i < fallingBlockVelocities_.size()) {
                    Vector3 bPos = armorBlocks_[i]->GetTranslate();

                    // ★ 追加：Pulsing フェーズ（ボスの鼓動）中は、地面にいたブロックを浮かび上がらせる
                    if (hpHalfPhase_ == HpHalfEventPhase::Pulsing) {
                        float riseSpeed = 3.0f; // 上昇速度
                        bPos.y += riseSpeed * actionDelta;
                        
                        // 浮かび上がりながらゆっくり回転させる
                        Vector3 rot = armorBlocks_[i]->GetRotation();
                        rot.x += 2.0f * actionDelta;
                        rot.y += 1.5f * actionDelta;
                        armorBlocks_[i]->SetRotation(rot);

                        armorBlocks_[i]->SetTranslate(bPos);
                    }
                    // 落下中、または空中にいる場合の物理
                    else if (fallingBlockVelocities_[i].x != 0.0f || fallingBlockVelocities_[i].y != 0.0f || fallingBlockVelocities_[i].z != 0.0f || bPos.y > 0.5f) {
                        fallingBlockVelocities_[i].y -= 25.0f * actionDelta; // 重力
                        bPos += fallingBlockVelocities_[i] * actionDelta;
                        if (bPos.y <= 0.5f) {
                            bPos.y = 0.5f;
                            fallingBlockVelocities_[i] = { 0,0,0 }; // 地面についたら停止
                        }
                        armorBlocks_[i]->SetTranslate(bPos);

                        // 空中にいる間は回転させる
                        if (bPos.y > 0.5f) {
                            Vector3 rot = armorBlocks_[i]->GetRotation();
                            rot.x += 5.0f * actionDelta;
                            rot.y += 3.0f * actionDelta;
                            armorBlocks_[i]->SetRotation(rot);
                        }
                    }
                }
            }
        }

        switch (hpHalfPhase_) {
        case HpHalfEventPhase::WaitIdle:
            // ★ 修正：いきなり落ちるのではなく、1.0秒間空中で「おや？」と思わせる溜めを作る
            if (hpHalfEffectTimer_ >= 1.0f) {
                originalCoreRotation_ = GetRotation();
                originalCorePosition_ = GetTranslate();
                this->UpdateWorldMatrix();

                fallingBlockVelocities_.clear();
                for (Object3d* block : armorBlocks_) {
                    if (!block) {
                        fallingBlockVelocities_.push_back({ 0,0,0 });
                        continue;
                    }

                    block->UpdateWorldMatrix();
                    Vector3 worldPos = block->GetWorldPosition();
                    block->SetParent(nullptr);
                    block->SetTranslate(worldPos);

                    // ボス中心から外側に向かって弾け飛ぶ速度を計算
                    Vector3 dir = { worldPos.x - originalCorePosition_.x, 0.0f, worldPos.z - originalCorePosition_.z };
                    float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                    if (len > 0.001f) {
                        dir.x /= len;
                        dir.z /= len;
                    } else {
                        float angle = (static_cast<float>(rand()) / RAND_MAX) * 3.14159f * 2.0f;
                        dir.x = std::cos(angle);
                        dir.z = std::sin(angle);
                    }

                    float horizontalSpeed = 7.0f + (rand() % 30) * 0.1f; // 15〜20 から半分程度に減少
                    float verticalSpeed = 7.0f + (rand() % 30) * 0.1f;   // 15〜20 から半分程度に減少

                    fallingBlockVelocities_.push_back({
                        dir.x * horizontalSpeed,
                        verticalSpeed,
                        dir.z * horizontalSpeed
                    });
                }

                basePostEffectParams_ = *PostEffect::GetInstance()->GetParams();
                hpHalfPhase_ = HpHalfEventPhase::Falling;
                hpHalfEffectTimer_ = 0.0f;
            }
            break;

        case HpHalfEventPhase::Falling:
        {
            // ★ 修正：急落するのではなく、1.5秒かけてゆっくり（かつ加速しながら）地面へ
            float duration = 1.5f;
            float t = std::min(hpHalfEffectTimer_ / duration, 1.0f);
            float easeT = t * t; // 加速して落ちる感じ

            Vector3 pos = GetTranslate();
            pos.y = Math::Lerp(4.0f, 0.8f, easeT);
            SetTranslate(pos);

            if (t >= 1.0f) {
                hpHalfPhase_ = HpHalfEventPhase::Lying;
                hpHalfEffectTimer_ = 0.0f;
            }

            Vector3 rot = GetRotation();
            rot.x = Math::Lerp(rot.x, 1.4f, t); // 落下時間に合わせて倒れ込む
            SetRotation(rot);
        }
        break;

        case HpHalfEventPhase::Lying:
            if (hpHalfEffectTimer_ >= 1.0f) {
                hpHalfPhase_ = HpHalfEventPhase::Recovery;
                hpHalfEffectTimer_ = 0.0f;
            }
            break;

        case HpHalfEventPhase::Recovery:
        {
            Vector3 rot = GetRotation();
            rot.x = Math::Lerp(rot.x, originalCoreRotation_.x, 2.0f * actionDelta);
            rot.y = originalCoreRotation_.y + std::sin(hpHalfEffectTimer_ * 15.0f) * 0.4f;
            SetRotation(rot);

            Vector3 pos = GetTranslate();
            pos.y = Math::Lerp(pos.y, originalCorePosition_.y, 2.0f * actionDelta);
            SetTranslate(pos);

            if (hpHalfEffectTimer_ >= 1.5f) {
                hpHalfPhase_ = HpHalfEventPhase::Pulsing;
                hpHalfEffectTimer_ = 0.0f;
            }
        }
        break;

        case HpHalfEventPhase::Pulsing:
        {
            float targetScale = 1.0f;
            Vector3 rot = GetRotation();

            if (hpHalfEffectTimer_ < 0.5f) {
                // 最初の0.5秒：小さくなってピタッと止まる演出（溜め）
                // 0.3秒で 0.6 倍まで縮み、残り0.2秒は完全に静止する
                float shrinkT = std::min(hpHalfEffectTimer_ / 0.3f, 1.0f);
                targetScale = Math::Lerp(1.0f, 0.6f, shrinkT);
                
                // この間は震えず、ただ斜めに傾くのみ
                float targetRotX = originalCoreRotation_.x + 0.6f;
                rot.x = Math::Lerp(rot.x, targetRotX, 2.5f * actionDelta);
            } else {
                // 0.5秒以降：縮んだ状態をベースにパルスし、震え始める
                float pulseTime = hpHalfEffectTimer_ - 0.5f;
                targetScale = 0.6f + std::sin(pulseTime * 40.0f) * 0.1f;

                float targetRotX = originalCoreRotation_.x + 0.6f;
                rot.x = Math::Lerp(rot.x, targetRotX, 2.5f * actionDelta);
                // 小刻みな震え
                rot.y = originalCoreRotation_.y + std::sin(pulseTime * 50.0f) * 0.05f;
            }

            SetScale({ targetScale, targetScale, targetScale });
            SetColor({ 1.0f, 0.1f, 0.1f, 1.0f });
            rot.z = originalCoreRotation_.z;
            SetRotation(rot);

            // 溜め時間を0.5秒使ったため、全体の演出時間を 1.5 -> 2.0秒 に延長
            if (hpHalfEffectTimer_ >= 2.0f) {
                hpHalfPhase_ = HpHalfEventPhase::Reassembling;
                hpHalfEffectTimer_ = 0.0f;
            }
        }
        break;

        case HpHalfEventPhase::Reassembling:
        {
            // ★ アニメーションが終わって元に戻る時も、線形補間で滑らかに戻す
            Vector3 coreRot = GetRotation();
            coreRot.x = Math::Lerp(coreRot.x, originalCoreRotation_.x, 4.0f * actionDelta);
            coreRot.y = Math::Lerp(coreRot.y, originalCoreRotation_.y, 4.0f * actionDelta);
            coreRot.z = Math::Lerp(coreRot.z, originalCoreRotation_.z, 4.0f * actionDelta);
            SetRotation(coreRot);

            Vector3 coreScale = GetScale();
            coreScale.x = Math::Lerp(coreScale.x, 1.0f, 4.0f * actionDelta);
            coreScale.y = Math::Lerp(coreScale.y, 1.0f, 4.0f * actionDelta);
            coreScale.z = Math::Lerp(coreScale.z, 1.0f, 4.0f * actionDelta);
            SetScale(coreScale);

            bool allDone = true;
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (armorBlocks_[i]) {
                    // ブロックを親子関係に戻しつつ、元の軌道位置へLerp
                    if (armorBlocks_[i]->GetParent() == nullptr) {
                        armorBlocks_[i]->SetParent(this);
                    }

                    OrbitData orbit = GetIdleOrbit(i);
                    Vector3 currentPos = armorBlocks_[i]->GetTranslate();
                    Vector3 targetPos = orbit.pos;
                    Vector3 nextPos = Math::Lerp(currentPos, targetPos, 4.0f * actionDelta);
                    armorBlocks_[i]->SetTranslate(nextPos);

                    if (Math::Length(nextPos - targetPos) > 0.1f) allDone = false;
                }
            }

            // すべての復帰が終わった、またはタイムアウト
            if (allDone || hpHalfEffectTimer_ >= 3.0f) {
                // ★ 追加：カメラの演出（GhostRecorder等）が完全に終わるまで待機
                if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                    // オーバーライドが終了し、かつ補間（戻り）も完全に終わっているかチェック
                    if (!camera->IsOverridden() && camera->GetOverrideWeight() <= 0.01f) {
                        hpHalfPhase_ = HpHalfEventPhase::Finishing;
                        hpHalfEffectTimer_ = 0.0f;
                    }
                } else {
                    hpHalfPhase_ = HpHalfEventPhase::Finishing;
                    hpHalfEffectTimer_ = 0.0f;
                }
            }
        }
        break;

        case HpHalfEventPhase::Finishing:
            isHpHalfEventActive_ = false;
            hpHalfPhase_ = HpHalfEventPhase::None;
            isPlayerRotated_ = false; // 次回のためにリセット

            if (target_) {
                if (auto player = dynamic_cast<Player*>(target_)) {
                    player->SetIsControlActive(true);
                }
            }
            SetColor(greenColor_);
            defaultColor_ = greenColor_;
            SetScale({ 1,1,1 });
            SetRotation(originalCoreRotation_);
            *PostEffect::GetInstance()->GetParams() = basePostEffectParams_;

            DebugConsole::GetInstance()->AddLog("【EVENT】 フェーズ移行完了！ボスの猛攻に備えろ！");
            break;
            break;
        }

        // ポストエフェクト演出：各フェーズに合わせてエフェクトを動的に変化させる
        auto params = PostEffect::GetInstance()->GetParams();

        if (hpHalfPhase_ == HpHalfEventPhase::Falling) {
            // --- 落下フェーズ：画面が一瞬光る（ブルーム強化） ---
            float glowT = std::sin(std::min(hpHalfEffectTimer_ * 3.1415f, 3.1415f));
            params->threshold = Math::Lerp(basePostEffectParams_.threshold, 0.0f, glowT);
            params->bloomIntensity = Math::Lerp(basePostEffectParams_.bloomIntensity, 0.7f, glowT);
            params->spread = Math::Lerp(basePostEffectParams_.spread, 1.2f, glowT);
            params->enableToneMapping = (glowT > 0.1f) ? 1 : basePostEffectParams_.enableToneMapping;
        }
        else if (hpHalfPhase_ == HpHalfEventPhase::Recovery) {
            // --- 起き上がりフェーズ：徐々に不穏な雰囲気を出す ---
            float recoveryT = std::min(hpHalfEffectTimer_ / 1.5f, 1.0f); // 0→1 で徐々に
            params->chromaticAberration = Math::Lerp(basePostEffectParams_.chromaticAberration, 0.00f, recoveryT);
            params->vignetteIntensity = Math::Lerp(basePostEffectParams_.vignetteIntensity, 0.0f, recoveryT);
            params->filmGrainIntensity = Math::Lerp(basePostEffectParams_.filmGrainIntensity, 0.08f, recoveryT);
        }
        else if (hpHalfPhase_ == HpHalfEventPhase::Pulsing) {
            // --- パルスフェーズ：ボスの大小アニメーションに同期したポストエフェクト ---
            if (hpHalfEffectTimer_ < 0.5f) {
                // ★ 溜め段階（0〜0.5秒）：じわじわとエフェクトを強くする
                float chargeT = std::min(hpHalfEffectTimer_ / 0.5f, 1.0f);
                float easeCharge = chargeT * chargeT; // EaseIn で加速感

                params->vignetteIntensity = Math::Lerp(basePostEffectParams_.vignetteIntensity, 0.0f, easeCharge);
                params->radialIntensity = Math::Lerp(basePostEffectParams_.radialIntensity, 0.005f, easeCharge);
                params->filmGrainIntensity = Math::Lerp(basePostEffectParams_.filmGrainIntensity, 0.04f, easeCharge);
                params->threshold = Math::Lerp(basePostEffectParams_.threshold, 0.8f, easeCharge);
                params->bloomIntensity = Math::Lerp(basePostEffectParams_.bloomIntensity, 1.2f, easeCharge);
            } else {
                // ★ パルス段階（0.5秒〜）：スケールの脈動に連動してエフェクトが波打つ
                float pulseTime = hpHalfEffectTimer_ - 0.5f;
                float pulseWave = std::sin(pulseTime * 40.0f); // スケールと同じ周波数
                float pulseNorm = (pulseWave + 1.0f) * 0.5f;   // 0〜1 に正規化

                // 時間経過で全体の強度を徐々に上げる（クライマックス感）
                float progressT = std::min(pulseTime / 1.5f, 1.0f);

                // ビネット：控えめな暗がり
                params->vignetteIntensity = Math::Lerp(0.5f, 1.2f, pulseNorm * progressT);

                // 放射ブラー：中心に軽く力が集まる程度の弱いボケ
                params->radialCenterX = 0.5f;
                params->radialCenterY = 0.5f;
                params->radialIntensity = Math::Lerp(0.005f, 0.015f, pulseNorm * progressT);

                // フィルムグレイン：ごく僅かなノイズ
                params->filmGrainIntensity = Math::Lerp(0.04f, 0.06f, progressT);

                // 画面揺れ：ごく僅かな震え
                params->wobbleIntensity = Math::Lerp(0.0f, 0.007f, progressT * progressT);
            }
            params->enableToneMapping = 1;
        }
        else if (hpHalfPhase_ == HpHalfEventPhase::Reassembling) {
            // --- 再集結フェーズ：エフェクトをスムーズに元に戻す ---
            float fadeT = std::min(hpHalfEffectTimer_ / 1.5f, 1.0f); // 1.5秒かけて戻す
            float easeFade = 1.0f - std::pow(1.0f - fadeT, 2.0f); // EaseOut

            params->chromaticAberration = Math::Lerp(params->chromaticAberration, basePostEffectParams_.chromaticAberration, easeFade);
            params->vignetteIntensity = Math::Lerp(params->vignetteIntensity, basePostEffectParams_.vignetteIntensity, easeFade);
            params->radialIntensity = Math::Lerp(params->radialIntensity, basePostEffectParams_.radialIntensity, easeFade);
            params->filmGrainIntensity = Math::Lerp(params->filmGrainIntensity, basePostEffectParams_.filmGrainIntensity, easeFade);
            params->threshold = Math::Lerp(params->threshold, basePostEffectParams_.threshold, easeFade);
            params->bloomIntensity = Math::Lerp(params->bloomIntensity, basePostEffectParams_.bloomIntensity, easeFade);
            params->spread = Math::Lerp(params->spread, basePostEffectParams_.spread, easeFade);
            params->wobbleIntensity = Math::Lerp(params->wobbleIntensity, basePostEffectParams_.wobbleIntensity, easeFade);
            params->enableToneMapping = basePostEffectParams_.enableToneMapping;
        }



        // ★ 演出中はここで return してしまうため、カメラアニメーション(GhostDirector)の更新もここで行う
        if (director_) {
            director_->Update(actionDelta);
        }

        return; // 演出中は以降の通常更新をスキップ
    }

    // ==========================================
    // 死亡演出の進行ロジック
    // ==========================================
    if (deathPhase_ == 1 || deathPhase_ == 2) {
        sequenceTimer_ -= deltaTime; // 死亡演出のカウントダウンは実時間

        // 1秒経つごとに次のフェーズへ進む
        if (sequenceTimer_ <= 0.0f) {

            if (deathPhase_ == 1) {
                // 1秒経過 -> 「亀裂フェーズ」へ移行し、さらに1秒待つ
                deathPhase_ = 2;
                sequenceTimer_ = 1.0f;
                ShowCrackedCore();
            }
            else if (deathPhase_ == 2) {
                // さらに1秒経過 -> ついに「粉砕フェーズ」へ
                deathPhase_ = 3;
                BreakCore();
            }
        }
    }

    if (director_) {
        director_->Update(actionDelta);

        // ゴーストディレクターのアニメーション終了を待っている場合
        if (isWaitingForDirector_ && director_->IsFinished()) {
            isWaitingForDirector_ = false;

            // アニメーションが終わったら、"Center_Collision_Box" の当たり判定を完全に消す
            if (sceneManager_ && sceneManager_->GetCurrentScene()) {
                for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
                    if (Object3d* found = FindObjectByNameRecursive(obj.get(), "Center_Collision_Box")) {
                        found->SetCollisionAttribute(0);
                        found->SetCollisionMask(0); // マスクも念のため消しておく
                        found->SetScale({ 0.0f, 0.0f, 0.0f }); // 見た目も消す
                        break;
                    }
                }
            }
        }
    }

    // ==========================================
    // ★ 登場演出中なら、それを更新する
    // ==========================================
    if (isAppearing_) {
        UpdateAppearance(deltaTime); // 内部で使い分け
    }

    // ==========================================
    // ★ パーティクルの自動追従の更新
    // ==========================================
    for (auto& emitter : particleEmitters_) {
        if (emitter) {
            emitter->Update(actionDelta);
        }
    }

    // バリアへのダメージ処理
    if (IsTargetValid() && damageCooldownTimer_ <= 0.0f && state_ != State::Weak &&
        !isFinalPhase_ && !isWaitingForFinisher_ && deathPhase_ == 0) {
        Object3d* weapon = FindWeaponRecursive(target_);
        bool hitFound = false;

        if (weapon && weapon->GetCollisionMask() != 0) {
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                Object3d* block = armorBlocks_[i];
                if (!block || blockBroken_[i]) continue;

                if (block->CheckCollision(weapon).isColliding) {
                    float dmg = weapon->GetAttackDamage();
                    TakeBarrierDamage(dmg, block);
                    GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());

                    blockHps_[i] -= dmg;
                    if (blockHps_[i] <= 0.0f) {
                        blockBroken_[i] = true;
                        GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());
                        DebugConsole::GetInstance()->AddLog("[BREAK] ブロック " + std::to_string(i) + " が破壊された！！！");
                    }
                    hitFound = true;
                    break;
                }
            }
        }

        // 2. その他のプレイヤー攻撃（エフェクトや弾丸など）をマネージャ経由でチェック
        if (!hitFound) {
            // エフェクトのチェック
            for (const auto& effect : MeshEffectManager::GetInstance()->GetActiveEffects()) {
                if (!effect || effect->isDead) continue;
                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    Object3d* block = armorBlocks_[i];
                    if (!block || blockBroken_[i]) continue;

                    if (effect->CanHit(block) && block->CheckCollision(effect.get()).isColliding) {
                        float dmg = effect->GetAttackDamage();
                        TakeBarrierDamage(dmg, block);
                        effect->AddHitObject(block); // 💥 ヒットリストに追加
                        GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());

                        blockHps_[i] -= dmg;
                        if (blockHps_[i] <= 0.0f) {
                            blockBroken_[i] = true;
                            GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());
                            DebugConsole::GetInstance()->AddLog("[BREAK] ブロック " + std::to_string(i) + " が破壊された！！！");
                        }
                        hitFound = true;
                        break;
                    }
                }
                if (hitFound) break;
            }
        }

        if (!hitFound) {
            // 弾丸のチェック
            for (const auto& bullet : BulletManager::GetInstance()->GetBullets()) {
                if (!bullet || bullet->IsDead()) continue;
                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    Object3d* block = armorBlocks_[i];
                    if (!block || blockBroken_[i]) continue;

                    if (block->CheckCollision(bullet.get()).isColliding) {
                        float dmg = bullet->GetAttackDamage();
                        TakeBarrierDamage(dmg, block);
                        GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());

                        blockHps_[i] -= dmg;
                        if (blockHps_[i] <= 0.0f) {
                            blockBroken_[i] = true;
                            GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());
                            DebugConsole::GetInstance()->AddLog("[BREAK] ブロック " + std::to_string(i) + " が破壊された！！！");
                        }
                        hitFound = true;
                        break;
                    }
                }
                if (hitFound) break;
            }
        }
    }

    if (preTimer > 0.0f && colorResetTimer_ <= 0.0f) {
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (armorBlocks_[i] && i < savedBlockColors_.size()) {
                SetBlockColor(armorBlocks_[i], savedBlockColors_[i]);
            }
        }
    }

    // ====================================================
     // 登場演出中か、戦闘開始後のみアニメーションタイマーを進める！
     // ====================================================
    if (SceneManager::GetInstance()->IsPlaying()) {
        if (isAppearing_ || isBattleStarted_) {
            s_globalIdleTimer += actionDelta; // アイドルアニメは倍速
        }
    }

    UpdateFlyingBlocks(actionDelta);

    // ==========================================
    // 4. 通常のステート更新
    // ==========================================
    if (SceneManager::GetInstance()->IsPlaying()) {
        if (isFirstFrame_) {
            originalColor_ = GetColor();
            for (Object3d* child : GetChildren()) {
                if (child->GetName() == "WarningArea") {
                    warningArea_ = child;
                    warningArea_->SetParent(nullptr);
                    warningArea_->SetScale({ 0.0f, 0.0f, 0.0f });
                    warningArea_->SetCollisionAttribute(0);
                    warningArea_->SetCollisionMask(0);
                    warningArea_->SetRotation({ 0.0f, 0.0f, 0.0f });
                    warningArea_->GetTransform()->isQuaternionMaster = false;

                    // ==========================================
                    // ★ 修正：ArmorBlocks_ だけでなく、HPやフラグのリストからも確実に消す！
                    // ==========================================
                    for (size_t i = 0; i < armorBlocks_.size(); ) {
                        if (armorBlocks_[i] == warningArea_) {
                            armorBlocks_.erase(armorBlocks_.begin() + i);
                            if (i < blockHps_.size()) blockHps_.erase(blockHps_.begin() + i);
                            if (i < blockBroken_.size()) blockBroken_.erase(blockBroken_.begin() + i);
                        }
                        else {
                            ++i;
                        }
                    }
                    break;
                }
            }

            ChangeState(State::Idle);
            isFirstFrame_ = false;

            // ====================================================
            // ★ 追加：最初のフレームで、装甲ブロックをランダムに散らかす！
            // ====================================================
            blockStartPos_.clear(); // ★ armorManager_ ではなく、BossCoreが直接持っている変数を使います
            Vector3 bossPos = GetTranslate();


            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                // ボスを中心に、半径15～30の距離に散らす
                float angle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>;
                float distance = 15.0f + (static_cast<float>(rand()) / RAND_MAX) * 15.0f;

                // ====================================================
                // ★ 修正：ブロックは「ボスの子供（ローカル座標）」なので計算を変えます！
                // ボスがどんな高さにいても、(0.5f - ボスの高さ) にすることで
                // ワールド空間での高さを強制的に 0.5f (地面) に揃えることができます！
                // ====================================================
                Vector3 scatterPos = {
                    std::cos(angle) * distance, // X: 子オブジェクトなので bossPos.x を足さなくてOK！
                    0.5f - bossPos.y,           // Y: 地面の高さ(0.5f) - ボスの高さ
                    std::sin(angle) * distance  // Z: 子オブジェクトなので bossPos.z を足さなくてOK！
                };
                blockStartPos_.push_back(scatterPos);

                // 初期位置にブロックをワープさせておく
                if (armorBlocks_[i]) {
                    armorBlocks_[i]->SetTranslate(scatterPos);

                    // ただの瓦礫感を出すため、初期角度をめちゃくちゃにする
                    float rX = (static_cast<float>(rand()) / RAND_MAX) * 3.1415f;
                    float rY = (static_cast<float>(rand()) / RAND_MAX) * 3.1415f;
                    float rZ = (static_cast<float>(rand()) / RAND_MAX) * 3.1415f;
                    armorBlocks_[i]->SetRotation({ rX, rY, rZ });

                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }
        }

        switch (state_) {
        case State::Idle:
            UpdateIdle(deltaTime); // 内部で使い分け
            break;
        case State::Attack:
            if (currentAttack_) {
                currentAttack_->Update(this, actionDelta); // 攻撃モーションは倍速

                // ★ TriggerCrashStun() 等でステートが Weak に変わった場合、
                //    currentAttack_ がリセット済みの可能性があるため再チェック
                if (currentAttack_ && currentAttack_->IsFinished()) {
                    currentAttack_.reset();

                    if (isFinalPhase_) {
                        ChangeState(State::Idle); 
                    }
                    else {
                        ChangeState(State::Idle);
                    }
                }
            }
            break;
        case State::Weak:
            UpdateWeak(deltaTime); // 内部で使い分け
            break;
        }
    }
    // ==========================================
    // ★ 魔法の処理：破壊されたブロックの強制消去
    // ==========================================
    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (blockBroken_[i] && armorBlocks_[i]) {
            armorBlocks_[i]->SetScale({ 0.0f, 0.0f, 0.0f });
            armorBlocks_[i]->SetCollisionAttribute(0);
        }
    }

    // ==========================================
    // ★ 追加：破片の物理計算と退場タイマーを進める
    // ==========================================
    UpdateCorePieces(deltaTime);

    // ★ 追加：コアとブロックを繋ぐエネルギー結線の更新
    // ====================================================
    // ★ 追加：ブロックの移動トレース（残像）エフェクト
    // ====================================================
    if (prevBlockPositions_.size() != armorBlocks_.size()) {
        prevBlockPositions_.resize(armorBlocks_.size());
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (armorBlocks_[i]) prevBlockPositions_[i] = armorBlocks_[i]->GetWorldPosition();
        }
    }

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (!armorBlocks_[i] || blockBroken_[i]) continue;

        Vector3 currentPos = armorBlocks_[i]->GetWorldPosition();
        Vector3 prevPos = prevBlockPositions_[i];

        // 移動距離を計算
        Vector3 diff = currentPos - prevPos;
        float moveDist = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

        // 一定以上動いていたら（高速移動中なら）トレースを出す
        if (moveDist > 0.1f) {
            // 放出量をさらに抑制 (最大2個)
            int count = static_cast<int>(moveDist * 5.0f);
            if (count < 1) count = 1;
            if (count > 2) count = 2;

            for (int c = 0; c < count; ++c) {
                float t = static_cast<float>(c) / static_cast<float>(count);
                Vector3 emitPos = Math::Lerp(prevPos, currentPos, t);
                GPUParticleManager::GetInstance()->Emit("BossBlockTrail", emitPos);
            }
        }

        prevBlockPositions_[i] = currentPos;
    }

    // UpdateTethers(deltaTime);
}

// =================================================================
// ステート状態管理
// =================================================================
void BossCore::ChangeState(State nextState) {
    state_ = nextState;

    uint32_t coreAttribute;
    uint32_t blockAttribute;

    // トドメ待ち状態の判定
    if (isWaitingForDeath_ || isWaitingForFinisher_) {
        coreAttribute = kEnemy;
        blockAttribute = 0;
    }
    else {
        if (state_ == State::Attack) {
            coreAttribute = kEnemy | kGround;
        }
        else if (state_ == State::Weak) {
            coreAttribute = kEnemy | kGround;
        }
        else {
            if (isBattleStarted_) {
                coreAttribute = kEnemy | kGround;
            }
            else {
                coreAttribute = kGround;
            }
        }
        blockAttribute = (state_ == State::Attack) ? (kEnemyAttack | kGround) : kGround;
    }

    SetCollisionAttribute(coreAttribute);
    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetCollisionAttribute(coreAttribute);
        }
    }

    if (state_ == State::Idle) {
        hasResetColorPreAttack_ = false;
        if (!isWaitingForDeath_) {
            SetColor(greenColor_);
            defaultColor_ = greenColor_;
        }
    }
    else if (state_ == State::Attack) {
        SetColor(originalColor_);
        defaultColor_ = originalColor_;
    }

    for (Object3d* block : armorBlocks_) {
        if (block) {
            block->SetCollisionAttribute(blockAttribute);
            if (state_ == State::Attack) {
                SetBlockColor(block, { 1.0f, 1.0f, 1.0f, 1.0f });
            }
            // ★ 追加：スタン(Weak)時のスケールを元に戻す
            if (state_ == State::Weak) {
                block->SetScale({ 1.0f, 1.0f, 1.0f });
                SetBlockColor(block, { 1.0f, 1.0f, 1.0f, 1.0f });

                // ★スタン時に子ブロック（Shard）の展開を強制収束（元のコンパクトな位置に戻す）
                for (auto* child : block->GetChildren()) {
                    if (child && child->GetName().find("Shard") != std::string::npos) {
                        Vector3 basePos = child->GetTranslate();
                        Vector3 dir = Math::Normalize(basePos);
                        if (Math::Length(dir) < 0.1f) dir = { 0.0f, 1.0f, 0.0f };

                        // 元の配置の基準距離（吸収したブロックなら0.25f、初期配置なら0.35f）に収束
                        float defaultOffset = 0.35f;
                        if (block->GetName().find("Enemy_Block") == std::string::npos) {
                            defaultOffset = 0.175f;
                        }
                        child->SetTranslate(dir * defaultOffset);
                    }
                }
            }
        }
    }

    if (state_ == State::Weak) {
        SetScale({ 1.0f, 1.0f, 1.0f }); // コア本体もリセット
    }

    switch (state_) {
    case State::Idle:
        animTimer_ = 0.0f;
        startIdlePos_ = GetTranslate(); // 遷移時の座標を保存
        break;

    case State::Attack: {
        static int lastAttack = 0;
        int totalWeight = 0;
        std::vector<::AttackWeight> candidates;

        // HP半分イベントがトリガーされたら第2形態のテーブル、それ以外なら第1形態のテーブルを使用
        const auto& attackPool = isHpHalfTriggered_ ? attackParams_.phase2Attacks : attackParams_.phase1Attacks;

        // 現在生きている（破壊されていない）装甲ブロックの数を数える
        int activeArmorCount = 0;
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (!blockBroken_[i]) {
                activeArmorCount++;
            }
        }

        // 残りの壊れていない装甲がしきい値以下、かつ満タンでない場合に発動
        bool isLowArmorTriggered = (activeArmorCount <= attackParams_.lowArmorThreshold) && !IsArmorFull();

        int nextAttack = 1;
        bool isForcedAbsorb = false;

        if (isLowArmorTriggered) {
            // 0〜99の乱数を取得し、設定された確率（％）未満なら「吸収攻撃 (ID: 7)」を確定させる！
            int roll = std::rand() % 100;
            if (roll < attackParams_.lowArmorAbsorbRate) {
                nextAttack = 7;
                isForcedAbsorb = true;
            }
        }

        if (!isForcedAbsorb) {
            // 通常通りの重み付き抽選を行う
            for (const auto& a : attackPool) {
                // 他に候補がある場合は、連続して同じ攻撃を出すのを防ぐ
                if (attackPool.size() > 1 && a.id == lastAttack) continue;
                // 通常抽選からは、装甲満タン時の吸収攻撃のみ除外
                if (a.id == 7 && IsArmorFull()) continue;

                candidates.push_back(a);
                totalWeight += a.weight;
            }

            // 連続制限などで候補が空になってしまった場合のセーフティ
            if (candidates.empty() && !attackPool.empty()) {
                for (const auto& a : attackPool) {
                    if (a.id == 7 && IsArmorFull()) continue;
                    candidates.push_back(a);
                    totalWeight += a.weight;
                }
            }

            if (totalWeight > 0) {
                int randomVal = std::rand() % totalWeight;
                int currentSum = 0;
                for (const auto& c : candidates) {
                    currentSum += c.weight;
                    if (randomVal < currentSum) {
                        nextAttack = c.id;
                        break;
                    }
                }
            }
            else if (!attackPool.empty()) {
                // 重みが設定されていない場合のセーフティ
                nextAttack = attackPool[std::rand() % attackPool.size()].id;
            }
        }

        lastAttack = nextAttack;

        if (s_debugForceAttack != 0) {
            nextAttack = s_debugForceAttack;
            s_debugForceAttack = 0;
        }

        if (isFinalPhase_) {
            nextAttack = 8;
        }

        animTimer_ = 0.0f;

        if (nextAttack == 1) currentAttack_ = std::make_unique<BossAttack1_Rush>();
        else if (nextAttack == 2) currentAttack_ = std::make_unique<BossAttack2_Shoot>();
        else if (nextAttack == 3) currentAttack_ = std::make_unique<BossAttack3_Hammer>();
        else if (nextAttack == 4) currentAttack_ = std::make_unique<BossAttack4_Wall>();
        else if (nextAttack == 5) currentAttack_ = std::make_unique<BossAttack5_Humanoid>();
        else if (nextAttack == 6) currentAttack_ = std::make_unique<BossAttack6_Laser>();
        else if (nextAttack == 7) currentAttack_ = std::make_unique<BossAttack7_Absorb>();
        else if (nextAttack == 8) currentAttack_ = std::make_unique<BossAttack8_Final>();
        else if (nextAttack == 9) currentAttack_ = std::make_unique<BossAttack9_Funnels>();
        else if (nextAttack == 10) currentAttack_ = std::make_unique<BossAttack9_Spawn>();

        if (currentAttack_) {
            currentAttack_->Initialize(this);
        }
        break;
    }

    case State::Weak:
        animTimer_ = 0.0f;
        break;
    }
}

void BossCore::StartAppearance() {
    if (isAppearing_ || isBattleStarted_) return;

    isAppearing_ = true;

    // ====================================================
    // ★ 変更：まずは「フェーズ0（1秒間の完全静止）」からスタート！
    // ====================================================
    appearancePhase_ = 0;
    appearanceTimer_ = 1.0f; // 1秒待つ

    DebugConsole::GetInstance()->AddLog("[EVENT] ボス部屋到達…1秒間の静寂！");
}

void BossCore::TakeBodyDamage(float damage) {
    // 既に爆散演出中、またはHP半分演出中、またはトドメ落下中は無敵
    if (deathPhase_ != 0 || isHpHalfEventActive_ || isFinisherFalling_) return;

    // 被弾前の色を保存
    SaveOriginalColors();

    // 赤色演出（ダメージフィードバック）
    SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
    colorResetTimer_ = 0.15f;

    // パラメータが設定されていなければ初期化する (安全策)
    if (!param_.has_value()) {
        param_ = EntityParameter();
        param_->hp = 1000.0f;
        param_->maxHp = 1000.0f;
    }

    float halfHp = param_->maxHp * 0.5f;
    float nextHp = param_->hp - damage;

    // ====================================================
    // ★ 追加：HPが50%を下回る瞬間に演出を開始し、HPを50%で止める
    // ====================================================
    if (!isHpHalfTriggered_ && nextHp <= halfHp) {
        param_->hp = halfHp;
        isHpHalfTriggered_ = true;

        // ★追加：ムービーに入るため、プレイヤーのロックオンを強制解除する
        if (target_) {
            if (auto player = dynamic_cast<Player*>(target_)) {
                player->RequestClearLockOn();
            }
        }

        // 強制的に待機状態へリセット
        if (currentAttack_) {
            currentAttack_.reset();
        }
        ChangeState(State::Idle);
        animTimer_ = 0.0f;

        SetColor(greenColor_);
        defaultColor_ = greenColor_;
        SetTranslate({ 0.0f, 4.0f, 0.0f }); // 演出開始時に強制的に真ん中へ移動(T)
        SetScale({ 1.0f, 1.0f, 1.0f });     // スケールをリセット(S)
        SetRotation({ 0.0f, 0.0f, 0.0f });  // 回転をリセット(R)
        flyingBlocks_.clear();
        FullyRecoverBarrierAndArmor();

        // HPが半分になった時、ステージの地面にまばらに20個のブロックを設置する
        if (auto currentScene = SceneManager::GetInstance()->GetCurrentScene()) {
            if (common_) {
                struct BlockSpawnInfo {
                    int x;
                    float y;
                    int z;
                    float scale;
                    float rotY;
                    float rotZ;
                };
                std::vector<BlockSpawnInfo> spawnInfos;
                spawnInfos.reserve(20);

                // 動的にまばらな配置を生成（最小距離を保証したランダム配置）
                for (int i = 0; i < 20; ++i) {
                    float x = 0.0f, z = 0.0f;
                    bool farEnough = false;
                    int attempts = 0;
                    while (!farEnough && attempts < 100) {
                        // 半径 15.0f 〜 65.0f の範囲に散布
                        float radius = 15.0f + (static_cast<float>(rand()) / RAND_MAX) * 50.0f;
                        float angle = (static_cast<float>(rand()) / RAND_MAX) * 3.14159265f * 2.0f;
                        x = std::cos(angle) * radius;
                        z = std::sin(angle) * radius;

                        // 他のブロックと十分に離れているかチェック（最小距離 15.0f）
                        farEnough = true;
                        for (const auto& existing : spawnInfos) {
                            float dx = x - static_cast<float>(existing.x);
                            float dz = z - static_cast<float>(existing.z);
                            float distSq = dx * dx + dz * dz;
                            if (distSq < 15.0f * 15.0f) {
                                farEnough = false;
                                break;
                            }
                        }
                        attempts++;
                    }

                    float scale = 1.0f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f;
                    // Y軸、Z軸ともに完全にランダムな回転角を適用
                    float rotY = (static_cast<float>(rand()) / RAND_MAX) * 3.14159265f * 2.0f;
                    float rotZ = (static_cast<float>(rand()) / RAND_MAX) * 3.14159265f * 2.0f;

                    // 浮動小数点数は整数値（例：1.0f, -2.0f）となるように丸める
                    // 埋まり具合を表現するため、y座標は 0.7f に下げる
                    spawnInfos.push_back({
                        static_cast<int>(std::round(x)),
                        0.7f,
                        static_cast<int>(std::round(z)),
                        scale,
                        rotY,
                        rotZ
                    });
                }

                int blockIdx = 0;
                for (const auto& info : spawnInfos) {
                    auto mapBlock = std::make_unique<MapBlock>();
                    mapBlock->Initialize(common_);

                    // JSON設定と同一の設定を適用
                    mapBlock->SetName("HP_Half_Placed_Block_" + std::to_string(blockIdx++));
                    mapBlock->SetClassName("MapBlock");
                    mapBlock->SetModel("block");
                    
                    // スケールを1〜1.5の範囲でまばらに適用
                    mapBlock->SetScale({ info.scale, info.scale, info.scale });
                    
                    // 回転（Y軸とZ軸の両方）をまばらに適用
                    mapBlock->SetRotation({ 0.0f, info.rotY, info.rotZ });
                    
                    mapBlock->SetTranslate({ static_cast<float>(info.x), static_cast<float>(info.y), static_cast<float>(info.z) });

                    // 衝突属性とマスク (kGround | kMapBlock / kPlayer | kEnemy)
                    mapBlock->SetCollisionAttribute(kGround | kMapBlock);
                    mapBlock->SetCollisionMask(kPlayer | kEnemy);

                    // グラフィックス/マテリアル
                    mapBlock->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    mapBlock->SetEmissive(1.0f);
                    mapBlock->SetEnableEnvMap(true);
                    mapBlock->SetEnvIntensity(0.1f);
                    mapBlock->SetMetallic(0.6f);
                    mapBlock->SetRoughness(0.8f);
                    mapBlock->SetEnableNormalMap(false);
                    mapBlock->SetNormalMap("Resources/sprite/b.png");

                    // コライダー (OBB, size 1x1x1, center 0,0,0)
                    Object3d::ColliderConfig config = mapBlock->GetColliderConfig();
                    config.type = ColliderType::kOBB;
                    config.center = { 0.0f, 0.0f, 0.0f };
                    config.size = { 1.0f, 1.0f, 1.0f };
                    config.rotation = { 0.0f, 0.0f, 0.0f };
                    mapBlock->SetColliderConfig(config);

                    // 行列更新
                    mapBlock->UpdateLocalMatrix();
                    mapBlock->UpdateWorldMatrix();

                    // 登録
                    CollisionManager::GetInstance()->AddObject(mapBlock.get());
                    currentScene->AddObject(std::move(mapBlock));
                }
            }
        }

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (armorBlocks_[i]) {
                armorBlocks_[i]->SetParent(this);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                OrbitData orbit = GetIdleOrbit(i);
                armorBlocks_[i]->SetTranslate(orbit.pos);
                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->SetScale(orbit.scale);
                armorBlocks_[i]->SetCollisionAttribute(kGround);
            }
        }

        isHpHalfEventActive_ = true;
        hpHalfPhase_ = HpHalfEventPhase::WaitIdle;
        hpHalfEffectTimer_ = 0.0f;

        if (target_) {
            if (auto player = dynamic_cast<Player*>(target_)) {
                player->SetIsControlActive(false);
                player->SetVelocity({ 0.0f, 0.0f, 0.0f });
                player->SetTranslate({ 0.0f, 1.231f, -60.0f });
                player->UpdateWorldMatrix();
            }
        }

        // ★ 追加：作成いただいたカメラアニメーション（JSON）を再生する
        // ゴーストレーダー（GhostRecorder）で作成されたアニメーションを直接再生（橋が落ちる処理と同じ方式）
        bool isCameraFound = false;
        if (sceneManager_ && sceneManager_->GetCurrentScene()) {
            auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
            for (auto& obj : objects) {
                // ★ 修正：ユーザーが配置した専用のカメラオブジェクト「EnemyHP50_Animation」を探す！
                if (obj->GetName() == "EnemyHP50_Animation") {
                    isCameraFound = true;
                    if (obj->recorder_) {
                        obj->recorder_->Play("EnemyHP50_Animation", false, false, true);
                        DebugConsole::GetInstance()->AddLog("【EVENT】 " + obj->GetName() + " を代用して EnemyHP50_Animation を再生指示！");
                    } else {
                        DebugConsole::GetInstance()->AddLog("【エラー】 " + obj->GetName() + " に GhostRecorder がアタッチされていません！");
                    }
                    break;
                }
            }
        } else {
            DebugConsole::GetInstance()->AddLog("【エラー】 sceneManager_ または GetCurrentScene() が nullptr です！");
        }
        
        if (!isCameraFound && sceneManager_ && sceneManager_->GetCurrentScene()) {
            DebugConsole::GetInstance()->AddLog("【エラー】 シネマティックカメラがシーン内に見つかりません！");
        }

        DebugConsole::GetInstance()->AddLog("【EVENT】 ボスHPが50%に到達！演出開始。");
        return;
    }

    // ====================================================
    // ★ 追加：最終奥義発動前、または大技中でトドメ待ちでないならHP1で踏みとどまる！
    // ====================================================
    if (!isFinalPhase_ && nextHp <= 1.0f) {
        nextHp = 1.0f;
    } else if (isFinalPhase_ && !isWaitingForFinisher_ && nextHp <= 1.0f) {
        nextHp = 1.0f;
    }

    param_->hp = nextHp;

    // HPが0以下になったら死亡演出を開始
    if (param_->hp <= 0.0f) {
        param_->hp = 0.0f;
        StartDeathSequence();
    }
}

// =================================================================
// 各ステートの個別更新処理
// =================================================================

void BossCore::UpdateIdle(float deltaTime) {
    if (isWaitingForFinisher_) {
        // 周りのブロックを念のため消去（通常は消えているはず）
        for (Object3d* block : armorBlocks_) {
            if (block) {
                block->SetScale({ 0.0f, 0.0f, 0.0f });
                block->SetCollisionAttribute(0);
            }
        }

        if (isFinisherFalling_) {
            // 落下物理
            float gravity = 40.0f;
            finisherFallVelocity_ -= gravity * deltaTime;

            Vector3 pos = GetTranslate();
            pos.y += finisherFallVelocity_ * deltaTime;

            float groundY = 0.8f;
            if (pos.y <= groundY) {
                pos.y = groundY;

                // バウンド処理
                if (finisherBounceCount_ < 2) {
                    finisherFallVelocity_ = -finisherFallVelocity_ * 0.4f; // 反発係数0.4
                    finisherBounceCount_++;

                    // 着地時の土煙エフェクト
                    GPUParticleManager::GetInstance()->Emit("BossHitSpark", pos, Math::MakeIdentity4x4());
                } else {
                    pos.y = groundY;
                    finisherFallVelocity_ = 0.0f;
                    isFinisherFalling_ = false; // 落下終了
                    DebugConsole::GetInstance()->AddLog("【トドメ待ち】 ボスが着地した！プレイヤーよ、とどめを刺せ！");
                }
            }
            SetTranslate(pos);

            // 落下中の回転（ぐるぐる回りながら落ちる）
            Vector3 rot = GetRotation();
            rot.y += 10.0f * deltaTime;
            rot.x += 5.0f * deltaTime;
            SetRotation(rot);
        }
        else {
            // 着地後の弱々しい待機モーション（ピクピク動く、またはゆっくり明滅する）
            float time = s_globalIdleTimer * 2.0f;
            float hover = 0.8f + std::sin(time * 3.0f) * 0.1f; // 低い位置でピクピク
            Vector3 pos = GetTranslate();
            pos.y = hover;
            pos.x = 0.0f;
            pos.z = 0.0f; // 中央固定
            SetTranslate(pos);

            // 弱々しい回転
            SetRotation({ 0.3f, std::sin(time * 0.5f) * 0.5f, 0.0f });

            // 赤と黒の弱々しい明滅
            float pulse = (std::sin(time * 4.0f) + 1.0f) * 0.5f;
            SetColor({ 1.0f, pulse * 0.2f, pulse * 0.2f, 1.0f });
        }
        return;
    }

    if (isWaitingForDeath_) {
        // (トドメ待ちのボロボロ処理…そのまま)
        return;
    }



    // ====================================================
    // フェーズ1（咆哮開始）になってから、初めて合体タイマーを進める
    // ====================================================
    if (appearancePhase_ == 1 || (!isBattleStarted_ && assemblyTimer_ > 0.0f)) {
        assemblyTimer_ += deltaTime;
    }
    else if (isBattleStarted_) {
        assemblyTimer_ = 3.0f; // 戦闘中はMAX(3秒)にしておく
    }

    // ====================================================
    // コア本体の待機モーション（鼓動と浮遊）
    // ====================================================
    if (isBattleStarted_) {
        float actionDelta = deltaTime * kBaseSpeedMultiplier;
        // 1. 鼓動
        float t = std::fmod(s_globalIdleTimer, 2.0f);
        float pulse = 0.0f;
        if (t < 0.15f) {
            pulse = std::sin((t / 0.15f) * std::numbers::pi_v<float>);
        } else if (t > 0.25f && t < 0.4f) {
            pulse = std::sin(((t - 0.25f) / 0.15f) * std::numbers::pi_v<float>) * 0.7f;
        }
        
        float scaleVal = 1.0f + pulse * 0.2f;
        SetScale({ scaleVal, scaleVal, scaleVal });

        // 2. 浮遊 (登場ムービー終了時や攻撃終了時の瞬間移動を防止するため、最初の1.5秒間は前の位置からスムーズにLerp)
        float hoverY = 4.0f + std::sin(s_globalIdleTimer * 1.5f) * 0.3f;
        Vector3 targetPos = { 0.0f, hoverY, 0.0f }; // 待機状態では常に中央を目標にする

        if (animTimer_ < 1.5f) {
            float transitionT = std::min(animTimer_ / 1.5f, 1.0f);
            float easeTransition = 1.0f - std::pow(1.0f - transitionT, 3.0f); // OutCubic
            Vector3 currentPos = Math::Lerp(startIdlePos_, targetPos, easeTransition);
            SetTranslate(currentPos);
        }
        else {
            SetTranslate(targetPos);
        }
    }

    // ====================================================
    // ★ ここが圧倒的カッコよさの秘密！
    // 1.8秒（咆哮が終わって元のサイズに戻る時間）までは 0% で完全待機。
    // 1.8秒を過ぎたら、0.7秒間かけて一気にシュバッ！と集める！
    // ====================================================
    float t = 0.0f;
    if (assemblyTimer_ > 1.8f) {
        t = std::min((assemblyTimer_ - 1.8f) / 0.7f, 1.0f);
    }

    // カッコいいイージング計算（3乗アウト）：最初は早く、ボスに近づくにつれてゆっくり！
    float easeT = 1.0f - std::pow(1.0f - t, 3.0f);
    Vector3 coreScale = GetScale();

    float actionDelta = deltaTime * kBaseSpeedMultiplier;

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        OrbitData orbit = GetIdleOrbit(i);
        Vector3 localPos = orbit.pos;
        localPos.x /= coreScale.x;
        localPos.y /= coreScale.y;
        localPos.z /= coreScale.z;

        Vector3 localScale = orbit.scale;
        localScale.x /= coreScale.x;
        localScale.y /= coreScale.y;
        localScale.z /= coreScale.z;

        if (i < blockStartPos_.size()) {
            Vector3 pos = Math::Lerp(blockStartPos_[i], localPos, easeT);
            armorBlocks_[i]->SetTranslate(pos);
        }
        else {
            armorBlocks_[i]->SetTranslate(localPos);
        }

        armorBlocks_[i]->SetScale(localScale);
        armorBlocks_[i]->SetRotation(orbit.rot);
        armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
    }

    // ==========================================
    // 戦闘開始フラグがONの時だけ、攻撃へのタイマーを進める
    // ==========================================
    if (isBattleStarted_) {

        // ====================================================
        // ★ 修正：4, 5, 6, 7, 8秒のどれかをピタリ選ぶ
        // ====================================================
        static float targetIdleTime = 5.0f;
        if (animTimer_ == 0.0f) {
            // rand() % 5 は「0, 1, 2, 3, 4」のどれかになるので、それに4を足すと「4, 5, 6, 7, 8」になります！
            int randomSeconds = 4 + (std::rand() % 5);
            targetIdleTime = static_cast<float>(randomSeconds);

            DebugConsole::GetInstance()->AddLog("[AI] 次の攻撃まで " + std::to_string(randomSeconds) + " 秒待機します");
        }

        animTimer_ += deltaTime;

        // 攻撃の1秒前に色を水色（青っぽい色）に戻す！
        if (animTimer_ >= targetIdleTime - 1.5f && !hasResetColorPreAttack_) {
            SetColor(originalColor_);
            defaultColor_ = originalColor_;
            hasResetColorPreAttack_ = true;
        }

        // タイマーが「今回決めた目標時間」を超えたら攻撃へ！
        if (animTimer_ >= targetIdleTime) {
            ChangeState(State::Attack);
        }
    }
}

void BossCore::UpdateWeak(float deltaTime) {
    float actionDelta = deltaTime * kBaseSpeedMultiplier;
    float duration = std::max(0.0f, attackParams_.stunDuration - 2.0f); // Reduce stun time by 2 seconds
    float wakeUpStart = duration > 2.0f ? duration - 2.0f : duration * 0.666f;
    float wakeUpDuration = duration - wakeUpStart;

    if (isCrashStun_) {
        // 自爆スタンの場合はバリアHPを変動させず、専用タイマーを進める
        crashStunTimer_ += deltaTime;
        if (crashStunTimer_ > duration) {
            crashStunTimer_ = duration;
        }
        animTimer_ = crashStunTimer_;
    }
    else {
        // スタンゲージ（barrierHp_）を duration 秒かけて0からmaxBarrierHp_まで回復（実時間で回復）
        barrierHp_ += (maxBarrierHp_ / duration) * deltaTime;
        if (barrierHp_ > maxBarrierHp_) {
            barrierHp_ = maxBarrierHp_;
        }
        // ゲージの回復進捗に合わせてアニメーションタイマー(0.0s〜duration)を逆算同期
        animTimer_ = (barrierHp_ / maxBarrierHp_) * duration;
    }

    // --- コア本体の落下・転がり（コロン）・復帰 ---
    Vector3 bossPos = GetTranslate();
    float fallDuration = 1.2f; // 0.5s -> 1.2sへ（重々しく倒れる）
    float rollAngle = 0.0f;

    // 1. 落下と転がり
    if (animTimer_ <= fallDuration) {
        float t = animTimer_ / fallDuration;
        float easeT = std::pow(t, 2.0f);
        bossPos.y = Math::Lerp(4.0f, 0.5f, easeT);
        rollAngle = Math::Lerp(0.0f, 90.0f * (std::numbers::pi_v<float> / 180.0f), easeT);
        SetTranslate(bossPos);
    }
    // 2. 起き上がり（最後の wakeUpDuration 秒）
    else if (animTimer_ > wakeUpStart) {
        float wakeUpT = (animTimer_ - wakeUpStart) / wakeUpDuration;
        float easeT = 1.0f - std::pow(1.0f - wakeUpT, 3.0f); // ふわりと浮くEaseOut
        float targetHoverY = 4.0f + std::sin(s_globalIdleTimer * 1.5f) * 0.3f; // 待機浮遊アニメのY座標にブレンド
        bossPos.y = Math::Lerp(0.5f, targetHoverY, easeT);
        rollAngle = Math::Lerp(90.0f * (std::numbers::pi_v<float> / 180.0f), 0.0f, easeT);
        SetTranslate(bossPos);
    }
    // 3. 地面でダウン中
    else {
        bossPos.y = 0.5f;
        rollAngle = 90.0f * (std::numbers::pi_v<float> / 180.0f);
        SetTranslate(bossPos);
    }

    SetRotation({ rollAngle, GetRotation().y, 0.0f });

    // --- フリッカー（点滅）エフェクト ---
    int flicker = static_cast<int>(animTimer_ * 15.0f) % 4; // ランダムっぽくチカチカさせる
    bool isLightOn = (flicker == 0 && animTimer_ < wakeUpStart);

    if (animTimer_ > wakeUpStart) {
        // 起き上がり中は徐々に元の色に戻す
        float wakeUpT = (animTimer_ - wakeUpStart) / wakeUpDuration;
        Vector4 color;
        color.x = Math::Lerp(0.3f, greenColor_.x, wakeUpT);
        color.y = Math::Lerp(0.3f, greenColor_.y, wakeUpT);
        color.z = Math::Lerp(0.3f, greenColor_.z, wakeUpT);
        color.w = 1.0f;
        SetColor(color);
    }
    else {
        SetColor(isLightOn ? greenColor_ : Vector4{ 0.3f, 0.3f, 0.3f, 1.0f });
    }

    // --- 装甲ブロックの動き ---
    float scatterDuration = 0.8f;
    float scatterT = std::min(animTimer_ / scatterDuration, 1.0f);
    float easeT = 1.0f - std::pow(1.0f - scatterT, 3.0f); // OutCubic

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
            // 現在の補間位置（ワールド空間）
            Vector3 currentPos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);

            // 落下中のバウンドエフェクト（ワールド空間のY座標に加算するため、完全に真上に跳ねる！）
            if (scatterT < 1.0f) {
                float bounce = std::abs(std::sin(scatterT * std::numbers::pi_v<float> * 2.0f)) * (1.0f - scatterT) * 4.0f;
                currentPos.y += bounce;
            }

            Vector3 currentRot = Math::Lerp(blockStartRot_[i], blockTargetRot_[i], easeT);
            // 落下中はワールド空間で追加の回転スピンを与える
            if (scatterT < 1.0f) {
                currentRot.x += 5.0f * animTimer_;
                currentRot.y += 3.0f * animTimer_;
            }

            // 起き上がり中のブロック補間（コアへスムーズに戻る）
            if (animTimer_ > wakeUpStart) {
                float wakeUpT = (animTimer_ - wakeUpStart) / wakeUpDuration;
                float returnEaseT = 1.0f - std::pow(1.0f - wakeUpT, 3.0f); // OutCubic
                
                OrbitData orbit = GetIdleOrbit(i);
                // 戻り先のターゲットワールド座標をボスの現在のワールド行列から計算
                Vector3 targetWorldPos = Math::Transform(orbit.pos, GetWorldMatrix());
                currentPos = Math::Lerp(currentPos, targetWorldPos, returnEaseT);

                // スケールも1.0fから orbit.scale へスムーズにイージング補間
                Vector3 currentScale = Math::Lerp({ 1.0f, 1.0f, 1.0f }, orbit.scale, returnEaseT);
                armorBlocks_[i]->SetScale(currentScale);

                // 戻り先のターゲットワールド回転（ボスの回転＋軌道自体の回転）
                Vector3 targetWorldRot = {
                    GetRotation().x + orbit.rot.x,
                    GetRotation().y + orbit.rot.y,
                    GetRotation().z + orbit.rot.z
                };

                auto LerpAngle = [](float a, float b, float t) {
                    float diff = b - a;
                    while (diff < -std::numbers::pi_v<float>) diff += 2.0f * std::numbers::pi_v<float>;
                    while (diff > std::numbers::pi_v<float>) diff -= 2.0f * std::numbers::pi_v<float>;
                    return a + diff * t;
                };
                currentRot.x = LerpAngle(currentRot.x, targetWorldRot.x, returnEaseT);
                currentRot.y = LerpAngle(currentRot.y, targetWorldRot.y, returnEaseT);
                currentRot.z = LerpAngle(currentRot.z, targetWorldRot.z, returnEaseT);
            }

            armorBlocks_[i]->SetTranslate(currentPos);
            armorBlocks_[i]->SetRotation(currentRot);
            armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;

            // ブロックも点滅
            if (animTimer_ > 4.0f) {
                float wakeUpT = (animTimer_ - 4.0f) / 2.0f;
                SetBlockColor(armorBlocks_[i], { 0.3f + wakeUpT * 0.7f, 0.3f + wakeUpT * 0.7f, 0.3f + wakeUpT * 0.7f, 1.0f });
            }
            else {
                SetBlockColor(armorBlocks_[i], isLightOn ? Vector4{ 0.8f, 0.8f, 0.8f, 1.0f } : Vector4{ 0.3f, 0.3f, 0.3f, 1.0f });
            }
        }
    }

    // --- duration 秒経過でステート復帰 ---
    if (animTimer_ >= duration) {
        animTimer_ = 0.0f;
        isCrashStun_ = false;
        crashStunTimer_ = 0.0f;
        SetRotation({ 0.0f, GetRotation().y, 0.0f });
        SetColor(greenColor_);
        defaultColor_ = greenColor_;
        Vector3 finalPos = GetTranslate();
        finalPos.y = 4.0f + std::sin(s_globalIdleTimer * 1.5f) * 0.3f; // 完全に待機モーションの浮遊位置に同期
        SetTranslate(finalPos);

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (armorBlocks_[i]) {
                armorBlocks_[i]->SetParent(this);
                SetBlockColor(armorBlocks_[i], { 1.0f, 1.0f, 1.0f, 1.0f });

                // 復帰時に完全に元のスケール・回転・位置に揃える
                OrbitData orbit = GetIdleOrbit(i);
                armorBlocks_[i]->SetTranslate(orbit.pos);
                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->SetScale(orbit.scale);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        ChangeState(State::Idle);
    }
}

void BossCore::UpdateFlyingBlocks(float deltaTime) {
    float actionDelta = deltaTime * kBaseSpeedMultiplier;
    int landedCount = 0;
    static Math math;

    for (auto& fb : flyingBlocks_) {
        if (!fb.block) continue;

        if (fb.mode == 4) {
            Vector3 bossPos = GetTranslate();
            Vector3 headPos = { bossPos.x, bossPos.y + 4.0f, bossPos.z };
            Vector3 currentPos = fb.block->GetTranslate();

            Vector3 dir = { headPos.x - currentPos.x, headPos.y - currentPos.y, headPos.z - currentPos.z };
            float distance = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (distance < 0.5f) {
                fb.block->SetTranslate(headPos);

                // ターゲット（プレイヤー）への追従
                if (IsTargetValid()) {
                    Vector3 targetPos = target_->GetWorldPosition();
                    targetPos.y = 0.0f;

                    Vector3 toPlayer = math.Normalize(targetPos - headPos);
                    float bulletSpeed = 60.0f;
                    fb.velocity = { toPlayer.x * bulletSpeed, toPlayer.y * bulletSpeed, toPlayer.z * bulletSpeed };

                    float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                    fb.currentRot = { 0.0f, angleY, 0.0f };
                }
                fb.mode = 0;
            }
            else {
                dir.x /= distance; dir.y /= distance; dir.z /= distance;
                float gatherSpeed = 30.0f;
                currentPos.x += dir.x * gatherSpeed * actionDelta;
                currentPos.y += dir.y * gatherSpeed * actionDelta;
                currentPos.z += dir.z * gatherSpeed * actionDelta;
                fb.block->SetTranslate(currentPos);

                fb.currentRot.x += 15.0f * actionDelta;
                fb.currentRot.y += 30.0f * actionDelta;
                fb.block->SetRotation(fb.currentRot);
            }
            fb.block->GetTransform()->isQuaternionMaster = false;
        }
        else if (fb.mode == 0) {
            Vector3 pos = fb.block->GetTranslate();
            pos.x += fb.velocity.x * actionDelta;
            pos.y += fb.velocity.y * actionDelta;
            pos.z += fb.velocity.z * actionDelta;

            if (pos.y <= 0.0f) {
                pos.y = 0.0f;
                fb.velocity = { 0.0f, 0.0f, 0.0f };
                fb.mode = 1;
            }
            fb.block->SetTranslate(pos);

            Vector3 spinSpeed = { 30.0f, 45.0f, 60.0f };
            fb.currentRot.x += spinSpeed.x * actionDelta;
            fb.currentRot.y += spinSpeed.y * actionDelta;
            fb.currentRot.z += spinSpeed.z * actionDelta;
            fb.block->SetRotation(fb.currentRot);
            fb.block->GetTransform()->isQuaternionMaster = false;

        }
        else if (fb.mode == 1) {
            landedCount++;
        }
        else if (fb.mode == 2) {
            Vector3 bossPos = GetTranslate();
            Vector3 blockPos = fb.block->GetTranslate();

            Vector3 dir = { bossPos.x - blockPos.x, bossPos.y - blockPos.y, bossPos.z - blockPos.z };
            float distance = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (distance < 2.0f) {
                fb.mode = 3;
            }
            else {
                dir.x /= distance; dir.y /= distance; dir.z /= distance;
                float returnSpeed = 60.0f;
                blockPos.x += dir.x * returnSpeed * actionDelta;
                blockPos.y += dir.y * returnSpeed * actionDelta;
                blockPos.z += dir.z * returnSpeed * actionDelta;
                fb.block->SetTranslate(blockPos);

                Vector3 spinSpeed = { 60.0f, 60.0f, 60.0f };
                fb.currentRot.x += spinSpeed.x * actionDelta;
                fb.currentRot.y += spinSpeed.y * actionDelta;
                fb.currentRot.z += spinSpeed.z * actionDelta;
                fb.block->SetRotation(fb.currentRot);
                fb.block->GetTransform()->isQuaternionMaster = false;
            }
        }
    }

    if (!flyingBlocks_.empty() && landedCount == flyingBlocks_.size() && flyingBlocks_.size() == armorBlocks_.size()) {
        returnDelayTimer_ += deltaTime;
        if (returnDelayTimer_ >= 5.0f) {
            for (auto& fb : flyingBlocks_) {
                fb.mode = 2;
                if (fb.block) {
                    fb.block->SetCollisionAttribute(kGround); // 戻るときに攻撃判定をなくす
                }
            }
            returnDelayTimer_ = 0.0f;
        }
    }
    else {
        returnDelayTimer_ = 0.0f;
    }

    for (auto it = flyingBlocks_.begin(); it != flyingBlocks_.end(); ) {
        if (it->mode == 3) {
            it->block->SetParent(this);
            int idx = it->originalIndex;

            OrbitData orbit = GetIdleOrbit(idx);
            it->block->SetTranslate(orbit.pos);
            it->block->SetScale(orbit.scale);
            it->block->SetRotation(orbit.rot);
            it->block->GetTransform()->isQuaternionMaster = false;

            it = flyingBlocks_.erase(it);
        }
        else {
            ++it;
        }
    }
}


void BossCore::TakeBarrierDamage(float damage, Object3d* hitBlock) {
    // 被弾前の色を保存
    SaveOriginalColors();

    barrierHp_ -= damage;

    DebugConsole::GetInstance()->AddLog("[HIT!] Barrier Damaged! 残りHP: " + std::to_string(barrierHp_) + " / " + std::to_string(maxBarrierHp_));

    damageCooldownTimer_ = 1.0f;
    colorResetTimer_ = 0.15f;

    // ==========================================
    // 全部ではなく、当たったブロックだけを赤くする！
    // ==========================================
    if (hitBlock) {
        SetBlockColor(hitBlock, { 1.0f, 0.0f, 0.0f, 1.0f });
    }

    if (barrierHp_ <= 0.0f) {
        DebugConsole::GetInstance()->AddLog("★★★ Barrier BROKEN! ★★★");
        barrierHp_ = 0.0f; // スタン開始時はゲージを0にする（ここから全回復に向けて増加）

        if (currentAttack_) currentAttack_.reset(); // ★ ダウン時の攻撃を強制終了！
        animTimer_ = 0.0f;
        flyingBlocks_.clear();

        blockStartPos_.clear();
        blockTargetPos_.clear();
        blockStartRot_.clear();
        blockTargetRot_.clear();

        Vector3 bossWorldPos = GetWorldPosition();

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            Object3d* block = armorBlocks_[i];
            if (block) {
                // まずブロックの現在のワールド座標とワールド回転を取得
                Vector3 startWorldPos = block->GetWorldPosition();
                Vector3 startWorldRot = block->GetRotation(); // ボス本体がまだ回転していないため、GetRotation()がそのままワールド回転と一致します。

                // 親子関係を解除（ワールド空間へ移行）
                block->SetParent(nullptr);
                block->SetTranslate(startWorldPos);
                block->SetRotation(startWorldRot);
                block->UpdateWorldMatrix();

                blockStartPos_.push_back(startWorldPos);
                blockStartRot_.push_back(startWorldRot);

                // 散らばり先のターゲット座標（ワールド空間）を計算
                float angle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>;
                float distance = 5.0f + (static_cast<float>(rand()) / RAND_MAX) * 8.0f;
                float height = 0.5f; // 地面の高さに平らに置く

                Vector3 scatterPos = {
                    bossWorldPos.x + std::cos(angle) * distance,
                    height,
                    bossWorldPos.z + std::sin(angle) * distance
                };
                blockTargetPos_.push_back(scatterPos);

                // 散らばり先のターゲット回転（ワールド空間）
                Vector3 scatterRot = {
                    0.0f,
                    (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>,
                    0.0f
                };
                blockTargetRot_.push_back(scatterRot);
            }
        }

        SetRotation({ 0.0f, GetRotation().y, 0.0f });
        ChangeState(State::Weak);
    }
}

void BossCore::TriggerCrashStun() {
    DebugConsole::GetInstance()->AddLog("★★★ Boss CRASH STUN! ★★★");
    
    // バリアHP（barrierHp_）は変更せず、現在の値を維持する！

    // ★ currentAttack_.reset() でダウン時の攻撃を強制終了！
    //    呼び出し元（BossAttack1_Rush::Update 等）は TriggerCrashStun() の直後に
    //    即座に return するため、this (攻撃オブジェクト) が破棄されても安全。
    if (currentAttack_) currentAttack_.reset();
    animTimer_ = 0.0f;
    // ★ 予測線を完全に消す（突進終了時と同じ方法）
    if (auto* warning = GetWarningArea()) {
        warning->SetScale({ 0.0f, 0.0f, 0.0f });
        warning->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    }
    isCrashStun_ = true;
    crashStunTimer_ = 0.0f;
    flyingBlocks_.clear();

    blockStartPos_.clear();
    blockTargetPos_.clear();
    blockStartRot_.clear();
    blockTargetRot_.clear();

    Vector3 bossWorldPos = GetWorldPosition();

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        Object3d* block = armorBlocks_[i];
        if (block) {
            // まずブロックの現在のワールド座標とワールド回転を取得
            Vector3 startWorldPos = block->GetWorldPosition();
            Vector3 startWorldRot = block->GetRotation(); // ボス本体がまだ回転していないため、GetRotation()がそのままワールド回転と一致します。

            // 親子関係を解除（ワールド空間へ移行）
            block->SetParent(nullptr);
            block->SetTranslate(startWorldPos);
            block->SetRotation(startWorldRot);
            block->UpdateWorldMatrix();

            blockStartPos_.push_back(startWorldPos);
            blockStartRot_.push_back(startWorldRot);

            // 散らばり先のターゲット座標（ワールド空間）を計算
            float angle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>;
            float distance = 5.0f + (static_cast<float>(rand()) / RAND_MAX) * 8.0f;
            float height = 0.5f; // 地面の高さに平らに置く

            Vector3 scatterPos = {
                bossWorldPos.x + std::cos(angle) * distance,
                height,
                bossWorldPos.z + std::sin(angle) * distance
            };
            blockTargetPos_.push_back(scatterPos);

            // 散らばり先のターゲット回転（ワールド空間）
            Vector3 scatterRot = {
                0.0f,
                (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>,
                0.0f
            };
            blockTargetRot_.push_back(scatterRot);
        }
    }

    SetRotation({ 0.0f, GetRotation().y, 0.0f });
    ChangeState(State::Weak);
}

void BossCore::StartDeathSequence() {
    if (deathPhase_ != 0) return; // 既に死亡処理中なら何もしない

    isWaitingForFinisher_ = false; // トドメ待ちモーションを解除し、座標の固定を防ぐ

    deathPhase_ = 1;         // ★ フェーズ1（無音で静止）
    sequenceTimer_ = 1.0f;   // ★ 1秒間待機！

    DebugConsole::GetInstance()->AddLog("[撃破] ボス沈黙…！！");

    // ====================================================
    // ボスに付いているすべてのパーティクルを止める！
    // これにより、新しいパーティクルが発生しなくなります。
    // ====================================================
    for (auto& emitter : particleEmitters_) {
        if (emitter) {
            emitter->Stop();
        }
    }

    if (currentAttack_) currentAttack_.reset();
    isWaitingForDeath_ = true;
    ChangeState(State::Idle);

    // 周りのブロックを消す
    for (Object3d* block : armorBlocks_) {
        if (block) {
            block->SetScale({ 0.0f, 0.0f, 0.0f });
            block->SetCollisionAttribute(0);
        }
    }

    for (auto& emitter : particleEmitters_) {
        if (emitter) {
            emitter->Stop();
        }
    }

    // ボス登場演出の際のコアの高さ（13.16f）から、周囲の遮蔽物（巨大ブロックなど）を避けるため10m上に配置（Y=23.16f）
    float targetY = 23.160861015319824f;
    this->SetRotation({ 0.0f, 0.0f, 0.0f });
    this->SetTranslate({ 0.07232095301151276f, targetY, -2.0776538848876953f });

    // カメラをパッと切り替え（0秒） - カメラ「a」のアングルと距離（2.5倍）を完全に維持したまま、高さを10m上に平行移動
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        Camera::CameraOverrideParams params;
        params.duration = 0.0f;
        params.trackEyeX = false; params.trackEyeY = false; params.trackEyeZ = false;
        params.fixedEyePos = { 0.4466233355404443f, 24.659861015319824f, -27.02978838708496f };
        params.trackTargetX = false; params.trackTargetY = false; params.trackTargetZ = false;
        params.fixedTargetPos = { 0.07232095301151276f, 23.160861015319824f, -2.0776538848876953f };
        camera->StartOverride(params);
    }
}

// ==========================================
// ★ 段階2：亀裂状態（少し隙間をあけた破片）を出現させる
// ==========================================
void BossCore::ShowCrackedCore() {
    DebugConsole::GetInstance()->AddLog("[撃破] コアに亀裂が…！生成予約");

    // ★ 生成はここではやらず、フラグだけ立てる！
    isShardSpawnRequested_ = true;
}

void BossCore::ActuallySpawnShards() {
    if (!isShardSpawnRequested_) return; // 予約がなければ何もしない

    this->SetScale({ 0.0f, 0.0f, 0.0f });
    this->SetCollisionAttribute(0);

    Vector3 corePos = this->GetTranslate();
    BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene();

    if (currentScene) {
        for (int i = 0; i < 18; ++i) {
            auto pieceObj = std::make_unique<Object3d>();
            pieceObj->Initialize(common_);
            pieceObj->SetStatic(true);
            pieceObj->SetModel("enemy_core_shards/enemy_core" + std::to_string(i + 1));
            pieceObj->SetColor({ 0.0f, 0.5946f, 1.0f, 1.0f });
            pieceObj->SetMaterialType(2);
            pieceObj->SetEmissive(2.0f);
            pieceObj->SetMetallic(0.0f);
            pieceObj->SetRoughness(0.5f);
            pieceObj->SetEnableEnvMap(true);
            pieceObj->SetEnvIntensity(1.035f);
            pieceObj->SetScale({ 1.0f, 1.0f, 1.0f });
            pieceObj->SetCollisionAttribute(0);

            float rx = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;
            float ry = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;
            float rz = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;
            Vector3 crackOffset = { rx * 0.2f, ry * 0.2f, rz * 0.2f };
            pieceObj->SetTranslate({ corePos.x + crackOffset.x, corePos.y + crackOffset.y, corePos.z + crackOffset.z });

            CorePiece piece;
            piece.obj = pieceObj.get();
            piece.velocity = { 0.0f, 0.0f, 0.0f };
            piece.rotSpeed = { 0.0f, 0.0f, 0.0f };
            corePieces_.push_back(piece);

            currentScene->AddObject(std::move(pieceObj));
        }
    }
    isShardSpawnRequested_ = false; // 生成完了！
}

// ==========================================
// ★ 段階3：一気に吹き飛ばす！（爆散）
// ==========================================
void BossCore::BreakCore() {
    isCoreBroken_ = true; // UpdateCorePieces のスローモーションを起動！
    deathTimer_ = 0.0f;

    DebugConsole::GetInstance()->AddLog("[撃破] コア完全粉砕！！！");

    for (auto& piece : corePieces_) {
        if (piece.obj) {
            float rx = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;
            float ry = ((static_cast<float>(rand()) / RAND_MAX) * 1.0f) + 0.5f;
            float rz = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;

            float speed = 30.0f + (rand() % 30);
            piece.velocity = { rx * speed, ry * speed, rz * speed };
            piece.rotSpeed = {
                (rand() % 60) - 30.0f, (rand() % 60) - 30.0f, (rand() % 60) - 30.0f
            };

            // 発光を元に戻す
            piece.obj->SetEmissive(1.0f);
        }
    }
}

void BossCore::UpdateCorePieces(float deltaTime) {
    if (!isCoreBroken_) return;

    deathTimer_ += deltaTime;

    if (deathTimer_ > 8.0f) { // 5.0s -> 8.0sへ延長
        for (auto& piece : corePieces_) {
            if (piece.obj) {
                piece.obj->isDead = true;
                piece.obj = nullptr; // ダングリングポインタ防止
            }
        }
        corePieces_.clear(); // リストもクリア
        isDead = true;
        isCompletelyDead_ = true;
        // ==========================================
        // ★ 変更：ボスが完全に消滅したら、カメラを元のプレイヤー視点に戻す！
        // 一瞬で戻すなら 0.0f に変更します。
        // ==========================================
        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            camera->EndOverride(0.0f); // 0秒で一瞬で戻す
        }
        return;
    }

    // --- スローモーション計算 ---
    float timeScale = 1.0f;
    if (deathTimer_ < 0.2f) { // ヒットストップをわずかに延長
        timeScale = 0.01f;
    }
    else if (deathTimer_ < 2.5f) { // スロー時間を2.5sへ延長
        timeScale = 0.05f;  // 0.2 -> 0.05へ（より深いスロー）
    }
    float slowDeltaTime = deltaTime * timeScale;

    // --- 物理計算 ---
    for (auto& piece : corePieces_) {
        if (piece.obj) {
            Vector3 pos = piece.obj->GetTranslate();

            piece.velocity.y -= 98.0f * slowDeltaTime;

            pos.x += piece.velocity.x * slowDeltaTime;
            pos.y += piece.velocity.y * slowDeltaTime;
            pos.z += piece.velocity.z * slowDeltaTime;

            if (pos.y <= 0.0f) {
                pos.y = 0.0f;
                piece.velocity.y *= -0.5f;
                piece.velocity.x *= 0.8f;
                piece.velocity.z *= 0.8f;
                piece.rotSpeed.x *= 0.8f;
                piece.rotSpeed.y *= 0.8f;
                piece.rotSpeed.z *= 0.8f;
            }

            piece.obj->SetTranslate(pos);

            Vector3 rot = piece.obj->GetRotation();
            rot.x += piece.rotSpeed.x * slowDeltaTime;
            rot.y += piece.rotSpeed.y * slowDeltaTime;
            rot.z += piece.rotSpeed.z * slowDeltaTime;
            piece.obj->SetRotation(rot);

            piece.obj->UpdateWorldMatrix();
        }
    }
}

void BossCore::SetBlockColor(Object3d* block, const Vector4& color) {
    if (!block) return;
    block->SetColor(color);
    for (Object3d* child : block->GetChildren()) {
        if (child) {
            child->SetColor(color);
        }
    }
}

void BossCore::SaveOriginalColors() {
    if (colorResetTimer_ <= 0.0f) {
        defaultColor_ = GetColor();
        savedBlockColors_.resize(armorBlocks_.size());
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (armorBlocks_[i]) {
                savedBlockColors_[i] = armorBlocks_[i]->GetColor();
            } else {
                savedBlockColors_[i] = { 1.0f, 1.0f, 1.0f, 1.0f };
            }
        }
    }
}

bool BossCore::OnCollision(Object3d* other) {
    uint32_t attribute = other->GetCollisionAttribute();

    if (attribute & kPlayerAttack) {
        // --- 連続ヒット防止：クールダウン中なら無視する ---
        if (damageCooldownTimer_ > 0.0f) {
            return true;
        }

        CollisionInfo info = CheckCollision(other);
        if (!info.isColliding) {
            return false;
        }

        TakeBodyDamage(10.0f);
        // ★ エフェクトからの被弾を確定させてヒットリストに記録
        if (EffectObject3d* effect = dynamic_cast<EffectObject3d*>(other)) {
            effect->AddHitObject(this);
        }
        damageCooldownTimer_ = 0.5f; // 💥 追加：連続ヒット防止クールダウンを設定！
        return true;
    }

    return BaseEnemy::OnCollision(other);
}

// ==========================================
// ★ 追加：戦闘開始の合図を受け取る！
// ==========================================
void BossCore::StartBattle() {
    if (isBattleStarted_) return; // 既に始まっていたら何もしない

    isBattleStarted_ = true;
    startBattlePos_ = GetTranslate(); // ★ 登場ムービー終了時の初期座標を記憶
    animTimer_ = 0.0f; // ★ ここから2秒後に最初の攻撃をさせるため、タイマーをリセット！

    DebugConsole::GetInstance()->AddLog("[BATTLE START] ボスが行動を開始した！！！");

    // ====================================================
    // ★ 追加：戦闘開始フラグがONになったので、
    // 現在の状態(Idle)を再セットして、即座に属性を「kEnemy」に更新する！
    // ====================================================
    ChangeState(state_);
}

void BossCore::UpdateAppearance(float deltaTime) {
    if (!isAppearing_) return;

    appearanceTimer_ -= deltaTime;

    // ====================================================
    // ★ 追加：フェーズ0（1秒間の待機）
    // ====================================================
    if (appearancePhase_ == 0) {
        if (appearanceTimer_ <= 0.0f) {
            // 1秒の沈黙が終わった！フェーズ1（咆哮）へ移行し、カメラを動かす！
            appearancePhase_ = 1;
            appearanceTimer_ = 2.0f; // 咆哮の2秒間
            DebugConsole::GetInstance()->AddLog("[EVENT] ボス起動！！");

            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                CameraEditor::GetInstance()->PlayOverrideCamera(camera, "a");
            }
        }
        return; // 待機中はここで処理を終わる（ボスは微動だにしない）
    }

    // ====================================================
    // フェーズ1：咆哮とスケール変更
    // ====================================================
    float t = 2.0f - appearanceTimer_;
    Vector3 currentScale = { 1.0f, 1.0f, 1.0f };

    if (t < 0.5f) {
        // ① 息を吸い込む
        float p = t / 0.5f;
        float shrink = std::sin(p * 3.1415f) * 0.2f;
        currentScale = { 1.0f - shrink, 1.0f - shrink, 1.0f - shrink };
    }
    else if (t < 1.8f) {
        // ② 咆哮・ブルブル震える
        float p = (t - 0.5f) / 1.3f;
        float swell = (1.0f - std::pow(p, 2.0f)) * 0.3f;

        float shakeX = std::sin(t * 60.0f) * 0.05f;
        float shakeY = std::cos(t * 65.0f) * 0.05f;
        float shakeZ = std::sin(t * 70.0f) * 0.05f;

        currentScale = { 1.0f + swell + shakeX, 1.0f + swell + shakeY, 1.0f + swell + shakeZ };
        SetColor({ 1.0f, 0.6f, 0.6f, 1.0f });
    }
    else {
        // ③ スッ…と元に戻る
        currentScale = { 1.0f, 1.0f, 1.0f };
        SetColor(greenColor_);
        defaultColor_ = greenColor_;
    }

    SetScale(currentScale);

    if (appearanceTimer_ <= 0.0f) {
        isAppearing_ = false;
        SetScale({ 1.0f, 1.0f, 1.0f });
        SetColor(greenColor_);
        defaultColor_ = greenColor_;

        // ゴーストディレクターのアニメーション（EntranceAnimation.json）を再生
        if (director_) {
            director_->PlayScenario(false, false);
            isWaitingForDirector_ = true; // 追加：アニメーション終了を待つフラグをオンにする！
        }
    }
}
void BossCore::UpgradeToFunnel(Object3d* block) {
    if (!block) return;

    // 既に Shard があるかチェック（二重生成防止）
    for (auto* child : block->GetChildren()) {
        if (child && child->GetName().find("Shard") != std::string::npos) return;
    }

    // --- 元のブロックから見た目の情報をすべて抜き出す ---
    std::string modelName = block->GetModelName();
    Vector4 color = block->GetColor();
    float metallic = block->GetMetallic();
    float roughness = block->GetRoughness();
    float emissive = block->GetEmissive();
    float envIntensity = block->GetEnvIntensity();
    bool enableNormal = block->GetEnableNormalMap();
    bool enableEnv = block->GetEnableEnvMap();
    int32_t matType = block->GetMaterialType();
    std::string texPath = block->GetTexturePath();
    std::string normalPath = block->GetNormalMapPath();
    std::string ormPath = block->GetOrmMapPath();

    // 親ブロック自体は当たり判定や座標の軸として使うため、見た目だけ消す
    block->SetIsVisible(false);

    // 1. 中心にコアを生成（攻撃時の発光体としての役割）
    auto core = std::make_unique<Object3d>();
    core->Initialize(common_);
    core->SetModel("enemy_core");
    core->SetName(block->GetName() + "_Core");
    core->SetParent(block);
    core->SetScale({ 0.5f, 0.5f, 0.5f }); // Shardより少し小さく
    core->SetColor({ 0.0f, 0.7f, 1.0f, 1.0f });
    core->SetMaterialType(2); // 発光マテリアル
    core->SetEmissive(4.0f);
    
    // 2. 8つの Shard（分割パーツ）を生成
    float offset = 0.175f; // スケール0.65fに合わせて外縁がピッタリ1.0（-0.5〜0.5）になるように配置
    Vector3 offsets[8] = {
        {-offset, -offset, -offset}, {offset, -offset, -offset},
        {-offset,  offset, -offset}, {offset,  offset, -offset},
        {-offset, -offset,  offset}, {offset, -offset,  offset},
        {-offset,  offset,  offset}, {offset,  offset,  offset}
    };

    for (int i = 0; i < 8; ++i) {
        auto shard = std::make_unique<Object3d>();
        shard->Initialize(common_);
        shard->SetModel(modelName); // ★ 元のマップブロックと同じモデルを使用！
        shard->SetName(block->GetName() + "_Shard" + std::to_string(i + 1));
        shard->SetParent(block);
        shard->SetTranslate(offsets[i]);
        shard->SetScale({ 0.65f, 0.65f, 0.65f }); // 0.5f から 0.65f に変更（主の当たり判定にピッタリ一致）
        
        // --- 見た目の情報を完璧にコピー ---
        shard->SetColor(color);
        shard->SetMetallic(metallic);
        shard->SetRoughness(roughness);
        shard->SetEmissive(emissive);
        shard->SetEnvIntensity(envIntensity);
        shard->SetEnableNormalMap(enableNormal);
        shard->SetEnableEnvMap(enableEnv);
        shard->SetMaterialType(matType);
        if (!texPath.empty()) shard->SetTexture(texPath);
        if (!normalPath.empty()) shard->SetNormalMap(normalPath);
        if (!ormPath.empty()) shard->SetOrmMap(ormPath);
        
        if (sceneManager_ && sceneManager_->GetCurrentScene()) {
            sceneManager_->GetCurrentScene()->AddObject(std::move(shard));
        }
    }
    
    if (sceneManager_ && sceneManager_->GetCurrentScene()) {
        sceneManager_->GetCurrentScene()->AddObject(std::move(core));
    }
}

void BossCore::LoadAttackParams() {
    std::string filePath = "Resources/json/enemy/boss_attack_params.json";
    if (!std::filesystem::exists(filePath)) {
        // デフォルトの攻撃パターンを初期設定
        attackParams_.phase1Attacks = {
            { 1, 30 }, // 突進 (Rush)
            { 2, 30 }, // 射撃 (Shoot)
            { 3, 30 }, // ハンマー (Hammer)
            { 4, 30 }  // 壁 (Wall)
        };
        attackParams_.phase2Attacks = {
            { 5, 30 }, // 人型 (Humanoid)
            { 6, 30 }, // レーザー (Laser)
            { 7, 30 }, // 吸収 (Absorb)
            { 9, 30 }  // ファンネル (Funnels)
        };
        SaveAttackParams(); // デフォルト値で作成
        return;
    }

    std::ifstream ifs(filePath);
    if (ifs.is_open()) {
        json j;
        ifs >> j;
        attackParams_.FromJson(j);
    }

    // 古いJSONなどで空の場合はデフォルトを流し込む
    if (attackParams_.phase1Attacks.empty()) {
        attackParams_.phase1Attacks = {
            { 1, 30 },
            { 2, 30 },
            { 3, 30 },
            { 4, 30 }
        };
    }
    if (attackParams_.phase2Attacks.empty()) {
        attackParams_.phase2Attacks = {
            { 5, 30 },
            { 6, 30 },
            { 7, 30 },
            { 9, 30 }
        };
    }
}

void BossCore::SaveAttackParams() {
    std::string dirPath = "Resources/json/enemy";
    if (!std::filesystem::exists(dirPath)) {
        std::filesystem::create_directories(dirPath);
    }

    std::string filePath = dirPath + "/boss_attack_params.json";
    std::ofstream ofs(filePath);
    if (ofs.is_open()) {
        json j;
        attackParams_.ToJson(j);
        ofs << j.dump(4);
    }
}

void BossCore::FullyRecoverBarrierAndArmor() {
    barrierHp_ = attackParams_.maxBarrierHp;
    maxBarrierHp_ = attackParams_.maxBarrierHp;
    for (size_t i = 0; i < blockHps_.size(); ++i) {
        blockHps_[i] = attackParams_.maxArmorBlockHp;
        blockBroken_[i] = false;
        if (i < armorBlocks_.size() && armorBlocks_[i]) {
            armorBlocks_[i]->SetIsVisible(true);
        }
    }
}

