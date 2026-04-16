#include "BossCore.h"
#include "InputManager.h"
#include "imgui.h"
#include "easing.h"
#include "DebugConsole.h"
#include <cmath>
#include <numbers>
#include <ctime>
#include <cstdlib>
#include "GPUParticleManager.h"

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
        if (block == newBlock) return true; // 既に同化済み！
    }

    newBlock->SetParent(this);
    newBlock->GetTransform()->isQuaternionMaster = false;
    newBlock->SetIsVisible(true);

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (blockBroken_[i]) {
            armorBlocks_[i] = newBlock;
            blockHps_[i] = 100.0f;
            blockBroken_[i] = false;

            newBlock->SetCollisionAttribute(kEnemyAttack);
            newBlock->SetCollisionMask(kPlayer);
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
// ★ 10個満タンになるまで、あと何個ブロックが必要かを計算する
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

    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    director_ = std::make_unique<GhostDirector>();
    if (sceneManager_) {
        director_->Initialize(sceneManager_);
    }

    originalColor_ = GetColor();

    // --- 1. オーラの追加 ---
    auto BossParticle1 = std::make_unique<GPUParticleEmitter>();
    BossParticle1->Initialize("Boss1", this);
    BossParticle1->Play();
    particleEmitters_.push_back(std::move(BossParticle1)); // 配列に追加！

    // --- 2. 砂埃の追加 ---
    auto BossParticle2 = std::make_unique<GPUParticleEmitter>();
    BossParticle2->Initialize("Boss2", this);
    BossParticle2->Play();
    particleEmitters_.push_back(std::move(BossParticle2)); // 配列に追加！

    // --- 3. 火花の追加 ---
    auto BossParticle3 = std::make_unique<GPUParticleEmitter>();
    BossParticle3->Initialize("Boss3", this);
    BossParticle3->Play();
    //particleEmitters_.push_back(std::move(BossParticle3)); // 配列に追加！
}

