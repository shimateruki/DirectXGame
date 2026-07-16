#include "BossCore.h"
#include "SceneManager.h"
#include "InputManager.h"
#include "imgui.h"
#include "easing.h"
#include "DebugConsole.h"
#include <cmath>
#include <numbers>
#include <ctime>
#include <cstdlib>

// 待機アニメーション用のタイマーと軌道計算関数
// =================================================================
namespace {
    float s_globalIdleTimer = 0.0f; // 待機アニメーション用のタイマー

    struct OrbitData {
        Vector3 pos;
        Vector3 rot;
        Vector3 scale;
    };

    // ブロックの待機軌道（現在の理想の位置・回転・スケール）を計算する便利関数！
    OrbitData GetIdleOrbit(size_t index) {
        OrbitData data;

        // スケールをランダム生成（初回のみ計算して記憶）
        // static を付けることで、インスタンス間で共有される固定値として保持される
        static std::vector<Vector3> randomScales;
        if (randomScales.empty()) {
            for (int i = 0; i < 6; ++i) {
                // 0.0 ～ 1.0 の乱数を生成し、0.5 ～ 1.0 の範囲に調整する
                float randomVal = 0.5f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f;
                randomScales.push_back({ randomVal, randomVal, randomVal });
            }
        }

        // 1. 各ブロックの「まばらな初期配置」を設定！
        Vector3 basePos;
        switch (index % 6) {
        case 0: basePos = { 2.0f,  2.0f,  0.5f }; break; // 高め・右
        case 1: basePos = { -1.5f, -1.5f,  1.8f }; break; // 低め・左手前
        case 2: basePos = { 0.6f,  2.5f, -1.8f }; break; // 一番高い・奥
        case 3: basePos = { -2.0f,  0.5f, -1.2f }; break; // 中段・左奥
        case 4: basePos = { 1.5f, -2.0f,  1.2f }; break; // 一番低い・右手前
        case 5: basePos = { -0.8f, -0.8f, -2.0f }; break; // やや低め・奥
        }

        // 2. 全ブロックを「同じスピード」「同じ方向」に回転させる！
        // 速度は0.8fでゆっくり回します
        float angle = s_globalIdleTimer * 0.8f;
        float cosY = std::cos(angle);
        float sinY = std::sin(angle);

        // Y軸を中心に、陣形を崩さずに全体を回す
        Vector3 rotatedPos = {
            basePos.x * cosY - basePos.z * sinY,
            basePos.y, // 高さはそれぞれの初期配置を維持！
            basePos.x * sinY + basePos.z * cosY
        };

        // 全体が呼吸するように、少しだけゆっくりフワフワ上下させる
        float hover = std::sin(s_globalIdleTimer * 1.5f) * 0.3f;
        rotatedPos.y += hover;

        data.pos = rotatedPos;

        // 3. 常にコア(中心)を向くように角度を計算
        float rotY = std::atan2(-data.pos.x, -data.pos.z);
        float xzLen = std::sqrt(data.pos.x * data.pos.x + data.pos.z * data.pos.z);
        float rotX = std::atan2(data.pos.y, xzLen);

        data.rot = { rotX, rotY, 0.0f }; // コアを睨みつける！

        // 記憶したランダムな大きさを適用
        data.scale = randomScales[index % 6];

        return data;
    }
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

// =================================================================
// 初期化・更新
// =================================================================

void BossCore::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 親クラス(BaseEnemy)の初期化
    BaseEnemy::Initialize(common, modelName);

    // 乱数のシードを現在時刻で設定
    // これを呼ぶことで、毎回違う行動パターンを生成する
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    // 演出・攻撃パターン管理用ディレクターの生成
    director_ = std::make_unique<GhostDirector>();
    if (sceneManager_) {
        director_->Initialize(sceneManager_);
    }

    originalColor_ = GetColor();

    // ==========================================
    // レーザー用の円柱モデルを6本生成しておく
    // ==========================================
    for (int i = 0; i < 6; ++i) {
        auto beam = std::make_unique<Object3d>();
        beam->Initialize(common_);         // エンジンの共通データで初期化
        beam->SetModel("Primitives/cylinder");        // ※円柱モデルのファイル名に合わせてください！
        beam->SetScale({ 0.0f, 0.0f, 0.0f }); // 最初は見えないようにする
        beam->SetCollisionAttribute(0);    // 当たり判定なし
        beam->SetCollisionMask(0);
        laserBeams_.push_back(std::move(beam));
    }
}

void BossCore::Update(float deltaTime) {
    // ボスの全体スピード倍率
    // 以前の仕様に合わせ、ボスの時間経過を1.5倍速で進める
    deltaTime *= 1.5f;

    // ==========================================
    // 0キーで時間停止（ザ・ワールド）機能！
    // ==========================================
    InputManager* input = InputManager::GetInstance();

    if (input->IsKeyTriggered(DIK_1)) {
        s_isTimeStopped_ = !s_isTimeStopped_; // 押すたびに切り替え

        if (s_isTimeStopped_) {
            DebugConsole::GetInstance()->AddLog("【TIME STOP】 ボスの時間が止まった…！");
        }
        else {
            DebugConsole::GetInstance()->AddLog("【TIME RESUME】 時は動き出す！");
        }
    }

    // 時間停止中は、このフレームの経過時間を 0 に設定する
    if (s_isTimeStopped_) {
        deltaTime = 0.0f;
    }

    // ここから下はボス本体、装甲ブロック、攻撃フェーズをまとめて更新する
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
        s_globalIdleTimer += deltaTime; // deltaTimeが0ならタイマーも停止する
    }

    // 飛んでいるブロックの更新
    UpdateFlyingBlocks(deltaTime); // ★ 弾も空中でピタッと止まる！

    // アニメーションシーケンスを優先実行
    UpdateAnimationSequence(deltaTime); // アニメーションも現在位置で停止する

    if (animPhase_ != 0 && animPhase_ != 4) {
        return;
    }

    // ==========================================
     // 4. 通常のステート更新（アニメーション中以外に動く）
     // ==========================================
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

