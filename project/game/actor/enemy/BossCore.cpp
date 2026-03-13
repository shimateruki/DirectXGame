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

    // ==========================================
    // ★ 飛んでいるブロックの更新
    // ==========================================
    UpdateFlyingBlocks (deltaTime);

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
        //director_->LoadScenario("boss_attack_1");
        //director_->PlayScenario(false, false);
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
    // 攻撃モード2：移動 → 陣形変化 → ブロック射撃 (Phase 10 ~ 13)
    // ======================================
    else if (attackMode_ == 2) {

        // --- Phase 10: X = 50.0f へ移動 ---
        if (animPhase_ == 10) {
            if (animTimer_ == 0.0f) animStartPos_ = GetTranslate ();
            animTimer_ += deltaTime;
            float t = std::min (animTimer_ / 2.5f, 1.0f);

            Vector3 pos = GetTranslate ();
            pos.x = Math::Lerp (animStartPos_.x, 50.0f, Easing::OutExpo (t));
            SetTranslate (pos);

            // 移動中も常にプレイヤーの方を向く！
            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition () - GetWorldPosition ();
                float angleY = std::atan2 (toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation ({ GetRotation ().x, angleY, GetRotation ().z });
                GetTransform ()->isQuaternionMaster = false;
            }

            // 移動が終わったら、次の「陣形変化」の準備をする！
            if (t >= 1.0f) {
                animPhase_ = 11;
                animTimer_ = 0.0f;

                // --- 射撃用の陣形データ（座標・スケール・回転） ---
                blockStartPos_.clear ();
                blockTargetPos_.clear ();

                struct BlockSetting {
                    Vector3 translate;
                    Vector3 scale;
                    Vector3 rotation;
                };

                // ★ ここが「射撃モードの時のブロックの形」です！
                // とりあえず「ボスの前方に円を描くように並んで砲口を向ける」ような仮の数値をいれています。
                // タイクラーさんのお好みで、モード1と同じようにカッコいい陣形に書き換えてください！
                // 90度（π/2）をラジアンで定義（ブロック自体の向きを正面に合わせる用）
                float turnY = std::numbers::pi_v<float> / 2.0f;

                // ★ ここが「射撃モードの時のブロックの形」です！
                std::vector<BlockSetting> settings = {
                    // { { X(前後), Y(上下), Z(左右) }, { スケール }, { 回転(XYZ) } }
                    { { -2.0f,  2.5f,  0.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } }, // 上
                    { { -2.0f,  1.0f, -2.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } }, // 左上
                    { { -2.0f,  1.0f,  2.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } }, // 右上
                    { { -2.0f, -1.0f, -2.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } }, // 左下
                    { { -2.0f, -1.0f,  2.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } }, // 右下
                    { { -2.0f, -2.5f,  0.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, turnY, 0.0f } }  // 下
                };

                for (size_t i = 0; i < armorBlocks_.size (); ++i) {
                    blockStartPos_.push_back (armorBlocks_[i]->GetTranslate ());

                    if (i < settings.size ()) {
                        blockTargetPos_.push_back (settings[i].translate);
                        armorBlocks_[i]->SetScale (settings[i].scale);
                        armorBlocks_[i]->SetRotation (settings[i].rotation);
                        // 回転オーバーライド（クォータニオン無効化）
                        armorBlocks_[i]->GetTransform ()->isQuaternionMaster = false;
                    } else {
                        blockTargetPos_.push_back ({ 0.0f, 0.0f, 0.0f });
                    }
                }
            }
        }
        // --- Phase 11: 射撃陣形へスライド移動（カシャッ！） ---
        else if (animPhase_ == 11) {
            animTimer_ += deltaTime;
            float duration = 1.0f; // 1秒かけて陣形を変える
            float t = std::min (animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo (t);

            for (size_t i = 0; i < armorBlocks_.size (); ++i) {
                if (i < blockStartPos_.size () && i < blockTargetPos_.size ()) {
                    Vector3 pos = Math::Lerp (blockStartPos_[i], blockTargetPos_[i], easeT);
                    armorBlocks_[i]->SetTranslate (pos);
                }
            }

            // 変形中も常にプレイヤーの方を向く！
            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition () - GetWorldPosition ();
                float angleY = std::atan2 (toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation ({ GetRotation ().x, angleY, GetRotation ().z });
                GetTransform ()->isQuaternionMaster = false;
            }

            // 陣形が完成したら、いよいよ射撃開始！
            if (t >= 1.0f) {
                animPhase_ = 12; // 射撃フェーズへ
                animTimer_ = 0.0f;
                shotCount_ = 0;
                shotInterval_ = 0.0f;
            }
        }
        // --- Phase 12: プレイヤーを向いて、1つずつ飛ばす ---
        else if (animPhase_ == 12) {
            animTimer_ += deltaTime;

            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition () - GetWorldPosition ();
                float angleY = std::atan2 (toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation ({ GetRotation ().x, angleY, GetRotation ().z });
                GetTransform ()->isQuaternionMaster = false;
            }

            float nextShotTime = shotCount_ * 0.5f;

            if (animTimer_ >= nextShotTime) {
                int idx = (int)armorBlocks_.size () - 1 - shotCount_;
                if (idx >= 0 && idx < armorBlocks_.size ()) {
                    Object3d *block = armorBlocks_[idx];

                    Vector3 bossPos = GetTranslate ();
                    float bossRotY = GetRotation ().y;
                    Vector3 localPos = block->GetTranslate ();

                    Vector3 worldPos;
                    worldPos.x = bossPos.x + (localPos.x * std::cos (bossRotY) + localPos.z * std::sin (bossRotY));
                    worldPos.y = bossPos.y + localPos.y;
                    worldPos.z = bossPos.z + (-localPos.x * std::sin (bossRotY) + localPos.z * std::cos (bossRotY));

                    block->SetParent (nullptr);
                    block->SetTranslate (worldPos);

                    // 今のブロックの回転を維持
                    Vector3 currentRot = block->GetRotation ();
                    block->GetTransform ()->isQuaternionMaster = false;

                    // ==========================================
                    // ★ 修正：いきなり飛ばさず、「モード4 (頭上へ装填中)」にする！
                    // 速度(velocity)は一旦 {0,0,0} で登録します。
                    // ==========================================
                    flyingBlocks_.push_back ({ block, {0.0f, 0.0f, 0.0f}, currentRot, 4, idx });
                }

                shotCount_++;

                if (shotCount_ >= armorBlocks_.size ()) {
                    animPhase_ = 13;
                    animTimer_ = 0.0f;
                }
            }
        } else if (animPhase_ == 13) {
            // ★ 超重要：飛んでいるブロックが「すべて」戻ってくるまで待つ！
            if (flyingBlocks_.empty ()) {
                animTimer_ += deltaTime;
                if (animTimer_ >= 1.0f) { // すべて戻ってきてから1秒の隙を晒す
                    animPhase_ = 0;
                    attackMode_ = 0;
                    animTimer_ = 0.0f;
                }
            }
        }
    }
}