void BossCore::Update(float deltaTime) {
    deltaTime *= 1.5f;

    InputManager* input = InputManager::GetInstance();

    

#ifdef USE_IMGUI
    if (SceneManager::GetInstance()->IsPlaying()) {
        // 0キーで時間停止
        if (input->IsKeyTriggered(DIK_0)) {
            s_isTimeStopped_ = !s_isTimeStopped_;
            if (s_isTimeStopped_) {
                DebugConsole::GetInstance()->AddLog("【TIME STOP】 ボスの時間が止まった…！");
            }
            else {
                DebugConsole::GetInstance()->AddLog("【TIME RESUME】 時は動き出す！");
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
        if (input->IsKeyTriggered(DIK_8)) triggerAttack = 8;

        // ==========================================
    // ★ 追加：9キーで即座にボスを爆散させるデバッグ機能！
    // ==========================================
        if (input->IsKeyTriggered(DIK_9)) {
            if (!isCoreBroken_) {
                DebugConsole::GetInstance()->AddLog("【DEBUG】 9キー入力：ボスを強制爆散させます！！！💥");

                param_->hp = 0.0f;  // 念のためステータスもHP0にしておく
                BreakCore();        // 爆散演出を強制発動！
            }
        }

        if (triggerAttack != 0) {
            DebugConsole::GetInstance()->AddLog("【DEBUG】 攻撃 " + std::to_string(triggerAttack) + " を予約！待機に戻ります！");

            s_debugForceAttack = triggerAttack;

            // 強制的に状態をリセットして待機(Idle)に戻す
            if (currentAttack_) currentAttack_.reset(); // 実行中の攻撃を破棄
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

            for (Object3d* block : armorBlocks_) {
                if (!block) continue;
                for (Object3d* child : block->GetChildren()) {
                    if (child->GetName().find("Beam_Cylinder") != std::string::npos) {
                        child->SetScale({ 0.0f, 0.0f, 0.0f });
                        child->SetCollisionAttribute(0);
                    }
                }
            }
        }
    }

    
#endif

    if (s_isTimeStopped_) {
        deltaTime = 0.0f;
    }

    float preTimer = colorResetTimer_;

    BaseEnemy::Update(deltaTime);

    // ==========================================
    // ★ パーティクルの自動追従・更新
    // ==========================================
    for (auto& emitter : particleEmitters_) {
        if (emitter) {
            emitter->Update(deltaTime);
        }
    }

    // バリアへのダメージ処理
    if (target_ && damageCooldownTimer_ <= 0.0f && state_ != State::Weak) {
        Object3d* weapon = FindWeaponRecursive(target_);

        // ==========================================
        // ★ 修正1：武器のマスクが 0 じゃない（＝剣を振っている）時だけ処理する！
        // ==========================================
        if (weapon && weapon->GetCollisionMask() != 0) {

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                Object3d* block = armorBlocks_[i];
                if (!block || blockBroken_[i]) continue;

                // ==========================================
                // ★ 修正2：無理やりマスクを全開放するのではなく、
                // ブロックに一瞬だけ「敵(kEnemy)」の属性を追加する！
                // ==========================================
                uint32_t originalAttr = block->GetCollisionAttribute();
                block->SetCollisionAttribute(originalAttr | kEnemy); // 敵属性を足す！

                // エンジンの正しいルールで当たり判定チェック
                CollisionInfo info = block->CheckCollision(weapon);

                // 判定が終わったら元の属性(kGroundなど)に戻す
                block->SetCollisionAttribute(originalAttr);

                if (info.isColliding) {
                    TakeBarrierDamage(10.0f, block); // 当たったブロックだけ赤くする

                    blockHps_[i] -= 10.0f; // 部位HPを減らす
                    if (blockHps_[i] <= 0.0f) {
                        blockBroken_[i] = true;
                        DebugConsole::GetInstance()->AddLog("【BREAK】 ブロック " + std::to_string(i) + " が破壊された！！💥");
                    }
                    break;
                }
            }
        }
    }

    if (preTimer > 0.0f && colorResetTimer_ <= 0.0f) {
        for (Object3d* block : armorBlocks_) {
            if (block) block->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        }
    }

    if (SceneManager::GetInstance()->IsPlaying()) {
        s_globalIdleTimer += deltaTime;
    }

    UpdateFlyingBlocks(deltaTime);

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
                    // ★ 修正：armorBlocks_ だけではなく、HPやフラグのリストからも確実に消す！
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
        }

        switch (state_) {
        case State::Idle:
            UpdateIdle(deltaTime);
            break;
        case State::Attack:
            // 攻撃クラスがあればそれを更新！終わったらIdleへ！
            if (currentAttack_) {
                currentAttack_->Update(this, deltaTime);
                if (currentAttack_->IsFinished()) {
                    currentAttack_.reset();
                    ChangeState(State::Idle);
                }
            }
            break;
        case State::Weak:
            UpdateWeak(deltaTime);
            break;
        }
    }
    // ==========================================
    // ★ 魔法の処理：破壊されたブロックの強制消去！
    // ==========================================
    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (blockBroken_[i] && armorBlocks_[i]) {
            armorBlocks_[i]->SetScale({ 0.0f, 0.0f, 0.0f });
            armorBlocks_[i]->SetCollisionAttribute(0);
        }
    }

    // ==========================================
    // ★ 追加：破片の物理計算・退場タイマーを進める！
    // これを呼ばないと、破片が飛び散りません。
    // ==========================================
    UpdateCorePieces(deltaTime);
}

