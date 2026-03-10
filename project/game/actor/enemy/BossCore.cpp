#include "BossCore.h"
#include "InputManager.h"
#include "imgui.h"
#include "easing.h" // 追加
#include "DebugConsole.h"

// =================================================================
// 初期化・更新
// =================================================================

void BossCore::Initialize(Object3dCommon* common, const std::string& modelName) {
    // 親クラス(BaseEnemy)の初期化
    BaseEnemy::Initialize(common, modelName);

    // 演出・攻撃パターン管理用ディレクターの生成
    director_ = std::make_unique<GhostDirector>();
    if (sceneManager_) {
        director_->Initialize(sceneManager_);
    }
}

void BossCore::Update(float deltaTime) {
    // 1. 基本更新（行列計算など）
    Object3d::Update (deltaTime);

    // ★ 3. アニメーションシーケンスを優先実行
    UpdateAnimationSequence (deltaTime);

    // 【重要】アニメーション実行中（Phase 1～3）は、
    // 下の既存ステート（Idle/Attackなど）を走らせないようにガードをかける！
    if (animPhase_ != 0 && animPhase_ != 4) {
        return;
    }

    // 4. 通常のステート更新（アニメーション中以外に動く）
    if (isFirstFrame_) {
        ChangeState (State::Idle);
        isFirstFrame_ = false;
    }

    switch (state_) {
    case State::Idle:   UpdateIdle (deltaTime);   break;
    case State::Attack: UpdateAttack (deltaTime); break;
    case State::Weak:   UpdateWeak (deltaTime);   break;
    }
}

// =================================================================
// ステート(状態)管理
// =================================================================

void BossCore::ChangeState(State nextState) {
    state_ = nextState;

    if (!director_) return;

    // 状態移行に合わせて、ディレクターの再生シナリオを切り替える
    switch (state_) {
    case State::Idle:
        director_->LoadScenario("boss_attack_1");
        director_->PlayScenario(false, false);
        break;

    case State::Attack: {
        // ランダムな攻撃パターンを選択 (1〜10)
        int nextAttack = rand() % 10 + 1;
        std::string attackName = "boss_attack_" + std::to_string(nextAttack);

        // TODO: 攻撃シナリオの実装が完了したらコメントアウトを外す
        // director_->LoadScenario(attackName);
        // director_->PlayScenario();
        break;
    }

    case State::Weak:
        // TODO: 弱点露出シナリオの実装が完了したらコメントアウトを外す
        // director_->LoadScenario("boss_weak");
        // director_->PlayScenario();
        break;
    }
}

// =================================================================
// 各ステートの個別更新処理
// =================================================================

void BossCore::UpdateIdle(float deltaTime) {
    // 待機シナリオが終了したら、攻撃ステートへ移行
    if (director_ && director_->IsFinished()) {
        ChangeState(State::Attack);
    }
}

void BossCore::UpdateAttack(float deltaTime) {
    if (!director_) return;

    // シナリオ内で発生したイベント(トリガー)を取得
    ActiveEvent eventInfo = director_->GetActiveEvent();

    if (eventInfo.id != 0 && eventInfo.targetObject) {
        // イベント発生元のワールド座標を取得 (弾やエフェクトの発生位置として使用)
        Vector3 spawnPos = eventInfo.targetObject->GetWorldPosition();

        if (eventInfo.id == 1) {
            // イベントID 1 の処理 (例: 斬撃エフェクト生成など)
        } else if (eventInfo.id == 2) {
            // イベントID 2 の処理 (例: 飛び道具の発射など)
        }
    }

    // 攻撃シナリオが終了したら、弱点露出ステートへ移行
    if (director_->IsFinished()) {
        ChangeState(State::Weak);
    }
}

void BossCore::UpdateWeak(float deltaTime) {
    // 弱点露出シナリオが終了したら、待機ステートへ戻る
    if (director_ && director_->IsFinished()) {
        ChangeState(State::Idle);
    }
}