// =================================================================
// ステート(状態)管理
// =================================================================
void BossCore::ChangeState(State nextState) {
    state_ = nextState;

    // コア本体とブロックの属性設定
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
        nextAttackPattern = 6;

        // 選ばれた攻撃モードをセットし、対応するPhaseからアニメーション開始！
        attackMode_ = nextAttack;
        animTimer_ = 0.0f;
        shotCount_ = 0; // コンボや射撃カウントもリセット

        if (attackMode_ == 1) {
            animPhase_ = 1;
        }
        else if (attackMode_ == 2) {
            animPhase_ = 10;
        }
        else if (attackMode_ == 3) {
            animPhase_ = 20;
        }
        else if (attackMode_ == 4) {
            animPhase_ = 39;
        }
        else if (attackMode_ == 5) {
            animPhase_ = 50;
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
    // 一定時間待機後、攻撃ステートへ自動移行
    // ==========================================
    animTimer_ += deltaTime;

    // 例：2.0秒待機したら攻撃へ（タイクラーさんのお好みで秒数は調整してください！）
    if (animTimer_ >= 2.0f) {
        ChangeState(State::Attack);
    }
}

void BossCore::UpdateAttack(float deltaTime) {
    // アニメーションの終了を監視
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

    // ブロックを飛散させるアニメーション
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

// 攻撃モードごとの長いフェーズ演出をここで一括管理する
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

        // 自動ループ化に伴い、キー入力による遷移を廃止
        // 攻撃のトリガーは ChangeState(State::Attack) が自動で行います。
        return;
    }

    // ======================================
    // 攻撃モード1：形態変化 ＆ 突進 (Phase 1 ~ 5)
    // ======================================
    if (attackMode_ == 1) {

        // Phase 1 開始時の初期化処理
        if (animPhase_ == 1 && animTimer_ == 0.0f) {
            blockStartPos_.clear();
            blockTargetPos_.clear();

            struct BlockSetting {
                Vector3 translate;
                Vector3 scale;
                Vector3 rotation;
            };

            std::vector<BlockSetting> settings = {
                { { -3.3f,  0.0f,  0.0f }, { 0.300f, 0.500f, 0.500f }, { 0.0f, 0.0f, 0.0f } },
                { { -2.0f,  0.0f,  0.0f }, { 1.035f, 1.000f, 1.500f }, { 0.0f, 0.0f, 0.0f } },
                { {  0.0f,  1.5f,  0.0f }, { 2.000f, 0.506f, 1.500f }, { 0.0f, 0.0f, 0.0f } },
                { {  0.0f, -1.5f,  0.0f }, { 2.000f, 0.511f, 1.500f }, { 0.0f, 0.0f, 0.0f } },
                { {  2.5f,  0.0f,  0.0f }, { 0.500f, 3.000f, 1.500f }, { 0.0f, 0.0f, 0.0f } },
                { {  3.5f,  0.0f,  0.0f }, { 0.500f, 1.000f, 0.500f }, { 0.0f, 0.0f, 0.0f } }
            };

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());

                if (i < settings.size()) {
                    blockTargetPos_.push_back(settings[i].translate);
                    armorBlocks_[i]->SetScale(settings[i].scale);
                    armorBlocks_[i]->SetRotation(settings[i].rotation);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
                else {
                    blockTargetPos_.push_back({ 0.0f, 0.0f, 0.0f });
                }
            }
        }

        // --- フェーズ1: 形態変化（ブロックがカシャッと合体する） ---
        if (animPhase_ == 1) {
            animTimer_ += deltaTime;
            float duration = 1.5f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                    Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                }
            }

            if (t >= 1.0f) {
                animPhase_ = 2;
                animTimer_ = 0.0f;
                animStartPos_ = GetTranslate();
            }
        }
        // --- フェーズ2: 移動 (x = -50) ---
        else if (animPhase_ == 2) {
            animTimer_ += deltaTime;
            float duration = 2.5f;
            float t = std::min(animTimer_ / duration, 1.0f);

            Vector3 pos = GetTranslate();
            pos.x = Math::Lerp(animStartPos_.x, -50.0f, Easing::OutExpo(t));
            SetTranslate(pos);

            if (t >= 1.0f) {
                animPhase_ = 3;
                animTimer_ = 0.0f;
                animStartPos_ = GetTranslate();
            }
        }
        // --- フェーズ3: シェイク & プレイヤー注視 ---
        else if (animPhase_ == 3) {
            animTimer_ += deltaTime;
            float duration = 3.0f;
            float t = std::min(animTimer_ / duration, 1.0f);

            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
                float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation({ GetRotation().x, angleY, GetRotation().z });
                GetTransform()->isQuaternionMaster = false;
            }

            Vector3 pos = animStartPos_;
            float shake = 0.3f;
            pos.x += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shake;
            pos.y += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shake;
            SetTranslate(pos);

            if (t >= 1.0f) {
                animPhase_ = 4;
                animTimer_ = 0.0f;
                animStartPos_ = GetTranslate();
                if (target_) animTargetPos_ = target_->GetWorldPosition();
            }
        }
        // --- フェーズ4: 加速突進 ---
        else if (animPhase_ == 4) {
            animTimer_ += deltaTime;
            float duration = 1.5f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easedT = std::pow(t, 4.0f);

            SetTranslate(Math::Lerp(animStartPos_, animTargetPos_, easedT));

            float totalRotation = std::numbers::pi_v<float> *2.0f * 5.0f;
            SetRotation({ easedT * totalRotation, GetRotation().y, GetRotation().z });
            GetTransform()->isQuaternionMaster = false;

            if (t >= 1.0f) {
                animPhase_ = 5;
                animTimer_ = 0.0f;
            }
        }
        // --- フェーズ5: 待機軌道に向かってゆっくり復帰する ---
        else if (animPhase_ == 5) {

            if (animTimer_ == 0.0f) {
                blockStartPos_.clear();
                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                }
            }

            animTimer_ += deltaTime;
            float duration = 3.0f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            SetRotation({ 0.0f, 0.0f, 0.0f });
            GetTransform()->isQuaternionMaster = false;

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size()) {
                    OrbitData orbit = GetIdleOrbit(i);
                    Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                    armorBlocks_[i]->SetScale(orbit.scale);
                    armorBlocks_[i]->SetRotation(orbit.rot);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }

            // 完全に復帰したら攻撃終了！
            if (t >= 1.0f) {
                animPhase_ = 0;
                attackMode_ = 0;
                animTimer_ = 0.0f;
            }
        }
    }

    // ======================================
    // 攻撃モード2：移動 → 陣形変化 → ブロック射撃 (Phase 10 ~ 13)
    // ======================================
    else if (attackMode_ == 2) {

        // --- Phase 10: X = 50.0f へ移動 ---
        if (animPhase_ == 10) {
            if (animTimer_ == 0.0f) animStartPos_ = GetTranslate();
            animTimer_ += deltaTime;
            float t = std::min(animTimer_ / 2.5f, 1.0f);

            Vector3 pos = GetTranslate();
            pos.x = Math::Lerp(animStartPos_.x, 50.0f, Easing::OutExpo(t));
            SetTranslate(pos);

            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
                float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation({ GetRotation().x, angleY, GetRotation().z });
                GetTransform()->isQuaternionMaster = false;
            }

            if (t >= 1.0f) {
                animPhase_ = 11;
                animTimer_ = 0.0f;

                blockStartPos_.clear();
                blockTargetPos_.clear();

                struct BlockSetting {
                    Vector3 translate;
                    Vector3 scale;
                    Vector3 rotation;
                };

                float turnY = std::numbers::pi_v<float> / 2.0f;

                std::vector<BlockSetting> settings = {
                    { { -2.0f,  2.5f,  0.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } },
                    { { -2.0f,  1.0f, -2.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } },
                    { { -2.0f,  1.0f,  2.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } },
                    { { -2.0f, -1.0f, -2.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } },
                    { { -2.0f, -1.0f,  2.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } },
                    { { -2.0f, -2.5f,  0.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } }
                };

                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());

                    if (i < settings.size()) {
                        blockTargetPos_.push_back(settings[i].translate);
                        armorBlocks_[i]->SetScale(settings[i].scale);
                        armorBlocks_[i]->SetRotation(settings[i].rotation);
                        armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                    }
                    else {
                        blockTargetPos_.push_back({ 0.0f, 0.0f, 0.0f });
                    }
                }
            }
        }
        // --- Phase 11: 射撃陣形へスライド移動 ---
        else if (animPhase_ == 11) {
            animTimer_ += deltaTime;
            float duration = 1.0f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                    Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                }
            }

            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
                float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation({ GetRotation().x, angleY, GetRotation().z });
                GetTransform()->isQuaternionMaster = false;
            }

            if (t >= 1.0f) {
                animPhase_ = 12;
                animTimer_ = 0.0f;
                shotCount_ = 0;
                shotInterval_ = 0.0f;
            }
        }
        // --- Phase 12: プレイヤーを向いて、1つずつ飛ばす ---
        else if (animPhase_ == 12) {
            animTimer_ += deltaTime;

            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
                float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation({ GetRotation().x, angleY, GetRotation().z });
                GetTransform()->isQuaternionMaster = false;
            }

            float nextShotTime = shotCount_ * 0.5f;

            if (animTimer_ >= nextShotTime) {
                int idx = (int)armorBlocks_.size() - 1 - shotCount_;
                if (idx >= 0 && idx < armorBlocks_.size()) {
                    Object3d* block = armorBlocks_[idx];

                    Vector3 bossPos = GetTranslate();
                    float bossRotY = GetRotation().y;
                    Vector3 localPos = block->GetTranslate();

                    Vector3 worldPos;
                    worldPos.x = bossPos.x + (localPos.x * std::cos(bossRotY) + localPos.z * std::sin(bossRotY));
                    worldPos.y = bossPos.y + localPos.y;
                    worldPos.z = bossPos.z + (-localPos.x * std::sin(bossRotY) + localPos.z * std::cos(bossRotY));

                    block->SetParent(nullptr);
                    block->SetTranslate(worldPos);

                    Vector3 currentRot = block->GetRotation();
                    block->GetTransform()->isQuaternionMaster = false;

                    flyingBlocks_.push_back({ block, {0.0f, 0.0f, 0.0f}, currentRot, 4, idx });
                }

                shotCount_++;

                if (shotCount_ >= armorBlocks_.size()) {
                    animPhase_ = 13;
                    animTimer_ = 0.0f;
                }
            }
        }
        else if (animPhase_ == 13) {
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                bool isFlying = false;
                for (auto& fb : flyingBlocks_) {
                    if (fb.originalIndex == i) { isFlying = true; break; }
                }
                if (!isFlying) {
                    OrbitData orbit = GetIdleOrbit(i);
                    armorBlocks_[i]->SetTranslate(orbit.pos);
                    armorBlocks_[i]->SetScale(orbit.scale);
                    armorBlocks_[i]->SetRotation(orbit.rot);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }

            if (flyingBlocks_.empty()) {
                animTimer_ += deltaTime;
                if (animTimer_ >= 1.0f) {
                    animPhase_ = 0;
                    attackMode_ = 0;
                    animTimer_ = 0.0f;
                }
            }
        }
    }
    // ======================================
    // 攻撃モード3：ハンマー合体 ＆ 目の前で叩き潰す！
    // ======================================
    else if (attackMode_ == 3) {

        // --- Phase 20: 瞬時にハンマー形態へ変形 ---
        if (animPhase_ == 20) {
            if (animTimer_ == 0.0f) {
                // ... (ハンマー変形の hammerSettings 処理はそのまま) ...
                struct HammerSetting {
                    Vector3 translate; Vector3 scale; Vector3 rotation;
                };
                std::vector<HammerSetting> hammerSettings = {
                    { {  0.000f,  4.000f,  0.000f }, { 1.500f, 1.000f, 1.000f }, { 0.0f, 0.0f, 0.0f } },
                    { {  2.000f,  4.000f,  0.000f }, { 0.700f, 1.500f, 1.000f }, { 0.0f, 0.0f, 0.0f } },
                    { {  0.000f,  1.000f,  0.000f }, { 0.400f, 2.200f, 0.400f }, { 0.0f, 0.0f, 0.0f } },
                    { { -2.000f,  4.000f,  0.000f }, { 0.700f, 1.500f, 1.000f }, { 0.0f, 0.0f, 0.0f } },
                    { {  0.000f, -1.800f, -0.002f }, { 0.500f, 0.500f, 0.800f }, { 0.0f, 0.0f, 0.0f } },
                    { {  0.000f,  5.100f,  0.000f }, { 0.500f, 0.250f, 0.500f }, { 0.0f, 0.0f, 0.0f } }
                };
                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    if (i < hammerSettings.size()) {
                        armorBlocks_[i]->SetTranslate(hammerSettings[i].translate);
                        armorBlocks_[i]->SetScale(hammerSettings[i].scale);
                        armorBlocks_[i]->SetRotation(hammerSettings[i].rotation);
                        armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                    }
                }
            }

            animTimer_ += deltaTime;

            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
                float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);
                SetRotation({ 0.0f, angleY, 0.0f });
                GetTransform()->isQuaternionMaster = false;
            }

            if (animTimer_ >= 1.5f) {
                animPhase_ = 21;
                animTimer_ = 0.0f;

                if (target_) { animTargetPos_ = target_->GetWorldPosition(); }
                else { animTargetPos_ = GetTranslate(); }

                if (warningArea_) {
                    // 高さをプレイヤーと同じ（2m）にし、位置を1m浮かせて地面に接地させる
                    if (armorBlocks_.size() > 1 && armorBlocks_[1]) {
                        Vector3 block2Scale = armorBlocks_[1]->GetScale();
                        warningArea_->SetScale({ block2Scale.x, 2.0f, block2Scale.z }); // 高さ2.0f
                    }
                    warningArea_->SetTranslate({ animTargetPos_.x, 1.0f, animTargetPos_.z }); // 中心を1.0fにする

                    float finalRotY = GetRotation().y + (std::numbers::pi_v<float> / 2.0f);
                    warningArea_->SetRotation({ 0.0f, finalRotY, 0.0f });
                    warningArea_->GetTransform()->isQuaternionMaster = false;

                    // 透明度を薄く（0.3f）してスタート
                    warningArea_->SetColor({ 1.0f, 1.0f, 0.0f, 0.9f });
                }
            }
        }
        // --- Phase 21: ロックオンした位置へ移動 ＆ 振りかぶる！ ---
        else if (animPhase_ == 21) {
            animTimer_ += deltaTime;

            float moveDuration = 4.5f;
            float moveT = std::min(animTimer_ / moveDuration, 1.0f);

            if (warningArea_) {
                // 予兆エリアの色を黄色から赤へ遷移
                float currentGreen = Math::Lerp(1.0f, 0.0f, moveT);
                warningArea_->SetColor({ 1.0f, currentGreen, 0.0f, 0.9f });
            }

            // ... (移動処理と回転処理は今のまま) ...
            Vector3 targetPos = animTargetPos_;
            Vector3 currentPos = GetTranslate();
            Vector3 toBoss = currentPos - targetPos;
            toBoss.y = 0.0f;
            float dist = std::sqrt(toBoss.x * toBoss.x + toBoss.z * toBoss.z);
            if (dist > 0.0f) { toBoss.x /= dist; toBoss.z /= dist; }
            Vector3 targetHoverPos = { targetPos.x + toBoss.x * 4.5f, targetPos.y + 1.0f, targetPos.z + toBoss.z * 4.5f };
            float easeT = moveT;
            currentPos.x = Math::Lerp(currentPos.x, targetHoverPos.x, easeT);
            currentPos.y = Math::Lerp(currentPos.y, targetHoverPos.y, easeT);
            currentPos.z = Math::Lerp(currentPos.z, targetHoverPos.z, easeT);
            SetTranslate(currentPos);
            float rotT = std::min(animTimer_ / 3.0f, 1.0f);
            Vector3 toPlayer = targetPos - currentPos;
            float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);
            float elasticT = 0.0f;
            if (rotT == 0.0f) elasticT = 0.0f;
            else if (rotT == 1.0f) elasticT = 1.0f;
            else {
                float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;
                elasticT = -std::pow(2.0f, 10.0f * rotT - 10.0f) * std::sin((rotT * 10.0f - 10.75f) * c4);
            }
            float targetTilt = -70.0f * (std::numbers::pi_v<float> / 180.0f);
            float tiltBack = Math::Lerp(0.0f, targetTilt, elasticT);
            SetRotation({ 0.0f, angleY, tiltBack });
            GetTransform()->isQuaternionMaster = false;

            if (moveT >= 1.0f) {
                animPhase_ = 22;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 22: 一気に振り下ろして叩き潰す！ ---
        else if (animPhase_ == 22) {
            animTimer_ += deltaTime;
            float smashDuration = 0.15f;
            float t = std::min(animTimer_ / smashDuration, 1.0f);

            if (warningArea_) {
                // 振り下ろし中も赤色を表示し続ける
                warningArea_->SetColor({ 1.0f, 0.0f, 0.0f, 0.9f });
            }

            float startRotZ = -70.0f * (std::numbers::pi_v<float> / 180.0f);
            float endRotZ = 270.0f * (std::numbers::pi_v<float> / 180.0f);
            float currentRotZ = Math::Lerp(startRotZ, endRotZ, std::pow(t, 3.0f));
            SetRotation({ 0.0f, GetRotation().y, currentRotZ });

            if (t >= 1.0f) {
                SetRotation({ 0.0f, GetRotation().y, endRotZ });
                if (warningArea_) { warningArea_->SetScale({ 0.0f, 0.0f, 0.0f }); }
                animPhase_ = 23;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 23: 地面に倒れたまま3秒待機 ---
        else if (animPhase_ == 23) {

            if (animTimer_ == 0.0f) {
                animStartPos_ = GetRotation();
            }

            animTimer_ += deltaTime;

            if (animTimer_ >= 3.0f) {
                animPhase_ = 24;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 24: 待機軌道に向かって復帰する ---
        else if (animPhase_ == 24) {
            // (ここは今までと全く同じなので省略せずにそのまま残します)
            if (animTimer_ == 0.0f) {
                blockStartPos_.clear();
                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                }
            }

            animTimer_ += deltaTime;
            float duration = 1.0f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            Vector3 bossPos = GetTranslate();
            bossPos.y = Math::Lerp(bossPos.y, 4.0f, easeT);
            SetTranslate(bossPos);

            Vector3 currentRot;
            currentRot.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
            currentRot.y = Math::Lerp(animStartPos_.y, 0.0f, easeT);
            currentRot.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
            SetRotation(currentRot);

            GetTransform()->isQuaternionMaster = false;

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size()) {
                    OrbitData orbit = GetIdleOrbit(i);
                    Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                    armorBlocks_[i]->SetScale(orbit.scale);
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
    // ======================================
    // 攻撃モード4：絶望の十字往復ギガ・ウォール！
    // ======================================
    else if (attackMode_ == 4) {

        // --- Phase 39: コンボ数に応じた壁の配置計算 ---
        if (animPhase_ == 39) {
            blockStartPos_.clear();
            blockTargetPos_.clear();

            float blockWidth = 25.0f;

            Vector3 bossCurrentPos = GetTranslate();
            animStartPos_ = bossCurrentPos; // 移動のスタート地点を記憶

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (shotCount_ == 0) {
                    Vector3 localPos = armorBlocks_[i]->GetTranslate();
                    float bossRotY = GetRotation().y;
                    Vector3 worldPos;
                    worldPos.x = bossCurrentPos.x + (localPos.x * std::cos(bossRotY) + localPos.z * std::sin(bossRotY));
                    worldPos.y = bossCurrentPos.y + localPos.y;
                    worldPos.z = bossCurrentPos.z + (-localPos.x * std::sin(bossRotY) + localPos.z * std::cos(bossRotY));

                    armorBlocks_[i]->SetParent(nullptr);
                    armorBlocks_[i]->SetTranslate(worldPos);
                    blockStartPos_.push_back(worldPos);
                }
                else {
                    blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                }

                float offset = -(i - 2.5f) * blockWidth;
                Vector3 targetPos;

                if (shotCount_ == 0) {
                    targetPos = { offset, 2.0f, 150.0f };
                    armorBlocks_[i]->SetScale({ blockWidth, 4.0f, 1.0f });
                }
                else if (shotCount_ == 1) {
                    targetPos = { offset, 2.0f, -150.0f };
                    armorBlocks_[i]->SetScale({ blockWidth, 4.0f, 1.0f });
                }
                else if (shotCount_ == 2) {
                    targetPos = { 150.0f, 2.0f, offset };
                    armorBlocks_[i]->SetScale({ 1.0f, 4.0f, blockWidth });
                }
                else if (shotCount_ == 3) {
                    targetPos = { -150.0f, 2.0f, offset };
                    armorBlocks_[i]->SetScale({ 1.0f, 4.0f, blockWidth });
                }

                blockTargetPos_.push_back(targetPos);
                armorBlocks_[i]->SetRotation({ 0.0f, 0.0f, 0.0f });
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }

            animPhase_ = 40;
            animTimer_ = 0.0f;
        }
        // --- Phase 40: ボスが上空へ先回り ＆ ブロックが壁を形成 ---
        else if (animPhase_ == 40) {
            animTimer_ += deltaTime;
            float duration = 1.5f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            Vector3 bossPos = GetTranslate();

            if (shotCount_ == 0) {
                bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
                bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
                bossPos.z = Math::Lerp(animStartPos_.z, 150.0f, easeT); // 奥
            }
            else if (shotCount_ == 1) {
                bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
                bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
                bossPos.z = Math::Lerp(animStartPos_.z, -150.0f, easeT); // 手前
            }
            else if (shotCount_ == 2) {
                bossPos.x = Math::Lerp(animStartPos_.x, 150.0f, easeT); // 右
                bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
                bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
            }
            else if (shotCount_ == 3) {
                bossPos.x = Math::Lerp(animStartPos_.x, -150.0f, easeT); // 左
                bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
                bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
            }
            SetTranslate(bossPos);

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                    Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                }
            }

            SetRotation({ 0.0f, 0.0f, 0.0f });
            GetTransform()->isQuaternionMaster = false;

            if (t >= 1.0f) {
                animPhase_ = 41;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 41: 壁だけがステージを往復横断！ ---
        else if (animPhase_ == 41) {
            animTimer_ += deltaTime;
            float duration = 5.0f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = std::pow(t, 2.0f);

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                Vector3 blockPos = armorBlocks_[i]->GetTranslate();

                if (shotCount_ == 0) {
                    blockPos.z = Math::Lerp(150.0f, -150.0f, easeT);
                }
                else if (shotCount_ == 1) {
                    blockPos.z = Math::Lerp(-150.0f, 150.0f, easeT);
                }
                else if (shotCount_ == 2) {
                    blockPos.x = Math::Lerp(150.0f, -150.0f, easeT);
                }
                else if (shotCount_ == 3) {
                    blockPos.x = Math::Lerp(-150.0f, 150.0f, easeT);
                }
                armorBlocks_[i]->SetTranslate(blockPos);
            }

            if (t >= 1.0f) {
                animPhase_ = 42;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 42: 攻撃後の猶予 ＆ 往復のループ判定！ ---
        else if (animPhase_ == 42) {
            animTimer_ += deltaTime;

            if (animTimer_ >= 0.5f) {
                shotCount_++;

                if (shotCount_ < 4) {
                    animPhase_ = 39;
                }
                else {
                    animPhase_ = 43;
                }
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 43: 親子関係を復活させ、コアも元の定位置に戻る ---
        else if (animPhase_ == 43) {

            if (animTimer_ == 0.0f) {
                blockStartPos_.clear();
                Vector3 bossPos = GetTranslate();
                float bossRotY = GetRotation().y;

                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    Vector3 worldPos = armorBlocks_[i]->GetTranslate();
                    Vector3 offset = { worldPos.x - bossPos.x, worldPos.y - bossPos.y, worldPos.z - bossPos.z };
                    Vector3 localPos;
                    localPos.x = offset.x * std::cos(-bossRotY) + offset.z * std::sin(-bossRotY);
                    localPos.y = offset.y;
                    localPos.z = -offset.x * std::sin(-bossRotY) + offset.z * std::cos(-bossRotY);

                    armorBlocks_[i]->SetParent(this);
                    armorBlocks_[i]->SetTranslate(localPos);
                    blockStartPos_.push_back(localPos);
                }
                animStartPos_ = bossPos;
            }

            animTimer_ += deltaTime;
            float duration = 3.0f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            Vector3 bossPos = GetTranslate();
            bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
            bossPos.y = Math::Lerp(animStartPos_.y, 4.0f, easeT);
            bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
            SetTranslate(bossPos);

            SetRotation({ 0.0f, 0.0f, 0.0f });
            GetTransform()->isQuaternionMaster = false;

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size()) {
                    OrbitData orbit = GetIdleOrbit(i);
                    Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                    armorBlocks_[i]->SetScale(orbit.scale);
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
    }// ======================================
    // 攻撃モード5：人型になって倒れてくる！ (Phase 50 ~ 55)
    // ======================================
    else if (attackMode_ == 5) {

        // --- Phase 50: ボスコアが画面の端（X = -50）まで行く ---
        if (animPhase_ == 50) {
            if (animTimer_ == 0.0f) {
                animStartPos_ = GetTranslate();
            }
            animTimer_ += deltaTime;
            float duration = 2.0f; // 2秒かけて移動
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            Vector3 pos = GetTranslate();
            pos.x = Math::Lerp(animStartPos_.x, -50.0f, easeT); // 左端へ

            pos.y = Math::Lerp(animStartPos_.y, 24.0f, easeT);
            SetTranslate(pos);

            if (target_) {
                // プレイヤーを向くように回転。背中合わせにならないよう調整
                Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
                float angleY = std::atan2(toPlayer.x, toPlayer.z) + std::numbers::pi_v<float>;

                SetRotation({ GetRotation().x, angleY, GetRotation().z });
                GetTransform()->isQuaternionMaster = false;
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
                blockTargetScale_.clear(); // 初期化

                animStartRot_ = GetRotation();

                // Block4とBlock5の Z軸回転 を 90度（pi/2）に設定
                // ==========================================
                float rotZ90 = std::numbers::pi_v<float> / 2.0f; // 90度のラジアン値

                struct BlockSetting { Vector3 translate; Vector3 scale; Vector3 rotation; };
                std::vector<BlockSetting> settings = {
                    { { -5.0f, -22.5f,  6.0f }, {  2.0f,  4.0f,  5.0f }, { 0.0f, 0.0f, 0.0f } },   // Block1(足)
                    { {  0.0f,  21.5f,  6.0f }, { 10.0f, 10.0f,  5.0f }, { 0.0f, 0.0f, 0.0f } },   // Block2(頭)
                    { {  0.0f,  -3.5f,  6.0f }, {  8.0f, 15.0f,  5.0f }, { 0.0f, 0.0f, 0.0f } },   // Block3(胴体)
                    { { 11.0f,   0.0f,  6.0f }, {  7.0f,  3.0f,  5.0f }, { 0.0f, 0.0f, rotZ90 } }, // Block4(右腕)
                    { { -11.0f,  0.0f,  6.0f }, {  7.0f,  3.0f,  5.0f }, { 0.0f, 0.0f, rotZ90 } }, // Block5(左腕)
                    { {  5.0f, -22.5f,  6.0f }, {  2.0f,  4.0f,  5.0f }, { 0.0f, 0.0f, 0.0f } }    // Block6(足)
                };

                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    armorBlocks_[i]->SetParent(this);
                    blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                    blockStartScale_.push_back(armorBlocks_[i]->GetScale()); // 開始時スケールを記憶

                    if (i < settings.size()) {
                        blockTargetPos_.push_back(settings[i].translate);
                        blockTargetScale_.push_back(settings[i].scale); // 目標スケールを記憶
                        armorBlocks_[i]->SetRotation(settings[i].rotation);
                        armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                    }
                    else {
                        blockTargetPos_.push_back({ 0.0f, 0.0f, 0.0f });
                        blockTargetScale_.push_back({ 0.0f, 0.0f, 0.0f });
                    }
                }

                if (warningArea_) {
                    warningArea_->SetTranslate({ 0.0f, -0.5f, 6.0f });
                    warningArea_->SetScale({ 35.0f, 55.0f, 5.0f });
                    warningArea_->SetRotation({ 0.0f, 0.0f, 0.0f });
                    warningArea_->GetTransform()->isQuaternionMaster = false;
                    warningArea_->SetColor({ 1.0f, 1.0f, 0.0f, 0.5f });
                }
            }

            animTimer_ += deltaTime;
            float duration = 1.5f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            // コアはそのままに、ブロック全体を 0.2倍 に圧縮
            // ==========================================
            float currentOverallScale = Math::Lerp(1.0f, 0.2f, easeT);

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                    // 目標の座標もスケールも、圧縮率(0.2倍)をかける！
                    Vector3 targetP = {
                        blockTargetPos_[i].x * 0.2f,
                        blockTargetPos_[i].y * 0.2f,
                        blockTargetPos_[i].z * 0.2f
                    };
                    Vector3 pos = Math::Lerp(blockStartPos_[i], targetP, easeT);
                    armorBlocks_[i]->SetTranslate(pos);

                    Vector3 targetS = {
                        blockTargetScale_[i].x * 0.2f,
                        blockTargetScale_[i].y * 0.2f,
                        blockTargetScale_[i].z * 0.2f
                    };
                    Vector3 scale = Math::Lerp(blockStartScale_[i], targetS, easeT);
                    armorBlocks_[i]->SetScale(scale);
                }
            }

            // コアの回転演出
            float spinCount = 3.0f;
            float totalAngle = spinCount * 2.0f * std::numbers::pi_v<float>;
            Vector3 rot = GetRotation();
            rot.y = animStartRot_.y - totalAngle * (1.0f - easeT);
            SetRotation(rot);
            GetTransform()->isQuaternionMaster = false;

            if (t >= 1.0f) {
                animPhase_ = 52;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 52: 倒れる前のタメ（予兆が赤くなる ＆ 巨大化！） ---
        else if (animPhase_ == 52) {
            animTimer_ += deltaTime;

            // ==========================================
            // 倒れるまでの「合計のタメ時間」をここで長くする！（例：2.5秒）
            // ==========================================
            float duration = 2.5f;
            float t = std::min(animTimer_ / duration, 1.0f);

            if (warningArea_) {
                float currentGreen = Math::Lerp(1.0f, 0.0f, t);
                warningArea_->SetColor({ 1.0f, currentGreen, 0.0f, 0.5f });
            }

            float currentOverallScale = 1.0f;

            // ==========================================
            // 't' ではなく 'animTimer_'（絶対時間）を使う！
            // ==========================================
            if (animTimer_ < 0.15f) {
                currentOverallScale = 0.2f; // 小さいまま力を溜める
            }
            else if (animTimer_ < 0.3f) {
                currentOverallScale = 1.2f; // バッ！と少し大きめに膨らむ
            }
            else if (animTimer_ < 0.45f) {
                currentOverallScale = 0.5f; // また縮む
            }
            else if (animTimer_ < 0.6f) {
                currentOverallScale = 1.1f; // また膨らむ
            }
            else if (animTimer_ < 0.75f) {
                currentOverallScale = 0.8f; // ちょい縮む
            }
            else {
                // 指定時間経過後、攻撃開始までスケールを固定して静止
                currentOverallScale = 1.0f;
            }

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockTargetPos_.size()) {
                    Vector3 pos = { blockTargetPos_[i].x * currentOverallScale, blockTargetPos_[i].y * currentOverallScale, blockTargetPos_[i].z * currentOverallScale };
                    armorBlocks_[i]->SetTranslate(pos);

                    Vector3 scale = { blockTargetScale_[i].x * currentOverallScale, blockTargetScale_[i].y * currentOverallScale, blockTargetScale_[i].z * currentOverallScale };
                    armorBlocks_[i]->SetScale(scale);
                }
            }

            if (t >= 1.0f) {
                animPhase_ = 53;
                animTimer_ = 0.0f;
                animStartRot_ = GetRotation();
                animStartPos_ = GetTranslate();
            }
            }
        // --- Phase 53: 前にぶっ倒れて叩き潰す！ ---
        else if (animPhase_ == 53) {
            animTimer_ += deltaTime;
            float duration = 2.0f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = std::pow(t, 3.0f);

            // ==========================================
            // 全体が1.0倍なので、つま先の距離(ピボット)も1.5倍！
            // ==========================================
            float overallScale = 1.0f;
            float pivotY = -24.5f * overallScale;
            float pivotZ = 8.5f * overallScale;

            float currentRotX = Math::Lerp(animStartRot_.x, -90.0f * (std::numbers::pi_v<float> / 180.0f), easeT);

            Vector3 rot = animStartRot_;
            rot.x = currentRotX;
            SetRotation(rot);
            GetTransform()->isQuaternionMaster = false;

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
            SetTranslate(newPos);

            // ==========================================
            // 右腕と左腕で「回転する方向」を逆にして、外側を通す！
            // ==========================================
            float startArmAngle = std::numbers::pi_v<float> / 2.0f;  // 90度
            float armOffset45 = std::numbers::pi_v<float> / 4.0f;   // 45度のラジアン

            // ① 右腕（Block4）は 270度(真上)の手前、225度に向かって「反時計回り（外回り）」
            // 3pi/2 (270度) - pi/4 (45度) = 225度
            float targetArmAngleRight = (3.0f * std::numbers::pi_v<float> / 2.0f) - armOffset45;
            float currentArmAngleRight = Math::Lerp(startArmAngle, targetArmAngleRight, easeT);

            // ② 左腕（Block5）は -90度(真上)の手前、-45度に向かって「時計回り（外回り）」
            // -pi/2 (-90度) + pi/4 (45度) = -45度
            float targetArmAngleLeft = (-std::numbers::pi_v<float> / 2.0f) + armOffset45;
            float currentArmAngleLeft = Math::Lerp(startArmAngle, targetArmAngleLeft, easeT);

            float armHalfLength = 3.5f; // 腕の長さ(Scale.x = 7.0)の半分
            float shoulderY = 3.5f;     // 肩の高さ（中心0.0 + 半分3.5）

            // 右腕 (Block4: index 3)
            if (armorBlocks_.size() > 3 && armorBlocks_[3]) {
                Vector3 pos = armorBlocks_[3]->GetTranslate();
                // 右腕用の角度(currentArmAngleRight)で計算
                pos.x = 11.0f - armHalfLength * std::cos(currentArmAngleRight);
                pos.y = shoulderY - armHalfLength * std::sin(currentArmAngleRight);

                armorBlocks_[3]->SetTranslate(pos);
                armorBlocks_[3]->SetRotation({ 0.0f, 0.0f, currentArmAngleRight });
                armorBlocks_[3]->GetTransform()->isQuaternionMaster = false;
            }

            // 左腕 (Block5: index 4)
            if (armorBlocks_.size() > 4 && armorBlocks_[4]) {
                Vector3 pos = armorBlocks_[4]->GetTranslate();
                // 左腕用の角度(currentArmAngleLeft)で計算
                pos.x = -11.0f - armHalfLength * std::cos(currentArmAngleLeft);
                pos.y = shoulderY - armHalfLength * std::sin(currentArmAngleLeft);

                armorBlocks_[4]->SetTranslate(pos);
                armorBlocks_[4]->SetRotation({ 0.0f, 0.0f, currentArmAngleLeft });
                armorBlocks_[4]->GetTransform()->isQuaternionMaster = false;
            }

            if (warningArea_) warningArea_->SetColor({ 1.0f, 0.0f, 0.0f, 0.9f });

            if (t >= 1.0f) {
                if (warningArea_) warningArea_->SetScale({ 0.0f, 0.0f, 0.0f });
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
                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                    blockStartScale_.push_back(armorBlocks_[i]->GetScale());
                }
                animStartRot_ = GetRotation();
            }

            animTimer_ += deltaTime;
            float duration = 1.5f;
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::InOutSine(t);

            // 起き上がりのピボットも 1.0倍 で計算
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
            Vector3 rot = animStartRot_;
            rot.x = currentRotX;
            SetRotation(rot);
            GetTransform()->isQuaternionMaster = false;

            float currentRotLocalY = pivotY * std::cos(currentRotX) - pivotZ * std::sin(currentRotX);
            float currentRotLocalZ = pivotY * std::sin(currentRotX) + pivotZ * std::cos(currentRotX);

            Vector3 newPos = {
                currentToeWorld.x - dirX * currentRotLocalZ,
                currentToeWorld.y - currentRotLocalY,
                currentToeWorld.z - dirZ * currentRotLocalZ
            };
            SetTranslate(newPos);

            // ブロックを元の待機軌道に戻す（スケールも元通りに！）
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size()) {
                    OrbitData orbit = GetIdleOrbit(i);
                    Vector3 bPos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                    armorBlocks_[i]->SetTranslate(bPos);

                    Vector3 bScale = Math::Lerp(blockStartScale_[i], orbit.scale, easeT); // ★追加：スケールも戻す
                    armorBlocks_[i]->SetScale(bScale);

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
    // ======================================
    // 攻撃モード6：中央移動 → 回転タメ → 回転変形 → 0.5秒停止 → レーザー！ (Phase 60 ~ 65)
    // ======================================
    else if (attackMode_ == 6) {

        // 回転スピードの調整用変数
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

                            // Y軸を長さ(80.0f)に、XZを極細(0.1f)に設定
                            // ==========================================
                            child->SetScale({ 0.1f, 80.0f, 0.1f });

                            // 円柱をX軸に90度（pi/2）倒して、正面に向ける
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

            // 実時間で 0.5秒間停止 (0.5 * 1.5 = 0.75f)
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

                // ビームの太さを拡大し、当たり判定を付与
                // ==========================================
                float expandTime = 0.2f; // 0.2秒で極太になる
                float t = std::min(animTimer_ / expandTime, 1.0f);

                // 太さを 0.1 から 3.0 へ一気に膨張させる
                float beamThickness = Math::Lerp(0.1f, 1.0f, Easing::OutExpo(t));

                for (Object3d* block : armorBlocks_) {
                    if (!block) continue;
                    for (Object3d* child : block->GetChildren()) {
                        if (child->GetName().find("Beam_Cylinder") != std::string::npos) {

                            // 長さ(Y軸)は80.0fに固定し、太さ(XZ)を拡大
                            // ==========================================
                            child->SetScale({ beamThickness, 80.0f, beamThickness });

                            // ※回転（Rotation）はPhase 63で倒したままなので、ここでは設定しなくてOKです！

                            child->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f }); // 濃い赤
                            child->SetCollisionAttribute(kEnemyAttack);  // 攻撃属性を付与
                        }
                    }
                }

                // 撃ち終わったら消して復帰！
                if (animTimer_ >= spinDuration) {
                    animPhase_ = 65; // 復帰フェーズへ
                    animTimer_ = 0.0f;
                    animStartRot_ = GetRotation();

                    // ビームを非表示にし、当たり判定を削除
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

// 射出された装甲ブロックの飛翔、着地、帰還を更新する
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

                    // プレイヤーの足元（地面）を狙う
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

            // 直線的な軌道で移動（重力計算なし）

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

            // 帰還したブロックを待機軌道に復帰
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
// バリアHPへのダメージとダウン遷移
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
        DebugConsole::GetInstance()->AddLog("Barrier BROKEN!");
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