// =================================================================
// ステート(状態)管理
// =================================================================
void BossCore::ChangeState(State nextState) {
    state_ = nextState;

    uint32_t coreAttribute;
    uint32_t blockAttribute;
    // ==========================================
    // トドメ待ち状態なら、カメラの邪魔になる kGround を外す！
    // ==========================================
    if (isWaitingForDeath_) {
        coreAttribute = kEnemy; // トドメの攻撃を受けるために敵判定だけ残す
        blockAttribute = 0;     // 装甲ブロックは完全に判定を消す
    }
    else {
        // 今までの通常の処理
        if (state_ == State::Attack) {
            coreAttribute = kEnemyAttack | kGround;
        }
        else if (state_ == State::Weak) {
            coreAttribute = kEnemy | kGround;
        }
        else {
            coreAttribute = kGround;
        }
        blockAttribute = (state_ == State::Attack) ? (kEnemyAttack | kGround) : kGround;
    }

    SetCollisionAttribute(coreAttribute);
    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetCollisionAttribute(coreAttribute);
        }
    }

    if (state_ == State::Attack) {
        SetColor(originalColor_);
    }

    //uint32_t blockAttribute = (state_ == State::Attack) ? (kEnemyAttack | kGround) : kGround;

    for (Object3d* block : armorBlocks_) {
        if (block) {
            block->SetCollisionAttribute(blockAttribute);
            if (state_ == State::Attack) {
                block->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            }
        }
    }

    switch (state_) {
    case State::Idle:
        animTimer_ = 0.0f;
        break;

    case State::Attack: {
        // ==========================================
        // ★ 攻撃の確率（重み）設定
        // 数値を大きくするほど、その攻撃が出やすくなります！
        // ==========================================
        struct AttackWeight {
            int id;      // 攻撃番号
            int weight;  // 出やすさ（重み）
        };

        std::vector<AttackWeight> attackList = {
            { 1, 30 }, // 突進 (30%)
            { 2, 25 }, // 射撃 (25%)
            { 3, 20 }, // ハンマー (20%)
            { 4, 10 }, // 壁 (10%)
            { 5, 10 }, // 人型 (10%)
            { 6, 5  },  // レーザー (5%) ※超大技！
            { 7, 15 },  // 吸収 (重み15)
        };

        static int lastAttack = 0; // 前回撃った攻撃を記憶
        int totalWeight = 0;
        std::vector<AttackWeight> candidates;

        // 前回と同じ攻撃を除外しながら、有効な攻撃の合計重みを計算
        for (const auto& a : attackList) {
            if (a.id != lastAttack) {
                candidates.push_back(a);
                totalWeight += a.weight;
            }
        }

        // 重みに基づいた抽選
        int nextAttack = 1; // デフォルト
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

        lastAttack = nextAttack; // 記憶更新

        // デバッグ用強制上書き
        if (s_debugForceAttack != 0) {
            nextAttack = s_debugForceAttack;
            s_debugForceAttack = 0;
        }

        if (isFinalPhase_) {
            nextAttack = 8;
        }

        animTimer_ = 0.0f;

        // 攻撃インスタンスの生成
        if (nextAttack == 1)      currentAttack_ = std::make_unique<BossAttack1_Rush>();
        else if (nextAttack == 2) currentAttack_ = std::make_unique<BossAttack2_Shoot>();
        else if (nextAttack == 3) currentAttack_ = std::make_unique<BossAttack3_Hammer>();
        else if (nextAttack == 4) currentAttack_ = std::make_unique<BossAttack4_Wall>();
        else if (nextAttack == 5) currentAttack_ = std::make_unique<BossAttack5_Humanoid>();
        else if (nextAttack == 6) currentAttack_ = std::make_unique<BossAttack6_Laser>();
        else if (nextAttack == 7) currentAttack_ = std::make_unique<BossAttack7_Absorb>();
        else if (nextAttack == 8) currentAttack_ = std::make_unique<BossAttack8_Final>();

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

void BossCore::TakeBodyDamage(float damage) {
    // すでに割れて退場中なら、これ以上ダメージ計算しない
    if (isCoreBroken_) return;

    // ダメージを与える
    param_->hp -= damage;

    // HPが0以下になったら爆散演出スタート！
    if (param_->hp <= 0.0f) {
        param_->hp = 0.0f; // HPを0に固定

        DebugConsole::GetInstance()->AddLog("【撃破】 ボスのHPが0になった！粉砕！！");

        // ★ ここで割れる演出の関数を呼ぶ！
        BreakCore();
    }
}

// =================================================================
// 各ステートの個別更新処理
// =================================================================

void BossCore::UpdateIdle(float deltaTime) {
    if (isWaitingForDeath_) {
        SetColor({ 0.5f, 0.5f, 0.5f, 1.0f }); // ボロボロの色にする

        // ブロックも地面に落として機能停止させる
        for (Object3d* block : armorBlocks_) {
            if (block) {
                Vector3 pos = block->GetTranslate();
                if (pos.y > 0.0f) pos.y -= 20.0f * deltaTime; // 地面に落ちる
                block->SetTranslate(pos);
            }
        }
        return; // これ以上何もしない（攻撃にも移行しない）
    }

    // ★ 待機中は常にブロックをランダムスケールの周回軌道に乗せる！
    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        OrbitData orbit = GetIdleOrbit(i);
        armorBlocks_[i]->SetTranslate(orbit.pos);
        armorBlocks_[i]->SetScale(orbit.scale);
        armorBlocks_[i]->SetRotation(orbit.rot);
        armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
    }

    animTimer_ += deltaTime;

    // 2.0秒待機したら攻撃へ
    if (animTimer_ >= 2.0f) {
        ChangeState(State::Attack);
    }
}

void BossCore::UpdateWeak(float deltaTime) {
    animTimer_ += deltaTime;

    float shakeX = std::sin(animTimer_ * 50.0f) * 0.05f;
    float shakeZ = std::cos(animTimer_ * 45.0f) * 0.05f;
    SetRotation({ shakeX, GetRotation().y, shakeZ });

    SetColor({ 0.3f, 0.3f, 0.3f, 1.0f });

    float scatterDuration = 0.8f;
    float t = std::min(animTimer_ / scatterDuration, 1.0f);
    float easeT = Easing::OutExpo(t);

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
            Vector3 currentPos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
            armorBlocks_[i]->SetTranslate(currentPos);

            Vector3 rot = armorBlocks_[i]->GetRotation();
            rot.x += 1.0f * deltaTime;
            rot.y += 1.5f * deltaTime;
            armorBlocks_[i]->SetRotation(rot);
            armorBlocks_[i]->SetColor({ 0.3f, 0.3f, 0.3f, 1.0f });
        }
    }

    if (animTimer_ >= 10.0f) {
        animTimer_ = 0.0f;
        SetRotation({ 0.0f, GetRotation().y, 0.0f });
        SetColor(originalColor_);
        ChangeState(State::Idle);
    }
}

