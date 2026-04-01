#include "BossCore.h"
#include "InputManager.h"
#include "imgui.h"
#include "easing.h"
#include "DebugConsole.h"
#include <cmath>
#include <numbers>
#include <ctime>
#include <cstdlib>

// ==========================================
// 攻撃クラスを読み込む
// ==========================================
#include "BossAttack/BossAttack1_Rush.h"
#include "BossAttack/BossAttack2_Shoot.h"
#include "BossAttack/BossAttack3_Hammer.h"
#include "BossAttack/BossAttack4_Wall.h"
#include "BossAttack/BossAttack5_Humanoid.h"

// =================================================================
// ★ 新規：待機アニメーション用のタイマーと軌道計算関数
// =================================================================
namespace {
    float s_globalIdleTimer = 0.0f; // 待機アニメーション用のタイマー

    int s_debugForceAttack = 0;

    Object3d* FindWeaponRecursive(Object3d* node) {
        if (!node) return nullptr;
        // 自分が kPlayerAttack(凶器) なら見つけた！
        if (node->GetCollisionAttribute() & kPlayerAttack) {
            return node;
        }
        // 見つからなければ子パーツの中を再帰的に探す
        for (Object3d* child : node->GetChildren()) {
            Object3d* result = FindWeaponRecursive(child);
            if (result) return result;
        }
        return nullptr;
    }
    float EaseOutElasticMario(float t) {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;

        // バウンドの周期と強度
        float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;

        // 減衰するサイン波： std::pow(2.0f, -10.0f * t) で徐々に振動を小さくする
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }
}

BossCore::OrbitData BossCore::GetIdleOrbit(size_t index) {
    BossCore::OrbitData data;

    static std::vector<Vector3> randomScales;
    if (randomScales.empty()) {
        for (int i = 0; i < 6; ++i) {
            float randomVal = 0.5f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f;
            randomScales.push_back({ randomVal, randomVal, randomVal });
        }
    }

    Vector3 basePos;
    switch (index % 6) {
    case 0: basePos = { 2.0f,  2.0f,  0.5f }; break;
    case 1: basePos = { -1.5f, -1.5f,  1.8f }; break;
    case 2: basePos = { 0.6f,  2.5f, -1.8f }; break;
    case 3: basePos = { -2.0f,  0.5f, -1.2f }; break;
    case 4: basePos = { 1.5f, -2.0f,  1.2f }; break;
    case 5: basePos = { -0.8f, -0.8f, -2.0f }; break;
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
    data.scale = randomScales[index % 6];

    return data;
}

// =================================================================
// 初期化・更新
// =================================================================

void BossCore::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 親クラス(BaseEnemy)の初期化
    BaseEnemy::Initialize(common, modelName);

    // ==========================================
    // ★ 新規：乱数の「種（シード）」を現在時刻で設定！
    // これを呼ぶことで、毎回違う行動パターンになります！
    // ==========================================
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // 演出・攻撃パターン管理用ディレクターの生成
    director_ = std::make_unique<GhostDirector>();
    if (sceneManager_) {
        director_->Initialize(sceneManager_);
    }

    originalColor_ = GetColor();
}