void BossCore::UpdateAnimationSequence (float deltaTime) {
    InputManager *input = InputManager::GetInstance ();

    // ======================================
    // フェーズ0: 入力待ち（待機）
    // ======================================
    if (animPhase_ == 0) {
        if (input->IsKeyTriggered (DIK_1)) {
            // モード1：形態変化からの突進
            attackMode_ = 1;
            animPhase_ = 1; // ★ 変形フェーズからスタート！
            animTimer_ = 0.0f;

            // --- 形態変化の準備（座標の計算と記憶） ---
            blockStartPos_.clear ();
            blockTargetPos_.clear ();

            // ★ ここで各ブロックの【最終形態】を細かく設定します！
            struct BlockSetting {
                Vector3 translate; // コアからのローカル座標 (目的地)
                Vector3 scale;     // 大きさ
                Vector3 rotation;  // 回転（ラジアン）
            };

            std::vector<BlockSetting> settings = {
                // { { 座標X, 座標Y, 座標Z }, { スケールX, スケールY, スケールZ }, { 回転X, 回転Y, 回転Z } }
                { { -3.3f,  0.0f,  0.0f }, { 0.300f, 0.500f, 0.500f }, { 0.0f, 0.0f, 0.0f } }, // 画像1 (左端の小型パーツ)
                { { -2.0f,  0.0f,  0.0f }, { 1.035f, 1.000f, 1.500f }, { 0.0f, 0.0f, 0.0f } }, // 画像2 (左側の厚みのあるパーツ)
                { {  0.0f,  1.5f,  0.0f }, { 2.000f, 0.506f, 1.500f }, { 0.0f, 0.0f, 0.0f } }, // 画像3 (頭上の平たいパーツ)
                { {  0.0f, -1.5f,  0.0f }, { 2.000f, 0.511f, 1.500f }, { 0.0f, 0.0f, 0.0f } }, // 画像4 (足元の平たいパーツ)
                { {  2.5f,  0.0f,  0.0f }, { 0.500f, 3.000f, 1.500f }, { 0.0f, 0.0f, 0.0f } }, // 画像5 (右側の縦長パーツ)
                { {  3.5f,  0.0f,  0.0f }, { 0.500f, 1.000f, 0.500f }, { 0.0f, 0.0f, 0.0f } }  // 画像6 (右端の小型パーツ)
            };

            for (size_t i = 0; i < armorBlocks_.size (); ++i) {
                // 移動のスタート地点を記憶
                blockStartPos_.push_back (armorBlocks_[i]->GetTranslate ());

                if (i < settings.size ()) {
                    // ゴール地点を記憶 (Phase 1 でここに向かって Lerp します)
                    blockTargetPos_.push_back (settings[i].translate);

                    // 大きさと回転は、変形開始と同時に適用してしまう！
                    armorBlocks_[i]->SetScale (settings[i].scale);
                    armorBlocks_[i]->SetRotation (settings[i].rotation);
                    // ★ 修正箇所1：ブロックのオイラー角(XYZ)を優先させる
                    armorBlocks_[i]->GetTransform ()->isQuaternionMaster = false;
                } else {
                    blockTargetPos_.push_back ({ 0.0f, 0.0f, 0.0f });
                }
            }
        } else if (input->IsKeyTriggered (DIK_2)) {
            // モード2：ブロック射撃
            attackMode_ = 2;
            animPhase_ = 10; // 射撃用フェーズへ
            animTimer_ = 0.0f;
        }
    }

    if (animPhase_ == 0) return;

    // ======================================
    // 攻撃モード1：形態変化 ＆ 突進 (Phase 1 ~ 5)
    // ======================================
    if (attackMode_ == 1) {

        // --- フェーズ1: 形態変化（ブロックがカシャッと合体する） ---
        if (animPhase_ == 1) {
            animTimer_ += deltaTime;
            float duration = 1.5f; // 1.5秒かけて変形
            float t = std::min (animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo (t); // カッコよくスライドさせる

            for (size_t i = 0; i < armorBlocks_.size (); ++i) {
                if (i < blockStartPos_.size () && i < blockTargetPos_.size ()) {
                    Vector3 pos = Math::Lerp (blockStartPos_[i], blockTargetPos_[i], easeT);
                    armorBlocks_[i]->SetTranslate (pos);
                }
            }

            if (t >= 1.0f) {
                animPhase_ = 2; // 変形が終わったら、突進準備(X=-50)へ！
                animTimer_ = 0.0f;
                animStartPos_ = GetTranslate ();
            }
        }
        // --- フェーズ2: 移動 (x = -50) ---
        else if (animPhase_ == 2) {
            animTimer_ += deltaTime;
            float duration = 2.5f;
            float t = std::min (animTimer_ / duration, 1.0f);

            Vector3 pos = GetTranslate ();
            pos.x = Math::Lerp (animStartPos_.x, -50.0f, Easing::OutExpo (t));
            SetTranslate (pos);

            if (t >= 1.0f) {
                animPhase_ = 3;
                animTimer_ = 0.0f;
                animStartPos_ = GetTranslate ();
            }
        }
        // --- フェーズ3: シェイク & プレイヤー注視 ---
        else if (animPhase_ == 3) {
            animTimer_ += deltaTime;
            float duration = 3.0f;
            float t = std::min (animTimer_ / duration, 1.0f);

            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition () - GetWorldPosition ();
                float angleY = std::atan2 (toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation ({ GetRotation ().x, angleY, GetRotation ().z });
                // ★ 修正箇所2：ボスの向き（オイラー角）を優先させる
                GetTransform ()->isQuaternionMaster = false;
            }

            Vector3 pos = animStartPos_;
            float shake = 0.3f;
            pos.x += ((float)rand () / RAND_MAX * 2.0f - 1.0f) * shake;
            pos.y += ((float)rand () / RAND_MAX * 2.0f - 1.0f) * shake;
            SetTranslate (pos);

            if (t >= 1.0f) {
                animPhase_ = 4;
                animTimer_ = 0.0f;
                animStartPos_ = GetTranslate ();
                if (target_) animTargetPos_ = target_->GetWorldPosition ();
            }
        }
        // --- フェーズ4: 加速突進 ---
        else if (animPhase_ == 4) {
            animTimer_ += deltaTime;
            float duration = 1.5f;
            float t = std::min (animTimer_ / duration, 1.0f);
            float easedT = std::pow (t, 4.0f);

            SetTranslate (Math::Lerp (animStartPos_, animTargetPos_, easedT));

            float totalRotation = std::numbers::pi_v<float> * 2.0f * 5.0f;
            SetRotation ({ easedT * totalRotation, GetRotation ().y, GetRotation ().z });
            // ★ 修正箇所3：ボスの突進回転（オイラー角）を優先させる
            GetTransform ()->isQuaternionMaster = false;

            if (t >= 1.0f) {
                animPhase_ = 5;
                animTimer_ = 0.0f;
            }
        }
        // --- フェーズ5: 自動リセット ---
        else if (animPhase_ == 5) {
            animPhase_ = 0;
            attackMode_ = 0; // モードもリセット
            animTimer_ = 0.0f;
        }
    }

    // ======================================
    // 攻撃モード2：ブロック射撃 (Phase 10 ~ 12)
    // ======================================
    else if (attackMode_ == 2) {

        // --- Phase 10: X = 50.0f へ移動 ---
        if (animPhase_ == 10) {
            if (animTimer_ == 0.0f) animStartPos_ = GetTranslate ();
            animTimer_ += deltaTime;
            float t = std::min (animTimer_ / 1.5f, 1.0f);

            Vector3 pos = GetTranslate ();
            pos.x = Math::Lerp (animStartPos_.x, 50.0f, Easing::OutExpo (t));
            SetTranslate (pos);

            if (t >= 1.0f) {
                animPhase_ = 11;
                animTimer_ = 0.0f;
                shotCount_ = 0;
                shotInterval_ = 0.0f;
            }
        }
        // --- Phase 11: プレイヤーを向いて、1つずつ飛ばす ---
        else if (animPhase_ == 11) {
            animTimer_ += deltaTime;
            shotInterval_ += deltaTime;

            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition () - GetWorldPosition ();
                float angleY = std::atan2 (toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation ({ GetRotation ().x, angleY, GetRotation ().z });
                // ★ 修正箇所4：ボスの向き（オイラー角）を優先させる
                GetTransform ()->isQuaternionMaster = false;
            }

            if (shotInterval_ >= 0.5f) {
                shotInterval_ = 0.0f;
                shotCount_++;

                if (!armorBlocks_.empty ()) {
                    Object3d *block = armorBlocks_.back ();
                    armorBlocks_.pop_back ();

                    Vector3 spawnPos = block->GetWorldPosition ();
                    block->SetScale ({ 0.0f, 0.0f, 0.0f });

                    if (target_) {
                        Vector3 targetPos = target_->GetWorldPosition ();
                        static Math math;
                        Vector3 dir = math.Normalize (targetPos - spawnPos);
                        float bulletSpeed = 40.0f;
                        Vector3 velocity = { dir.x * bulletSpeed, dir.y * bulletSpeed, dir.z * bulletSpeed };

                        // DebugConsole::GetInstance()->AddLog("Block Detached and Fired!\n");
                        // 弾を出す処理をここに書く
                    }
                }

                if (shotCount_ >= 5 || armorBlocks_.empty ()) {
                    animPhase_ = 12;
                    animTimer_ = 0.0f;
                }
            }
        }
        // --- Phase 12: 撃ち終わった後の隙（硬直） ---
        else if (animPhase_ == 12) {
            animTimer_ += deltaTime;
            if (animTimer_ >= 1.0f) {
                animPhase_ = 0;
                attackMode_ = 0;
                animTimer_ = 0.0f;
            }
        }
    }
}