void BossCore::UpdateFlyingBlocks(float deltaTime) {
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

                if (target_) {
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
                currentPos.x += dir.x * gatherSpeed * deltaTime;
                currentPos.y += dir.y * gatherSpeed * deltaTime;
                currentPos.z += dir.z * gatherSpeed * deltaTime;
                fb.block->SetTranslate(currentPos);

                fb.currentRot.x += 15.0f * deltaTime;
                fb.currentRot.y += 30.0f * deltaTime;
                fb.block->SetRotation(fb.currentRot);
            }
            fb.block->GetTransform()->isQuaternionMaster = false;
        }
        else if (fb.mode == 0) {
            Vector3 pos = fb.block->GetTranslate();
            pos.x += fb.velocity.x * deltaTime;
            pos.y += fb.velocity.y * deltaTime;
            pos.z += fb.velocity.z * deltaTime;

            if (pos.y <= 0.0f) {
                pos.y = 0.0f;
                fb.velocity = { 0.0f, 0.0f, 0.0f };
                fb.mode = 1;
            }
            fb.block->SetTranslate(pos);

            Vector3 spinSpeed = { 30.0f, 45.0f, 60.0f };
            fb.currentRot.x += spinSpeed.x * deltaTime;
            fb.currentRot.y += spinSpeed.y * deltaTime;
            fb.currentRot.z += spinSpeed.z * deltaTime;
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
                blockPos.x += dir.x * returnSpeed * deltaTime;
                blockPos.y += dir.y * returnSpeed * deltaTime;
                blockPos.z += dir.z * returnSpeed * deltaTime;
                fb.block->SetTranslate(blockPos);

                Vector3 spinSpeed = { 60.0f, 60.0f, 60.0f };
                fb.currentRot.x += spinSpeed.x * deltaTime;
                fb.currentRot.y += spinSpeed.y * deltaTime;
                fb.currentRot.z += spinSpeed.z * deltaTime;
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
    barrierHp_ -= damage;

    DebugConsole::GetInstance()->AddLog("【HIT!】 Barrier Damaged! 残りHP: " + std::to_string(barrierHp_) + " / " + std::to_string(maxBarrierHp_));

    damageCooldownTimer_ = 0.5f;
    colorResetTimer_ = 0.15f;

    // ==========================================
    // 全部ではなく、当たったブロックだけを赤くする！
    // ==========================================
    if (hitBlock) {
        hitBlock->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
    }

    if (barrierHp_ <= 0.0f) {
        DebugConsole::GetInstance()->AddLog("★☆ Barrier BROKEN! ☆★");
        barrierHp_ = maxBarrierHp_;

        if (currentAttack_) currentAttack_.reset(); // ★ ダウン時は攻撃を強制終了！
        animTimer_ = 0.0f;
        flyingBlocks_.clear();

        blockStartPos_.clear();
        blockTargetPos_.clear();

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            Object3d* block = armorBlocks_[i];
            if (block) {
                block->SetParent(this);
                blockStartPos_.push_back(block->GetTranslate());

                float angle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>;
                float distance = 10.0f + (static_cast<float>(rand()) / RAND_MAX) * 5.0f;
                float height = ((static_cast<float>(rand()) / RAND_MAX) * 10.0f) - 5.0f;

                Vector3 scatterPos = {
                    std::cos(angle) * distance,
                    height,
                    std::sin(angle) * distance
                };
                blockTargetPos_.push_back(scatterPos);
            }
        }

        SetRotation({ 0.0f, GetRotation().y, 0.0f });
        ChangeState(State::Weak);
    }
}