void BossCore::Update(float deltaTime) {
    // ==========================================
    // ★ 魔法の1行：ボスの全体スピード倍率！
    // 以前の「2回Update」と同じ速度を再現するため、ボスの時間だけを2倍速で進める！
    // ==========================================
    deltaTime *= 1.5f;

    // ==========================================
    // 0キーで時間停止（ザ・ワールド）機能！
    // ==========================================
    InputManager* input = InputManager::GetInstance();

    // ==========================================
    // タイムストップ機能を「0キー(DIK_0)」にお引っ越し！
    // ==========================================
    if (input->IsKeyTriggered(DIK_0)) {
        s_isTimeStopped_ = !s_isTimeStopped_; // 押すたびに切り替え
        if (s_isTimeStopped_) {
            DebugConsole::GetInstance()->AddLog("【TIME STOP】 ボスの時間が止まった…！");
        }
        else {
            DebugConsole::GetInstance()->AddLog("【TIME RESUME】 時は動き出す！");
        }
    }

#ifdef USE_IMGUI
    if (SceneManager::GetInstance()->IsPlaying()) {
        int triggerAttack = 0;
        if (input->IsKeyTriggered(DIK_1)) triggerAttack = 1;
        if (input->IsKeyTriggered(DIK_2)) triggerAttack = 2;
        if (input->IsKeyTriggered(DIK_3)) triggerAttack = 3;
        if (input->IsKeyTriggered(DIK_4)) triggerAttack = 4;
        if (input->IsKeyTriggered(DIK_5)) triggerAttack = 5;
        if (input->IsKeyTriggered(DIK_6)) triggerAttack = 6;

        if (triggerAttack != 0) {
            DebugConsole::GetInstance()->AddLog("【DEBUG】 攻撃 " + std::to_string(triggerAttack) + " を予約！待機に戻ります！");

            s_debugForceAttack = triggerAttack; // 次の攻撃を予約

            // 強制的に状態をリセットして待機(Idle)に戻す
            ChangeState(State::Idle);
            animPhase_ = 0;
            attackMode_ = 0;
            animTimer_ = 0.0f; // ★ これにより、きっちり2秒間待機モーションを見せてから次の攻撃に移ります

            // ボス本体の位置と回転を中央に強制リセット
            SetTranslate({ 0.0f, 4.0f, 0.0f });
            SetRotation({ 0.0f, 0.0f, 0.0f });
            SetColor(originalColor_);

            // 飛んでいるブロックを回収し、親子関係を強制修復
            flyingBlocks_.clear();
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (armorBlocks_[i]) {
                    armorBlocks_[i]->SetParent(this);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }

            // ワーニングエリアを確実に隠す
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

    // ★ 魔法の処理：時間停止中は、このフレームの経過時間を「0秒」に偽装する！
    if (s_isTimeStopped_) {
        deltaTime = 0.0f;
    }

    // ------------------------------------------
    // ここから下は今までの Update と全く同じです
    // ------------------------------------------
    float preTimer = colorResetTimer_;

    // 1. 基本更新（行列計算など）
    BaseEnemy::Update(deltaTime);

    if (target_ && damageCooldownTimer_ <= 0.0f && state_ != State::Weak) {
        // ① 腕の先などにある剣を確実に見つける！
        Object3d* weapon = FindWeaponRecursive(target_);

        // ② 振られている剣を見つけたら、ブロックとの当たり判定をチェック！
        if (weapon) {
            for (Object3d* block : armorBlocks_) {
                if (!block) continue;

                // 待機中のブロックは「壁(kGround)」になっているので、一瞬だけ判定を全開放
                uint32_t originalMask = block->GetCollisionMask();
                block->SetCollisionMask(0xFFFFFFFF);

                CollisionInfo info = block->CheckCollision(weapon);

                // 判定が終わったらすぐに元のマスクに戻す
                block->SetCollisionMask(originalMask);

                if (info.isColliding) {
                    TakeBarrierDamage(10.0f); // バリアに10ダメージ！
                    break;
                }
            }
        }
    }

    // ブロックの色を元に戻す処理
    if (preTimer > 0.0f && colorResetTimer_ <= 0.0f) {
        for (Object3d* block : armorBlocks_) {
            if (block) block->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        }
    }

    // ゲーム再生中は待機タイマーを常に進める
    if (SceneManager::GetInstance()->IsPlaying()) {
        s_globalIdleTimer += deltaTime; // ★ deltaTimeが0ならタイマーも止まる！
    }

    // 飛んでいるブロックの更新
    UpdateFlyingBlocks(deltaTime); // ★ 弾も空中でピタッと止まる！

    // ==========================================
    // 新しい攻撃クラスがあれば、そっちを優先して更新する！
    // ==========================================
    if (currentAttack_) {
        currentAttack_->Update(this, deltaTime);

        // 攻撃が終わったよ！と返ってきたら、待機に戻す
        if (currentAttack_->IsFinished()) {
            currentAttack_.reset(); // クラスを破棄
            ChangeState(State::Idle);
        }
        return; // ★ 古い処理に行かないようにここで強制終了！
    }

    // アニメーションシーケンスを優先実行
    UpdateAnimationSequence(deltaTime); // ★ アニメーションも現在位置で完全フリーズ！

    if (animPhase_ != 0 && animPhase_ != 4) {
        return;
    }

    // ==========================================
     // 4. 通常のステート更新（アニメーション中以外に動く）
     // ==========================================
    if (SceneManager::GetInstance()->IsPlaying()) {
        if (isFirstFrame_) {
            originalColor_ = GetColor();
            // ① まず、子供たちの中から WarningArea を見つけ出す！
            for (Object3d* child : GetChildren()) {
                if (child->GetName() == "WarningArea") {
                    warningArea_ = child;
                    warningArea_->SetParent(nullptr); // 親子関係を解除
                    warningArea_->SetScale({ 0.0f, 0.0f, 0.0f }); // 最初は見えないようにする

                    // ==========================================
                    // WarningArea の当たり判定を完全に処刑する（幽霊化）！！
                    // ==========================================
                    warningArea_->SetCollisionAttribute(0); // 自分の属性を「無し(0)」にする！
                    warningArea_->SetCollisionMask(0);      // ぶつかる相手を「無し(0)」にする！

                    // ==========================================
                    // 絶対に斜めにならないよう、角度を完全に平ら(0,0,0)に強制リセット！
                    // ==========================================
                    warningArea_->SetRotation({ 0.0f, 0.0f, 0.0f });
                    warningArea_->GetTransform()->isQuaternionMaster = false;

                    // ==========================================
                    // ② 見つけたら、装甲ブロックのリスト（armorBlocks_）から完全に追放（はく奪）する！
                    // ==========================================
                    for (auto it = armorBlocks_.begin(); it != armorBlocks_.end(); ) {
                        if (*it == warningArea_) {
                            it = armorBlocks_.erase(it); // リストから消去！
                        }
                        else {
                            ++it;
                        }
                    }

                    DebugConsole::GetInstance()->AddLog("🟢 WarningArea を取得し、リストからはく奪しました！");
                    break;
                }
            }

            // ==========================================
            // ゲームが始まった瞬間に、エディターのScale設定に関わらず確実にビームを消しておく！
            // ==========================================
            for (Object3d* block : armorBlocks_) {
                if (!block) continue;
                for (Object3d* child : block->GetChildren()) {
                    if (child->GetName().find("Beam_Cylinder") != std::string::npos) {
                        child->SetScale({ 0.0f, 0.0f, 0.0f });
                        child->SetCollisionAttribute(0);
                    }
                }
            }

            // ③ リストから「はく奪」した【後】に、状態をIdleに切り替える！
            // （これでWarningAreaは ChangeState の属性上書きに巻き込まれません！）
            ChangeState(State::Idle);
            isFirstFrame_ = false;
        }

        switch (state_) {
        case State::Idle:   UpdateIdle(deltaTime);   break;
        case State::Attack: UpdateAttack(deltaTime); break;
        case State::Weak:   UpdateWeak(deltaTime);   break;
        }
    }
}

// =================================================================
// ステート(状態)管理
// =================================================================
void BossCore::ChangeState(State nextState) {
    state_ = nextState;

    // =======================================================
    // ★ 修正：コア本体とブロックの属性設定！
    // =======================================================
    uint32_t coreAttribute;
    if (state_ == State::Attack) {
        // 攻撃中：触れるとダメージ ＋ 絶対すり抜けない壁
        coreAttribute = kEnemyAttack | kGround;
    }
    else if (state_ == State::Weak) {
        // ダウン中：剣で殴れる ＋ 絶対すり抜けない壁
        coreAttribute = kEnemy | kGround;
    }
    else {
        // 待機中：ただの壁（体当たりしてもノーダメージ）
        coreAttribute = kGround;
    }

    // ① コア本体の属性を設定し、さらに「全ての子パーツ」にも属性を同期させる！
    // これにより、コアの見た目メッシュなどの子パーツに触れてもダメージを受けなくなります。
    SetCollisionAttribute(coreAttribute);
    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetCollisionAttribute(coreAttribute);
        }
    }

    if (state_ == State::Attack) {
        SetColor(originalColor_);
    }

    // ② 周りのブロックの属性（攻撃中は「敵の攻撃＋壁」、それ以外は「ただの壁」）
    // 装甲ブロックは上で子パーツとしても処理されますが、念のためここで確実に上書きします。
    uint32_t blockAttribute = (state_ == State::Attack) ? (kEnemyAttack | kGround) : kGround;

    for (Object3d* block : armorBlocks_) {
        if (block) {
            block->SetCollisionAttribute(blockAttribute);
            if (state_ == State::Attack) {
                block->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            }
        }
    }

    // ==========================================
    // 状態移行に合わせて、攻撃アニメーションをスタート！
    // ==========================================
    switch (state_) {
    case State::Idle:
        // 待機状態に入った瞬間にタイマーをリセット（ここから2秒数え始めます）
        animTimer_ = 0.0f;
        break;

    case State::Attack: {
        // 順番に攻撃する。
        static int nextAttackPattern = 0;
        int nextAttack = nextAttackPattern;
        nextAttackPattern++;
        if (nextAttackPattern > 6) {
            nextAttackPattern = 1; // 4番の次は1番に戻る
        }
        //nextAttackPattern = 6;

        // ==========================================
        // ★ 新規追加：デバッグ強制予約があれば、順番を無視して上書き！
        // ==========================================
        if (s_debugForceAttack != 0) {
            nextAttack = s_debugForceAttack;
            s_debugForceAttack = 0; // 一度使ったらリセットして通常の順番に戻す
        }

        // 選ばれた攻撃モードをセットし、対応するPhaseからアニメーション開始！
        attackMode_ = nextAttack;
        animTimer_ = 0.0f;
        shotCount_ = 0; // コンボや射撃カウントもリセット

        if (attackMode_ == 1) {
            currentAttack_ = std::make_unique<BossAttack1_Rush>();
            currentAttack_->Initialize(this); // クラスの初期化を呼ぶ
        }
        else if (attackMode_ == 2) {
            currentAttack_ = std::make_unique<BossAttack2_Shoot>();
            currentAttack_->Initialize(this);
        }
        else if (attackMode_ == 3) {
            currentAttack_ = std::make_unique<BossAttack3_Hammer>();
            currentAttack_->Initialize(this);
        }
        else if (attackMode_ == 4) {
            currentAttack_ = std::make_unique<BossAttack4_Wall>();
            currentAttack_->Initialize(this);
        }
        else if (attackMode_ == 5) {
            currentAttack_ = std::make_unique<BossAttack5_Humanoid>();
            currentAttack_->Initialize(this);
        }
        else if (attackMode_ == 6) {
            animPhase_ = 60; 
        }
        break;
    }

    case State::Weak:
        animTimer_ = 0.0f;
        break;
    }
}
// =================================================================
// 各ステートの個別更新処理
// =================================================================