void BossCore::UpdateFlyingBlocks (float deltaTime) {
    int landedCount = 0; // 地面に刺さっているブロックの数
    static Math math;

    // ==========================================
    // 1. 各ブロックの移動・回転・状態更新
    // ==========================================
    for (auto &fb : flyingBlocks_) {
        if (!fb.block) continue;

        // ==========================================
        // モード4（頭上へ装填中）
        // ==========================================
        if (fb.mode == 4) {
            Vector3 bossPos = GetTranslate ();
            // コアの頭上（Y + 4.0f 付近）を目標地点にする
            Vector3 headPos = { bossPos.x, bossPos.y + 4.0f, bossPos.z };
            Vector3 currentPos = fb.block->GetTranslate ();

            Vector3 dir = { headPos.x - currentPos.x, headPos.y - currentPos.y, headPos.z - currentPos.z };
            float distance = std::sqrt (dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (distance < 0.5f) {
                // --- 頭上に到着！装填完了！ ---
                fb.block->SetTranslate (headPos);

                // ここで初めてプレイヤーへの方向を計算して「ドカン！」と撃ち出す！
                if (target_) {
                    Vector3 targetPos = target_->GetWorldPosition ();

                    // ==========================================
                    // ★ 修正1：プレイヤーの「足元（地面）」を直接狙う！
                    // ==========================================
                    targetPos.y = 0.0f; // 確実に地面の座標をロックオン！

                    Vector3 toPlayer = math.Normalize (targetPos - headPos);

                    // 重力がなくなったので、初速を少し速め(60.0fなど)にすると鋭く飛んでカッコいいです！
                    float bulletSpeed = 60.0f;
                    fb.velocity = { toPlayer.x * bulletSpeed, toPlayer.y * bulletSpeed, toPlayer.z * bulletSpeed };

                    float angleY = std::atan2 (toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                    fb.currentRot = { 0.0f, angleY, 0.0f };
                }
                fb.mode = 0; // 「飛翔モード」へ移行！
            } else {
                // --- 頭上に向かって移動中（シュッ！） ---
                dir.x /= distance; dir.y /= distance; dir.z /= distance;
                float gatherSpeed = 30.0f; // 頭上に移動するスピード
                currentPos.x += dir.x * gatherSpeed * deltaTime;
                currentPos.y += dir.y * gatherSpeed * deltaTime;
                currentPos.z += dir.z * gatherSpeed * deltaTime;
                fb.block->SetTranslate (currentPos);

                // 移動中も少し回転させておくとカッコいいです
                fb.currentRot.x += 15.0f * deltaTime;
                fb.currentRot.y += 30.0f * deltaTime;
                fb.block->SetRotation (fb.currentRot);
            }
            fb.block->GetTransform ()->isQuaternionMaster = false;
        }
        if (fb.mode == 0) {
            // --- 攻撃中（直線的に足元へ突撃！） ---

            // ==========================================
            // ★ 修正2：タイクラーさんの直感通り、重力の計算を完全に削除！
            // fb.velocity.y -= 40.0f * deltaTime;  ←これを消す！
            // ==========================================

            Vector3 pos = fb.block->GetTranslate ();
            pos.x += fb.velocity.x * deltaTime;
            pos.y += fb.velocity.y * deltaTime;
            pos.z += fb.velocity.z * deltaTime;

            // 地面（Y=0.0f）にぶつかったら刺さって止まる
            if (pos.y <= 0.0f) {
                pos.y = 0.0f;
                fb.velocity = { 0.0f, 0.0f, 0.0f }; // 速度リセット
                fb.mode = 1; // 地面待機モードへ！
            }
            fb.block->SetTranslate (pos);

            // 乱回転
            Vector3 spinSpeed = { 30.0f, 45.0f, 60.0f };
            fb.currentRot.x += spinSpeed.x * deltaTime;
            fb.currentRot.y += spinSpeed.y * deltaTime;
            fb.currentRot.z += spinSpeed.z * deltaTime;
            fb.block->SetRotation (fb.currentRot);
            fb.block->GetTransform ()->isQuaternionMaster = false;
        } else if (fb.mode == 1) {
            // --- 地面待機中 ---
            landedCount++; // 地面にある数をカウントする
        } else if (fb.mode == 2) {
            // --- ボスへ帰還中 ---
            Vector3 bossPos = GetTranslate ();
            Vector3 blockPos = fb.block->GetTranslate ();

            // ボスとの距離と方向を計算
            Vector3 dir = { bossPos.x - blockPos.x, bossPos.y - blockPos.y, bossPos.z - blockPos.z };
            float distance = std::sqrt (dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (distance < 2.0f) {
                fb.mode = 3; // ボスに十分近づいたら回収完了！
            } else {
                // 正規化してボスの方向へ進む
                dir.x /= distance; dir.y /= distance; dir.z /= distance;
                float returnSpeed = 60.0f; // ★帰りは超高速で引き戻す！
                blockPos.x += dir.x * returnSpeed * deltaTime;
                blockPos.y += dir.y * returnSpeed * deltaTime;
                blockPos.z += dir.z * returnSpeed * deltaTime;
                fb.block->SetTranslate (blockPos);

                // 帰りも回転させる
                Vector3 spinSpeed = { 60.0f, 60.0f, 60.0f };
                fb.currentRot.x += spinSpeed.x * deltaTime;
                fb.currentRot.y += spinSpeed.y * deltaTime;
                fb.currentRot.z += spinSpeed.z * deltaTime;
                fb.block->SetRotation (fb.currentRot);
                fb.block->GetTransform ()->isQuaternionMaster = false;
            }
        }
    }

    // ==========================================
    // 2. 「すべての弾が地面に落ちた」＆「全部撃ち終わった」なら3秒待って一斉帰還！
    // ==========================================
    if (!flyingBlocks_.empty () && landedCount == flyingBlocks_.size () && flyingBlocks_.size () == armorBlocks_.size ()) {

        // ★ 修正：いきなり帰還させず、まずはタイマーを進める！
        returnDelayTimer_ += deltaTime;

        // ★ 3秒（3.0f）経過したら帰還命令を出す！
        if (returnDelayTimer_ >= 5.0f) {
            for (auto &fb : flyingBlocks_) {
                fb.mode = 2; // 全員一斉に帰還モードへ
            }
            returnDelayTimer_ = 0.0f; // 次の攻撃のためにタイマーをリセットしておく
        }

    } else {
        // まだ条件を満たしていない時（攻撃中など）は、タイマーを確実に0にしておく
        returnDelayTimer_ = 0.0f;
    }

    // ==========================================
    // 3. 回収完了
    // ==========================================
    for (auto it = flyingBlocks_.begin (); it != flyingBlocks_.end (); ) {
        if (it->mode == 3) {
            it->block->SetParent (this);

            struct DefaultSetting {
                Vector3 translate;
                Vector3 scale;
                Vector3 rotation;
            };

            std::vector<DefaultSetting> defaultSettings = {
                { {-3.500f,  0.000f, 0.000f}, {0.500f, 0.500f, 0.500f}, {0.0f, 0.0f, 0.0f} },
                { {-2.000f,  0.000f, 0.000f}, {1.000f, 1.000f, 1.647f}, {0.0f, 0.0f, 0.0f} },
                { { 0.000f,  1.510f, 0.000f}, {2.000f, 0.506f, 1.625f}, {0.0f, 0.0f, 0.0f} },
                { { 0.000f, -1.504f, 0.000f}, {2.000f, 0.511f, 1.665f}, {0.0f, 0.0f, 0.0f} },
                { { 2.000f,  0.000f, 0.000f}, {1.000f, 1.000f, 1.659f}, {0.0f, 0.0f, 0.0f} },
                { { 3.500f,  0.000f, 0.000f}, {0.500f, 0.500f, 0.500f}, {0.0f, 0.0f, 0.0f} }
            };

            // ★ 修正：記憶していた「元々の定位置の番号」を使って絶対間違えないようにする！
            int idx = it->originalIndex;
            if (idx >= 0 && idx < defaultSettings.size ()) {
                it->block->SetTranslate (defaultSettings[idx].translate);
                it->block->SetScale (defaultSettings[idx].scale);
                it->block->SetRotation (defaultSettings[idx].rotation);
            }

            it->block->GetTransform ()->isQuaternionMaster = false;

            // ★ 削除：もう配列からは消していないので armorBlocks_.push_back() は書きません！

            it = flyingBlocks_.erase (it);
        } else {
            ++it;
        }
    }
}