void BossCore::BreakCore() {
    isCoreBroken_ = true;
    deathTimer_ = 0.0f;

    DebugConsole::GetInstance()->AddLog("【撃破】 コアが粉砕された！！！🎉");

    this->SetScale({ 0.0f, 0.0f, 0.0f });
    this->SetCollisionAttribute(0);

    // ボスの現在の座標を取得
    Vector3 corePos = this->GetTranslate();

    // 現在のシーンを取得（ゲーム中なら必ず成功する）
    BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene();
    if (!currentScene) return;

    // ==========================================
    // ★ ここで初めて破片を18個生成し、シーンにぶん投げる！
    // ==========================================
    for (int i = 0; i < 18; ++i) {
        auto pieceObj = std::make_unique<Object3d>();

        pieceObj->Initialize(common_);
        pieceObj->SetStatic(true);
        pieceObj->SetModel("enemy_core_shards/enemy_core" + std::to_string(i + 1));

        pieceObj->SetScale({ 1.0f, 1.0f, 1.0f });
        pieceObj->SetTranslate(corePos);
        pieceObj->SetCollisionAttribute(0);

        CorePiece piece;
        piece.obj = pieceObj.get();

        // ランダムな方向に吹き飛ぶ計算
        float rx = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;
        float ry = ((static_cast<float>(rand()) / RAND_MAX) * 1.0f) + 0.5f;
        float rz = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;

        float speed = 30.0f + (rand() % 30);
        piece.velocity = { rx * speed, ry * speed, rz * speed };
        piece.rotSpeed = {
            (rand() % 60) - 30.0f,
            (rand() % 60) - 30.0f,
            (rand() % 60) - 30.0f
        };

        corePieces_.push_back(piece);

        // シーンのリストに実体を渡す！（AllDrawに乗る）
        currentScene->GetObjects().push_back(std::move(pieceObj));
    }
}

void BossCore::UpdateCorePieces(float deltaTime) {
    if (!isCoreBroken_) return;

    deathTimer_ += deltaTime;
    if (deathTimer_ > 5.0f) {
        // ★ ボスが消える時、シーンにある破片にも「死」を伝達して自動削除させる
        for (auto& piece : corePieces_) {
            if (piece.obj) {
                piece.obj->isDead = true;
            }
        }
        isDead = true; // ボス自身も消滅
        return;
    }

    // 物理演算
    for (auto& piece : corePieces_) {
        // ★ 安全対策：ポインタが生きている時だけ動かす
        if (piece.obj) {
            Vector3 pos = piece.obj->GetTranslate();
            piece.velocity.y -= 98.0f * deltaTime;

            pos.x += piece.velocity.x * deltaTime;
            pos.y += piece.velocity.y * deltaTime;
            pos.z += piece.velocity.z * deltaTime;

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
            rot.x += piece.rotSpeed.x * deltaTime;
            rot.y += piece.rotSpeed.y * deltaTime;
            rot.z += piece.rotSpeed.z * deltaTime;
            piece.obj->SetRotation(rot);

            piece.obj->UpdateWorldMatrix();
        }
    }
}
bool BossCore::OnCollision(Object3d* other) {
    uint32_t attribute = other->GetCollisionAttribute();

    if (attribute & kPlayerAttack) {
        CollisionInfo info = CheckCollision(other);
        if (!info.isColliding) {
            return false;
        }

        TakeBodyDamage(10.0f);
        return BaseEnemy::OnCollision(other);
    }

    return BaseEnemy::OnCollision(other);
}