void BossCore::UpdateIdle(float deltaTime) {
    // ==========================================
    // ★ 修正：一定時間待機したら、攻撃ステートへ自動で移行！
    // ==========================================
    animTimer_ += deltaTime;

    // 例：2.0秒待機したら攻撃へ（タイクラーさんのお好みで秒数は調整してください！）
    if (animTimer_ >= 2.0f) {
        ChangeState(State::Attack);
    }
}

void BossCore::UpdateAttack(float deltaTime) {
    // ==========================================
    // ★ 修正：アニメーションの終了を監視する！
    // UpdateAnimationSequence() の中で攻撃が完了すると attackMode_ が 0 に戻るのを利用。
    // ==========================================

    // 攻撃が完全に終了して待機状態(0)に戻ったら、Idleステートへ移行！
    if (attackMode_ == 0) {
        ChangeState(State::Idle);
    }
}

void BossCore::UpdateWeak(float deltaTime) {
    animTimer_ += deltaTime;

    // --- 演出1：ボス本体のシェイク ---
    float shakeX = std::sin(animTimer_ * 50.0f) * 0.05f;
    float shakeZ = std::cos(animTimer_ * 45.0f) * 0.05f;
    SetRotation({ shakeX, GetRotation().y, shakeZ });

    // --- 演出2：全体の色を暗くする ---
    SetColor({ 0.3f, 0.3f, 0.3f, 1.0f });

    // ==========================================
    // ★ 追加：ブロックをバラバラに弾け飛ばすアニメーション！
    // ==========================================
    float scatterDuration = 0.8f; // 0.8秒かけて弾け飛ぶ
    float t = std::min(animTimer_ / scatterDuration, 1.0f);
    float easeT = Easing::OutExpo(t); // 「バッ！」と広がる動き

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
            // スタート位置からランダムな目標位置へ補間
            Vector3 currentPos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
            armorBlocks_[i]->SetTranslate(currentPos);

            // スタン中っぽく、各ブロックを適当な方向にゆっくり回転させ続ける
            Vector3 rot = armorBlocks_[i]->GetRotation();
            rot.x += 1.0f * deltaTime;
            rot.y += 1.5f * deltaTime;
            armorBlocks_[i]->SetRotation(rot);
            armorBlocks_[i]->SetColor({ 0.3f, 0.3f, 0.3f, 1.0f }); // ブロックも暗くする
        }
    }

    // --- 3秒経過で復帰 ---
    if (animTimer_ >= 10.0f) {
        animTimer_ = 0.0f;
        SetRotation({ 0.0f, GetRotation().y, 0.0f });
        SetColor(originalColor_);
        ChangeState(State::Idle); // ここで各ブロックは IdleOrbit に戻るようになります
    }
}

void BossCore::UpdateAnimationSequence(float deltaTime) {

    // ==========================================
    // ゲームが再生中(Play)でなければ、この先のアニメーション・入力処理を一切行わない！
    // ==========================================
    if (!SceneManager::GetInstance()->IsPlaying()) {
        return; // 再生中でなければ操作を受け付けない
    }
    if (state_ == State::Weak) {
        return;
    }

    // ======================================
    // フェーズ0: 待機（Idleステート中）
    // ======================================
    if (animPhase_ == 0) {
        // 待機中は常にブロックをランダムスケールの周回軌道に乗せる！
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            OrbitData orbit = GetIdleOrbit(i);
            armorBlocks_[i]->SetTranslate(orbit.pos);
            armorBlocks_[i]->SetScale(orbit.scale);
            armorBlocks_[i]->SetRotation(orbit.rot);
            armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
        }

        // ★ 自動ループ化に伴い、キー入力は全削除！
        // 攻撃のトリガーは ChangeState(State::Attack) が自動で行います。
        return;
    }

    
    
    // ======================================
    // 攻撃モード6：中央移動 → 回転タメ → 回転変形 → 0.5秒停止 → レーザー！ (Phase 60 ~ 65)
    // ======================================
    else if (attackMode_ == 6) {

        // ==========================================
        // ★ 回転スピードの調整用変数
        // ==========================================
        float maxSpinSpeed = 8.0f;  // タメ・変形中の大回転トップスピード！
        float fireSpinSpeed = 0.3f; // レーザー発射中の少し落ち着いた回転スピード

        // --- Phase 60: まずはコアが中央(0,0)へスゥーッと移動する ---
        if (animPhase_ == 60) {
            if (animTimer_ == 0.0f) {
                animStartPos_ = GetTranslate(); // コアのスタート位置
            }

            animTimer_ += deltaTime;
            float duration = 2.5f; // 実時間で約1.6秒
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::InOutSine(t);

            // コア本体をステージ中央 (X=0, Y=4.0, Z=0) へ移動
            Vector3 corePos = GetTranslate();
            corePos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
            corePos.y = Math::Lerp(animStartPos_.y, 2.0f, easeT);
            corePos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
            SetTranslate(corePos);

            // 移動中、ブロックたちは普段の「フワフワ待機軌道」のまま追従
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                OrbitData orbit = GetIdleOrbit(i);
                armorBlocks_[i]->SetTranslate(orbit.pos);
                armorBlocks_[i]->SetScale(orbit.scale);
                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }

            if (t >= 1.0f) {
                animPhase_ = 61;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 61: 待機軌道のまま、コアがギュイィィンと回転し始める（回転だけの時間） ---
        else if (animPhase_ == 61) {
            if (animTimer_ == 0.0f) {
                // この瞬間のフワフワ軌道を「固定」してコアの子パーツにする！
                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    OrbitData orbit = GetIdleOrbit(i);
                    armorBlocks_[i]->SetParent(this);
                    armorBlocks_[i]->SetTranslate(orbit.pos);
                    armorBlocks_[i]->SetScale(orbit.scale);
                    armorBlocks_[i]->SetRotation(orbit.rot);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }

            animTimer_ += deltaTime;
            // 実時間で1.5秒間、回転だけのタメを作る (1.5 * 1.5 = 2.25f)
            float duration = 2.25f;
            float t = std::min(animTimer_ / duration, 1.0f);

            // コアの回転スピードを 0 から maxSpinSpeed(5.0f) まで徐々に上げる！（加速演出）
            float currentSpinSpeed = Math::Lerp(0.0f, maxSpinSpeed, Easing::InSine(t));

            Vector3 rot = GetRotation();
            rot.y += currentSpinSpeed * deltaTime;
            SetRotation(rot);
            GetTransform()->isQuaternionMaster = false;

            if (t >= 1.0f) {
                animPhase_ = 62;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 62: 大回転を維持したまま、遠心力で開くように砲台陣形へ変形！ ---
        else if (animPhase_ == 62) {
            if (animTimer_ == 0.0f) {
                blockStartPos_.clear();
                blockTargetPos_.clear();
                blockStartScale_.clear();
                blockTargetScale_.clear();
                attentionStartRot_.clear(); // ヘッダにある回転記憶用の変数を流用

                float radius = 12.0f; // 陣形の半径

                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                    blockStartScale_.push_back(armorBlocks_[i]->GetScale());
                    attentionStartRot_.push_back(armorBlocks_[i]->GetRotation()); // 現在の回転を記憶

                    float angle = (i * 2.0f * std::numbers::pi_v<float>) / armorBlocks_.size();
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
            // 変形は実時間で2.0秒 (2.0 * 1.5 = 3.0f)
            float duration = 3.0f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::InOutSine(t);

            // コアは大回転（トップスピード 5.0f）をそのまま継続！
            Vector3 rot = GetRotation();
            rot.y += maxSpinSpeed * deltaTime;
            SetRotation(rot);
            GetTransform()->isQuaternionMaster = false;

            // ブロックの座標・スケール・角度を砲台の定位置へ補間
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                    Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                    armorBlocks_[i]->SetTranslate(pos);

                    Vector3 scale = Math::Lerp(blockStartScale_[i], blockTargetScale_[i], easeT);
                    armorBlocks_[i]->SetScale(scale);

                    // 角度も、待機軌道のナナメ向きから「外側を向く角度」へ補間
                    float angle = (i * 2.0f * std::numbers::pi_v<float>) / armorBlocks_.size();
                    Vector3 targetRot = { 0.0f, -angle, 0.0f };

                    Vector3 currentRot;
                    currentRot.x = Math::Lerp(attentionStartRot_[i].x, targetRot.x, easeT);
                    currentRot.y = Math::Lerp(attentionStartRot_[i].y, targetRot.y, easeT);
                    currentRot.z = Math::Lerp(attentionStartRot_[i].z, targetRot.z, easeT);

                    armorBlocks_[i]->SetRotation(currentRot);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }

            // 展開完了したら、完全停止フェーズへ！
            if (t >= 1.0f) {
                animPhase_ = 63;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 63: 陣形完了後、0.5秒間完全に沈黙する！（嵐の前の静けさ ＋ 予兆レーザー） ---
        else if (animPhase_ == 63) {
            if (animTimer_ == 0.0f) {
                for (Object3d* block : armorBlocks_) {
                    if (!block) continue;
                    for (Object3d* child : block->GetChildren()) {
                        if (child->GetName().find("Beam_Cylinder") != std::string::npos) {

                            // ==========================================
                            // ★ 修正：Y軸を長さ(80.0f)にして、XZを極細(0.1f)にする！
                            // ==========================================
                            child->SetScale({ 0.1f, 80.0f, 0.1f });

                            // ==========================================
                            // ★ 修正：円柱をX軸に90度（pi/2）倒して、正面に向ける！
                            // ==========================================
                            float rotX90 = std::numbers::pi_v<float> / 2.0f;
                            child->SetRotation({ rotX90, 0.0f, 0.0f });
                            child->GetTransform()->isQuaternionMaster = false;

                            child->SetColor({ 1.0f, 0.0f, 0.0f, 0.5f }); // 半透明の赤
                            child->SetCollisionAttribute(0); // 当たり判定なし
                        }
                    }
                }
            }

            animTimer_ += deltaTime;

            // ★ 修正：実時間で 0.5秒間ストップ (0.5 * 1.5 = 0.75f) に直しました！
            float stopDuration = 0.75f;

            if (animTimer_ >= stopDuration) {
                animPhase_ = 64;
                animTimer_ = 0.0f;
            }
            }
            // --- Phase 64: 陣形を維持したまま回転し、ビームを撃つ！ ---
        else if (animPhase_ == 64) {
                animTimer_ += deltaTime;

                // 実時間で5.0秒間レーザーを撃ち続ける (5.0 * 1.5 = 7.5f)
                float spinDuration = 7.5f;

                // 回転スピードをレーザー用の速度（fireSpinSpeed: 0.5f）に切り替えて回し続ける
                Vector3 rot = GetRotation();
                rot.y += fireSpinSpeed * deltaTime;
                SetRotation(rot);
                GetTransform()->isQuaternionMaster = false;

                // ==========================================
                // ★ 追加：ビームを一気に極太にして、当たり判定を付ける！
                // ==========================================
                float expandTime = 0.2f; // 0.2秒で極太になる
                float t = std::min(animTimer_ / expandTime, 1.0f);

                // 太さを 0.1 から 3.0 へ一気に膨張させる
                float beamThickness = Math::Lerp(0.1f, 1.0f, Easing::OutExpo(t));

                for (Object3d* block : armorBlocks_) {
                    if (!block) continue;
                    for (Object3d* child : block->GetChildren()) {
                        if (child->GetName().find("Beam_Cylinder") != std::string::npos) {

                            // ==========================================
                            // ★ 修正：長さのY軸は80.0fに固定し、XZ（太さ）を極太にする！
                            // ==========================================
                            child->SetScale({ beamThickness, 80.0f, beamThickness });

                            // ※回転（Rotation）はPhase 63で倒したままなので、ここでは設定しなくてOKです！

                            child->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f }); // 濃い赤
                            child->SetCollisionAttribute(kEnemyAttack);  // ★触れたらダメージ！
                        }
                    }
                }

                // 撃ち終わったら消して復帰！
                if (animTimer_ >= spinDuration) {
                    animPhase_ = 65; // 復帰フェーズへ
                    animTimer_ = 0.0f;
                    animStartRot_ = GetRotation();

                    // ==========================================
                    // ★ 追加：ビームを消して、当たり判定も消す
                    // ==========================================
                    for (Object3d* block : armorBlocks_) {
                        if (!block) continue;
                        for (Object3d* child : block->GetChildren()) {
                            if (child->GetName().find("Beam_Cylinder") != std::string::npos) {
                                child->SetScale({ 0.0f, 0.0f, 0.0f }); // 見えなくする
                                child->SetCollisionAttribute(0);       // 当たり判定を消す
                            }
                        }
                    }

                    blockStartPos_.clear();
                    blockStartScale_.clear();
                    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                        blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                        blockStartScale_.push_back(armorBlocks_[i]->GetScale());
                    }
                }
                }
        // --- Phase 65: 回転を止め、待機状態のバラバラ軌道へ復帰する ---
        else if (animPhase_ == 65) {
            animTimer_ += deltaTime;
            // 復帰は実時間で1.5秒 (1.5 * 1.5 = 2.25f)
            float duration = 2.25f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::InOutSine(t);

            // コアの回転をゆっくり 0 に戻す
            Vector3 rot = animStartRot_;
            rot.y = Math::Lerp(animStartRot_.y, 0.0f, easeT);
            SetRotation(rot);
            GetTransform()->isQuaternionMaster = false;

            // ブロックを元の待機軌道に戻す
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size()) {
                    OrbitData orbit = GetIdleOrbit(i);
                    Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                    armorBlocks_[i]->SetTranslate(pos);

                    Vector3 scale = Math::Lerp(blockStartScale_[i], orbit.scale, easeT);
                    armorBlocks_[i]->SetScale(scale);

                    armorBlocks_[i]->SetRotation(orbit.rot);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }

            if (t >= 1.0f) {
                animPhase_ = 0;
                attackMode_ = 0;
                animTimer_ = 0.0f;
            }
        }
        }
}

void BossCore::UpdateFlyingBlocks(float deltaTime) {
    int landedCount = 0; // 地面に刺さっているブロックの数
    static Math math;

    // ==========================================
    // 1. 各ブロックの移動・回転・状態更新
    // ==========================================
    for (auto& fb : flyingBlocks_) {
        if (!fb.block) continue;

        // ==========================================
        // モード4（頭上へ装填中）
        // ==========================================
        if (fb.mode == 4) {
            Vector3 bossPos = GetTranslate();
            // コアの頭上（Y + 4.0f 付近）を目標地点にする
            Vector3 headPos = { bossPos.x, bossPos.y + 4.0f, bossPos.z };
            Vector3 currentPos = fb.block->GetTranslate();

            Vector3 dir = { headPos.x - currentPos.x, headPos.y - currentPos.y, headPos.z - currentPos.z };
            float distance = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (distance < 0.5f) {
                // --- 頭上に到着！装填完了！ ---
                fb.block->SetTranslate(headPos);

                // ここで初めてプレイヤーへの方向を計算して「ドカン！」と撃ち出す！
                if (target_) {
                    Vector3 targetPos = target_->GetWorldPosition();

                    // ★ タイクラーさん仕様：プレイヤーの「足元（地面）」を直接狙う！
                    targetPos.y = 0.0f;

                    Vector3 toPlayer = math.Normalize(targetPos - headPos);

                    // 初速を少し速め(60.0f)にして鋭く飛ばす！
                    float bulletSpeed = 60.0f;
                    fb.velocity = { toPlayer.x * bulletSpeed, toPlayer.y * bulletSpeed, toPlayer.z * bulletSpeed };

                    float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                    fb.currentRot = { 0.0f, angleY, 0.0f };
                }
                fb.mode = 0; // 「飛翔モード」へ移行！
            }
            else {
                // --- 頭上に向かって移動中（シュッ！） ---
                dir.x /= distance; dir.y /= distance; dir.z /= distance;
                float gatherSpeed = 30.0f; // 頭上に移動するスピード
                currentPos.x += dir.x * gatherSpeed * deltaTime;
                currentPos.y += dir.y * gatherSpeed * deltaTime;
                currentPos.z += dir.z * gatherSpeed * deltaTime;
                fb.block->SetTranslate(currentPos);

                // 移動中も少し回転させておく
                fb.currentRot.x += 15.0f * deltaTime;
                fb.currentRot.y += 30.0f * deltaTime;
                fb.block->SetRotation(fb.currentRot);
            }
            fb.block->GetTransform()->isQuaternionMaster = false;
        }

        // ==========================================
        // モード0（飛翔中・攻撃）
        // ==========================================
        else if (fb.mode == 0) {
            // --- 攻撃中（直線的に足元へ突撃！） ---

            // ★ タイクラーさん仕様：重力の計算はしない！（直線レーザー）

            Vector3 pos = fb.block->GetTranslate();
            pos.x += fb.velocity.x * deltaTime;
            pos.y += fb.velocity.y * deltaTime;
            pos.z += fb.velocity.z * deltaTime;

            // 地面（Y=0.0f）にぶつかったら刺さって止まる
            if (pos.y <= 0.0f) {
                pos.y = 0.0f;
                fb.velocity = { 0.0f, 0.0f, 0.0f }; // 速度リセット
                fb.mode = 1; // 地面待機モードへ！
            }
            fb.block->SetTranslate(pos);

            // 乱回転
            Vector3 spinSpeed = { 30.0f, 45.0f, 60.0f };
            fb.currentRot.x += spinSpeed.x * deltaTime;
            fb.currentRot.y += spinSpeed.y * deltaTime;
            fb.currentRot.z += spinSpeed.z * deltaTime;
            fb.block->SetRotation(fb.currentRot);
            fb.block->GetTransform()->isQuaternionMaster = false;

        }
        else if (fb.mode == 1) {
            // --- 地面待機中 ---
            landedCount++; // 地面にある数をカウントする

        }
        else if (fb.mode == 2) {
            // --- ボスへ帰還中 ---
            Vector3 bossPos = GetTranslate();
            Vector3 blockPos = fb.block->GetTranslate();

            // ボスとの距離と方向を計算
            Vector3 dir = { bossPos.x - blockPos.x, bossPos.y - blockPos.y, bossPos.z - blockPos.z };
            float distance = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (distance < 2.0f) {
                fb.mode = 3; // ボスに十分近づいたら回収完了！
            }
            else {
                // 正規化してボスの方向へ進む
                dir.x /= distance; dir.y /= distance; dir.z /= distance;
                float returnSpeed = 60.0f; // 帰りは超高速で引き戻す！
                blockPos.x += dir.x * returnSpeed * deltaTime;
                blockPos.y += dir.y * returnSpeed * deltaTime;
                blockPos.z += dir.z * returnSpeed * deltaTime;
                fb.block->SetTranslate(blockPos);

                // 帰りも回転させる
                Vector3 spinSpeed = { 60.0f, 60.0f, 60.0f };
                fb.currentRot.x += spinSpeed.x * deltaTime;
                fb.currentRot.y += spinSpeed.y * deltaTime;
                fb.currentRot.z += spinSpeed.z * deltaTime;
                fb.block->SetRotation(fb.currentRot);
                fb.block->GetTransform()->isQuaternionMaster = false;
            }
        }
    }

    // ==========================================
    // 2. 「すべての弾が地面に落ちた」＆「全部撃ち終わった」なら3秒待って一斉帰還！
    // ==========================================
    if (!flyingBlocks_.empty() && landedCount == flyingBlocks_.size() && flyingBlocks_.size() == armorBlocks_.size()) {
        returnDelayTimer_ += deltaTime;
        if (returnDelayTimer_ >= 5.0f) {
            for (auto& fb : flyingBlocks_) {
                fb.mode = 2; // 全員一斉に帰還モードへ
            }
            returnDelayTimer_ = 0.0f; // 次の攻撃のためにタイマーをリセットしておく
        }
    }
    else {
        // まだ条件を満たしていない時（攻撃中など）は、タイマーを確実に0にしておく
        returnDelayTimer_ = 0.0f;
    }

    // ==========================================
    // 3. 回収完了
    // ==========================================
    for (auto it = flyingBlocks_.begin(); it != flyingBlocks_.end(); ) {
        if (it->mode == 3) {
            it->block->SetParent(this);

            int idx = it->originalIndex;

            // ==========================================
            // ★ 修正：戻ってきた弾も、固定位置ではなく待機軌道に乗せる！
            // ==========================================
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


// ==========================================
// バリアのダメージ＆スタン(ダウン)処理
// ==========================================
void BossCore::TakeBarrierDamage(float damage) {
    barrierHp_ -= damage;

    // デバッグコンソールに分かりやすく残りHPを表示！
    DebugConsole::GetInstance()->AddLog("【HIT!】 Barrier Damaged! 残りHP: " + std::to_string(barrierHp_) + " / " + std::to_string(maxBarrierHp_));

    // 無敵タイマーと色リセットタイマーをセット
    damageCooldownTimer_ = 0.5f;
    colorResetTimer_ = 0.15f;

    // 全ブロックを赤く光らせてダメージを受けた感を出す！
    for (Object3d* block : armorBlocks_) {
        if (block) block->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
    }

    if (barrierHp_ <= 0.0f) {
        DebugConsole::GetInstance()->AddLog("★☆ Barrier BROKEN! ☆★");
        barrierHp_ = maxBarrierHp_;

        animPhase_ = 0;
        attackMode_ = 0;
        animTimer_ = 0.0f;

        // ① 弾けている最中のブロック（射撃中など）を全て停止させる
        flyingBlocks_.clear();

        // ② 拡散用のデータを準備する
        blockStartPos_.clear();
        blockTargetPos_.clear();

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            Object3d* block = armorBlocks_[i];
            if (block) {
                // 親子関係は維持したまま（ローカル座標で計算）
                block->SetParent(this);

                // 現在の位置をスタート地点として保存
                blockStartPos_.push_back(block->GetTranslate());

                // 弾け飛ぶ方向をランダムに決定（半径 10.0f 〜 15.0f の範囲）
                float angle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>;
                float distance = 10.0f + (static_cast<float>(rand()) / RAND_MAX) * 5.0f;
                float height = ((static_cast<float>(rand()) / RAND_MAX) * 10.0f) - 5.0f; // 上下にもばらけさせる

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



bool BossCore::OnCollision(Object3d* other) {
    uint32_t attribute = other->GetCollisionAttribute();

    // プレイヤーの剣(kPlayerAttack)が当たった場合は、衝突判定を確認して
    // BaseEnemy のダメージ処理に委譲する（＝剣でダメージを受ける）
    if (attribute & kPlayerAttack) {
        CollisionInfo info = CheckCollision(other);
        if (!info.isColliding) {
            return false;
        }
        // ここで BaseEnemy::OnCollision を呼ぶことで DamageEvent 発行など既存の処理を再利用
        return BaseEnemy::OnCollision(other);
    }

    // それ以外は従来通り BaseEnemy に委譲
    return BaseEnemy::OnCollision(other);
}