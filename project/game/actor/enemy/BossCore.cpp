#include "BossCore.h"
#include "InputManager.h"
#include "imgui.h"
#include "easing.h" // 追加
#include "DebugConsole.h"

// =================================================================
// 初期化・更新
// =================================================================

void BossCore::Initialize (Object3dCommon *common, const std::string &modelName) {
    // 親クラス(BaseEnemy)の初期化
    BaseEnemy::Initialize (common, modelName);

    // 演出・攻撃パターン管理用ディレクターの生成
    director_ = std::make_unique<GhostDirector> ();
    if (sceneManager_) {
        director_->Initialize (sceneManager_);
    }
}

void BossCore::Update (float deltaTime) {
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

void BossCore::ChangeState (State nextState) {
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
        int nextAttack = rand () % 10 + 1;
        std::string attackName = "boss_attack_" + std::to_string (nextAttack);

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

void BossCore::UpdateIdle (float deltaTime) {
    // 待機シナリオが終了したら、攻撃ステートへ移行
    if (director_ && director_->IsFinished ()) {
        ChangeState (State::Attack);
    }
}

void BossCore::UpdateAttack (float deltaTime) {
    if (!director_) return;

    // シナリオ内で発生したイベント(トリガー)を取得
    ActiveEvent eventInfo = director_->GetActiveEvent ();

    if (eventInfo.id != 0 && eventInfo.targetObject) {
        // イベント発生元のワールド座標を取得 (弾やエフェクトの発生位置として使用)
        Vector3 spawnPos = eventInfo.targetObject->GetWorldPosition ();

        if (eventInfo.id == 1) {
            // イベントID 1 の処理 (例: 斬撃エフェクト生成など)
        } else if (eventInfo.id == 2) {
            // イベントID 2 の処理 (例: 飛び道具の発射など)
        }
    }

    // 攻撃シナリオが終了したら、弱点露出ステートへ移行
    if (director_->IsFinished ()) {
        ChangeState (State::Weak);
    }
}

void BossCore::UpdateWeak (float deltaTime) {
    // 弱点露出シナリオが終了したら、待機ステートへ戻る
    if (director_ && director_->IsFinished ()) {
        ChangeState (State::Idle);
    }
}

void BossCore::UpdateAnimationSequence(float deltaTime) {

    // ==========================================
    // ゲームが再生中(Play)でなければ、この先のアニメーション・入力処理を一切行わない！
    // ==========================================
    if (!SceneManager::GetInstance()->IsPlaying()) {
        return; // 再生中でなければ操作を受け付けない
    }

    InputManager* input = InputManager::GetInstance();

    // ======================================
    // フェーズ0: 入力待ち（待機）
    // ======================================
    if (animPhase_ == 0) {
        if (input->IsKeyTriggered(DIK_1)) {
            // モード1：形態変化からの突進
            attackMode_ = 1;
            animPhase_ = 1; // ★ 変形フェーズからスタート！
            animTimer_ = 0.0f;

            // --- 形態変化の準備（座標の計算と記憶） ---
            blockStartPos_.clear();
            blockTargetPos_.clear();

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

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                // 移動のスタート地点を記憶
                blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());

                if (i < settings.size()) {
                    // ゴール地点を記憶 (Phase 1 でここに向かって Lerp します)
                    blockTargetPos_.push_back(settings[i].translate);

                    // 大きさと回転は、変形開始と同時に適用してしまう！
                    armorBlocks_[i]->SetScale(settings[i].scale);
                    armorBlocks_[i]->SetRotation(settings[i].rotation);
                    // ★ 修正箇所1：ブロックのオイラー角(XYZ)を優先させる
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
                else {
                    blockTargetPos_.push_back({ 0.0f, 0.0f, 0.0f });
                }
            }
        }
        else if (input->IsKeyTriggered(DIK_2)) {
            // モード2：ブロック射撃
            attackMode_ = 2;
            animPhase_ = 10; // 射撃用フェーズへ
            animTimer_ = 0.0f;
        }
        // ==========================================
        // 3キーで「こぶし落下攻撃」を発動！
        // ==========================================
        else if (input->IsKeyTriggered(DIK_3)) {
            attackMode_ = 3;
            animPhase_ = 20; // フェーズ20からスタート
            animTimer_ = 0.0f;
        }
        // ==========================================
        // 4キーで「モーション4：巨大な壁の横断攻撃」を発動！
        // ==========================================
        else if (input->IsKeyTriggered(DIK_4)) {
            attackMode_ = 4;

            // ★ 修正：いきなり移動せず、新設する「準備フェーズ(39)」へ！
            animPhase_ = 39;
            animTimer_ = 0.0f;

            // ★ 新規：コンボの回数を管理するために shotCount_ を再利用！
            // 0=前方攻撃, 1=右側攻撃, 2=左側攻撃
            shotCount_ = 0;
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
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t); // カッコよくスライドさせる

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                    Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                }
            }

            if (t >= 1.0f) {
                animPhase_ = 2; // 変形が終わったら、突進準備(X=-50)へ！
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
                // ★ 修正箇所2：ボスの向き（オイラー角）を優先させる
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
            // ★ 修正箇所3：ボスの突進回転（オイラー角）を優先させる
            GetTransform()->isQuaternionMaster = false;

            if (t >= 1.0f) {
                animPhase_ = 5;
                animTimer_ = 0.0f;
            }
        }
        // --- フェーズ5: その場でゆっくり装甲を再構成する ---
        else if (animPhase_ == 5) {

            // ==========================================
            // ★ 修正：現在のブロックの位置を `blockStartPos_` に上書き記憶させる！
            // ==========================================
            if (animTimer_ == 0.0f) {
                blockStartPos_.clear();
                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                }
            }

            animTimer_ += deltaTime;
            float duration = 3.0f; // 3秒かけてゆっくり戻る
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            // ボスの回転だけは 0 にリセット（直立姿勢へ）
            SetRotation({ 0.0f, 0.0f, 0.0f });
            GetTransform()->isQuaternionMaster = false;

            // ブロックを元の装甲の形にゆっくり戻す
            struct DefaultSetting { Vector3 translate; Vector3 scale; Vector3 rotation; };
            std::vector<DefaultSetting> defaultSettings = {
                { {-3.500f,  0.000f, 0.000f}, {0.500f, 0.500f, 0.500f}, {0.0f, 0.0f, 0.0f} },
                { {-2.000f,  0.000f, 0.000f}, {1.000f, 1.000f, 1.647f}, {0.0f, 0.0f, 0.0f} },
                { { 0.000f,  1.510f, 0.000f}, {2.000f, 0.506f, 1.625f}, {0.0f, 0.0f, 0.0f} },
                { { 0.000f, -1.504f, 0.000f}, {2.000f, 0.511f, 1.665f}, {0.0f, 0.0f, 0.0f} },
                { { 2.000f,  0.000f, 0.000f}, {1.000f, 1.000f, 1.659f}, {0.0f, 0.0f, 0.0f} },
                { { 3.500f,  0.000f, 0.000f}, {0.500f, 0.500f, 0.500f}, {0.0f, 0.0f, 0.0f} }
            };

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size() && i < defaultSettings.size()) {
                    Vector3 pos = Math::Lerp(blockStartPos_[i], defaultSettings[i].translate, easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                    armorBlocks_[i]->SetScale(defaultSettings[i].scale);
                    armorBlocks_[i]->SetRotation(defaultSettings[i].rotation);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }

            // ★ 完全に元に戻ったら、状態をすべてクリアして待機(Idle)へ！
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

            // 移動中も常にプレイヤーの方を向く！
            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
                float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation({ GetRotation().x, angleY, GetRotation().z });
                GetTransform()->isQuaternionMaster = false;
            }

            // 移動が終わったら、次の「陣形変化」の準備をする！
            if (t >= 1.0f) {
                animPhase_ = 11;
                animTimer_ = 0.0f;

                // --- 射撃用の陣形データ（座標・スケール・回転） ---
                blockStartPos_.clear();
                blockTargetPos_.clear();

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

                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());

                    if (i < settings.size()) {
                        blockTargetPos_.push_back(settings[i].translate);
                        armorBlocks_[i]->SetScale(settings[i].scale);
                        armorBlocks_[i]->SetRotation(settings[i].rotation);
                        // 回転オーバーライド（クォータニオン無効化）
                        armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                    }
                    else {
                        blockTargetPos_.push_back({ 0.0f, 0.0f, 0.0f });
                    }
                }
            }
        }
        // --- Phase 11: 射撃陣形へスライド移動（カシャッ！） ---
        else if (animPhase_ == 11) {
            animTimer_ += deltaTime;
            float duration = 1.0f; // 1秒かけて陣形を変える
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                    Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                }
            }

            // 変形中も常にプレイヤーの方を向く！
            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
                float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation({ GetRotation().x, angleY, GetRotation().z });
                GetTransform()->isQuaternionMaster = false;
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

                    // 今のブロックの回転を維持
                    Vector3 currentRot = block->GetRotation();
                    block->GetTransform()->isQuaternionMaster = false;

                    // ==========================================
                    // ★ 修正：いきなり飛ばさず、「モード4 (頭上へ装填中)」にする！
                    // 速度(velocity)は一旦 {0,0,0} で登録します。
                    // ==========================================
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
            // ★ 超重要：飛んでいるブロックが「すべて」戻ってくるまで待つ！
            if (flyingBlocks_.empty()) {
                animTimer_ += deltaTime;
                if (animTimer_ >= 1.0f) { // すべて戻ってきてから1秒の隙を晒す
                    animPhase_ = 0;
                    attackMode_ = 0;
                    animTimer_ = 0.0f;
                }
            }
        }
    }
    // ======================================
    // 攻撃モード3：ハンマー合体 ＆ 目の前で叩き潰す！（完全修正版）
    // ======================================
    else if (attackMode_ == 3) {

        // --- Phase 20: 瞬時にハンマー形態へ変形
        if (animPhase_ == 20) {
            if (animTimer_ == 0.0f) {
                struct HammerSetting {
                    Vector3 translate; Vector3 scale; Vector3 rotation;
                };

                // 画像から抽出したハンマーの数値データ
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

            // ==========================================
            // 1.5秒タメ終わった瞬間に「目標地点」をロックオン（記憶）する！
            // ==========================================
            if (animTimer_ >= 1.5f) {
                animPhase_ = 21;
                animTimer_ = 0.0f;

                if (target_) {
                    animTargetPos_ = target_->GetWorldPosition(); // プレイヤーの現在地を記憶！
                }
                else {
                    animTargetPos_ = GetTranslate();
                }
            }
        }
        // --- Phase 21: ロックオンした位置（記憶した座標）へ移動 ＆ 振りかぶる！ ---
        else if (animPhase_ == 21) {
            animTimer_ += deltaTime;

            // ==========================================
            // ★ 修正：移動と振りかぶりの「時間」を完全に分離！
            // ==========================================
            float moveDuration = 4.5f; // 近づくのにかける時間（ゆっくりジリジリ）
            float rotDuration = 3.0f; // 振りかぶるのにかける時間（元のキレをキープ！）

            // それぞれの進行度 (0.0 ～ 1.0) を別々に計算
            float moveT = std::min(animTimer_ / moveDuration, 1.0f);
            float rotT = std::min(animTimer_ / rotDuration, 1.0f);

            // ------------------------------------------
            // 1. 移動の処理（moveT を使う）
            // ------------------------------------------
            Vector3 targetPos = animTargetPos_;
            Vector3 currentPos = GetTranslate();

            Vector3 toBoss = currentPos - targetPos;
            toBoss.y = 0.0f;
            float dist = std::sqrt(toBoss.x * toBoss.x + toBoss.z * toBoss.z);
            if (dist > 0.0f) { toBoss.x /= dist; toBoss.z /= dist; }

            Vector3 targetHoverPos = { targetPos.x + toBoss.x * 4.5f, targetPos.y + 1.0f, targetPos.z + toBoss.z * 4.5f };

            // 瞬間移動感をなくすため、一定の速度(moveT)でヌルッと近づかせる
            float easeT = moveT;
            currentPos.x = Math::Lerp(currentPos.x, targetHoverPos.x, easeT);
            currentPos.y = Math::Lerp(currentPos.y, targetHoverPos.y, easeT);
            currentPos.z = Math::Lerp(currentPos.z, targetHoverPos.z, easeT);
            SetTranslate(currentPos);

            // ------------------------------------------
            // 2. 回転（振りかぶり）の処理（rotT を使う）
            // ------------------------------------------
            Vector3 toPlayer = targetPos - currentPos;
            float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);

            // easeInElastic の計算も rotT を基準に行う
            float elasticT = 0.0f;
            if (rotT == 0.0f) {
                elasticT = 0.0f;
            }
            else if (rotT == 1.0f) {
                elasticT = 1.0f;
            }
            else {
                float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;
                elasticT = -std::pow(2.0f, 10.0f * rotT - 10.0f) * std::sin((rotT * 10.0f - 10.75f) * c4);
            }

            // 目標の振りかぶり角度（お好みで変更可能）
            float targetTilt = -70.0f * (std::numbers::pi_v<float> / 180.0f);

            // elasticT を使って回転させる！
            float tiltBack = Math::Lerp(0.0f, targetTilt, elasticT);

            SetRotation({ 0.0f, angleY, tiltBack });
            GetTransform()->isQuaternionMaster = false;

            // ------------------------------------------
            // 3. 次のフェーズへの移行
            // ------------------------------------------
            // 「移動」が終わったら叩きつける！(moveDuration基準)
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

            // ==========================================
            // ★ 修正：倒れ込む方向を逆にする
            // ==========================================
            // 振りかぶり角度（例として -70度 まで大きくのけぞるように変更！）
            float startRotZ = -70.0f * (std::numbers::pi_v<float> / 180.0f);

            // 振り下ろし角度（例として 120度 まで深くめり込むように変更！）
            float endRotZ = 270.0f * (std::numbers::pi_v<float> / 180.0f);

            float currentRotZ = Math::Lerp(startRotZ, endRotZ, std::pow(t, 3.0f));
            SetRotation({ 0.0f, GetRotation().y, currentRotZ });

            if (t >= 1.0f) {
                SetRotation({ 0.0f, GetRotation().y, endRotZ });
                animPhase_ = 23;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 23: 地面に倒れたまま3秒待機 ---
        else if (animPhase_ == 23) {

            // ★ 修正：Phase 23 に入った最初の1フレーム目だけ、角度を記憶する！
            if (animTimer_ == 0.0f) {
                animStartPos_ = GetRotation(); // ボスの全回転角度を記憶
            }

            animTimer_ += deltaTime;

            if (animTimer_ >= 3.0f) {
                animPhase_ = 24; // ★ 修正：24へ
                animTimer_ = 0.0f;

            }
        }
        // --- Phase 24: ボスの姿勢と装甲が元の形にシュッと戻る ---
        else if (animPhase_ == 24) {

            // ==========================================
            // ★ 修正：ここでも記憶処理を上書きしてしまっていたので削除！
            // 最初のフレームで「現在のブロックの位置」を正しく記憶させます
            // ==========================================
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

            // 1. ボス自身を元の高さに戻す
            Vector3 bossPos = GetTranslate();
            bossPos.y = Math::Lerp(bossPos.y, 4.0f, easeT);
            SetTranslate(bossPos);

            // 記憶した角度(animStartPos_)から、完全に 0.0f へ戻す！
            Vector3 currentRot;
            currentRot.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
            currentRot.y = Math::Lerp(animStartPos_.y, 0.0f, easeT);
            currentRot.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
            SetRotation(currentRot);

            // 他の回転処理（ターゲット追従など）に上書きされないよう、マスター権限を奪う！
            GetTransform()->isQuaternionMaster = false;

            // 3. ブロックも元の完璧な装甲の形に戻す
            struct DefaultSetting { Vector3 translate; Vector3 scale; Vector3 rotation; };
            std::vector<DefaultSetting> defaultSettings = {
                { {-3.500f,  0.000f, 0.000f}, {0.500f, 0.500f, 0.500f}, {0.0f, 0.0f, 0.0f} },
                { {-2.000f,  0.000f, 0.000f}, {1.000f, 1.000f, 1.647f}, {0.0f, 0.0f, 0.0f} },
                { { 0.000f,  1.510f, 0.000f}, {2.000f, 0.506f, 1.625f}, {0.0f, 0.0f, 0.0f} },
                { { 0.000f, -1.504f, 0.000f}, {2.000f, 0.511f, 1.665f}, {0.0f, 0.0f, 0.0f} },
                { { 2.000f,  0.000f, 0.000f}, {1.000f, 1.000f, 1.659f}, {0.0f, 0.0f, 0.0f} },
                { { 3.500f,  0.000f, 0.000f}, {0.500f, 0.500f, 0.500f}, {0.0f, 0.0f, 0.0f} }
            };

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size() && i < defaultSettings.size()) {
                    Vector3 pos = Math::Lerp(blockStartPos_[i], defaultSettings[i].translate, easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                    armorBlocks_[i]->SetScale(defaultSettings[i].scale);
                    armorBlocks_[i]->SetRotation(defaultSettings[i].rotation);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }

            // 完全に元に戻ったら、状態をすべてクリア！
            if (t >= 1.0f) {
                animPhase_ = 0;
                attackMode_ = 0;
                animTimer_ = 0.0f;
            }
        }
    }
    // ======================================
    // 攻撃モード4：絶望の3連撃ギガ・ウォール！
    // ======================================
    else if (attackMode_ == 4) {

        // --- Phase 39: 【新設】コンボ数に応じた壁の配置計算 ---
        if (animPhase_ == 39) {
            blockStartPos_.clear();
            blockTargetPos_.clear();
            float blockWidth = 12.5f;

            Vector3 bossCurrentPos = GetTranslate();
            animStartPos_ = bossCurrentPos; // 移動のスタート地点を記憶

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                // 最初の攻撃(0回目)の時だけ、親子関係を解除してワールド座標にする！
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
                    // 2回目以降はすでにワールド座標にいるので、そのまま現在地をスタートに！
                    blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                }

                // ==========================================
                // ★ ここで何回目の攻撃かによって、壁の場所と形を変える！
                // ==========================================
                float offset = -(i - 2.5f) * blockWidth;
                Vector3 targetPos;

                if (shotCount_ == 0) {
                    // 【1撃目：前方から】奥(Z=75)に、X軸に並べる
                    targetPos = { offset, 2.0f, 75.0f };
                    armorBlocks_[i]->SetScale({ blockWidth, 4.0f, 1.0f });
                }
                else if (shotCount_ == 1) {
                    // 【2撃目：右側面から】右(X=75)に、Z軸に並べる
                    targetPos = { 75.0f, 2.0f, offset };
                    armorBlocks_[i]->SetScale({ 1.0f, 4.0f, blockWidth });
                }
                else if (shotCount_ == 2) {
                    // 【3撃目：左側面から】左(X=-75)に、Z軸に並べる
                    targetPos = { -75.0f, 2.0f, offset };
                    armorBlocks_[i]->SetScale({ 1.0f, 4.0f, blockWidth });
                }

                blockTargetPos_.push_back(targetPos);
                armorBlocks_[i]->SetRotation({ 0.0f, 0.0f, 0.0f });
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }

            animPhase_ = 40;
            animTimer_ = 0.0f;
        }
        // --- Phase 40: ボスが上空へ移動 ＆ ブロックが壁を形成 ---
        else if (animPhase_ == 40) {
            animTimer_ += deltaTime;
            float duration = 1.5f; // コンボ中はテンポよく1.5秒で壁を作る！
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            Vector3 bossPos = GetTranslate();

            // コンボに応じてボスの待機場所（上空）も変える！
            if (shotCount_ == 0) {
                bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
                bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
                bossPos.z = Math::Lerp(animStartPos_.z, 75.0f, easeT); // 奥の上空
            }
            else if (shotCount_ == 1) {
                bossPos.x = Math::Lerp(animStartPos_.x, 75.0f, easeT); // 右の上空
                bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
                bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
            }
            else if (shotCount_ == 2) {
                bossPos.x = Math::Lerp(animStartPos_.x, -75.0f, easeT); // 左の上空
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
        // --- Phase 41: 壁だけがステージを大横断！ ---
        else if (animPhase_ == 41) {
            animTimer_ += deltaTime;
            float duration = 2.5f; // コンボなので横断速度も少し早めに！
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = std::pow(t, 2.0f);

            // コンボに応じて動かす軸を変える！
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                Vector3 blockPos = armorBlocks_[i]->GetTranslate();

                if (shotCount_ == 0) {
                    blockPos.z = Math::Lerp(75.0f, -75.0f, easeT); // 奥から手前へ
                }
                else if (shotCount_ == 1) {
                    blockPos.x = Math::Lerp(75.0f, -75.0f, easeT); // 右から左へ
                }
                else if (shotCount_ == 2) {
                    blockPos.x = Math::Lerp(-75.0f, 75.0f, easeT); // 左から右へ
                }
                armorBlocks_[i]->SetTranslate(blockPos);
            }

            if (t >= 1.0f) {
                animPhase_ = 42;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 42: 攻撃後の猶予 ＆ コンボのループ判定！ ---
        else if (animPhase_ == 42) {
            animTimer_ += deltaTime;

            // 攻撃が終わったら 0.5秒 だけ隙を見せる
            if (animTimer_ >= 0.5f) {
                shotCount_++; // コンボカウントを進める！

                if (shotCount_ < 3) {
                    // ★ まだ3回終わってないなら、準備フェーズ(39)へループ！！
                    animPhase_ = 39;
                }
                else {
                    // ★ 3回終わったら、修復フェーズ(43)へ！
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

            // ボス本体を中央の定位置へ戻す！
            Vector3 bossPos = GetTranslate();
            bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
            bossPos.y = Math::Lerp(animStartPos_.y, 4.0f, easeT);
            bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
            SetTranslate(bossPos);

            SetRotation({ 0.0f, 0.0f, 0.0f });
            GetTransform()->isQuaternionMaster = false;

            struct DefaultSetting { Vector3 translate; Vector3 scale; Vector3 rotation; };
            std::vector<DefaultSetting> defaultSettings = {
                { {-3.500f,  0.000f, 0.000f}, {0.500f, 0.500f, 0.500f}, {0.0f, 0.0f, 0.0f} },
                { {-2.000f,  0.000f, 0.000f}, {1.000f, 1.000f, 1.647f}, {0.0f, 0.0f, 0.0f} },
                { {  0.000f,  1.510f, 0.000f}, {2.000f, 0.506f, 1.625f}, {0.0f, 0.0f, 0.0f} },
                { {  0.000f, -1.504f, 0.000f}, {2.000f, 0.511f, 1.665f}, {0.0f, 0.0f, 0.0f} },
                { {  2.000f,  0.000f, 0.000f}, {1.000f, 1.000f, 1.659f}, {0.0f, 0.0f, 0.0f} },
                { {  3.500f,  0.000f, 0.000f}, {0.500f, 0.500f, 0.500f}, {0.0f, 0.0f, 0.0f} }
            };

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < blockStartPos_.size() && i < defaultSettings.size()) {
                    Vector3 pos = Math::Lerp(blockStartPos_[i], defaultSettings[i].translate, easeT);
                    armorBlocks_[i]->SetTranslate(pos);
                    armorBlocks_[i]->SetScale(defaultSettings[i].scale);
                    armorBlocks_[i]->SetRotation(defaultSettings[i].rotation);
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

                    // ★ タイクラーさん仕様：プレイヤーの「足元（地面）」を直接狙う！
                    targetPos.y = 0.0f;

                    Vector3 toPlayer = math.Normalize (targetPos - headPos);

                    // 初速を少し速め(60.0f)にして鋭く飛ばす！
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

                // 移動中も少し回転させておく
                fb.currentRot.x += 15.0f * deltaTime;
                fb.currentRot.y += 30.0f * deltaTime;
                fb.block->SetRotation (fb.currentRot);
            }
            fb.block->GetTransform ()->isQuaternionMaster = false;
        }

        // ==========================================
        // モード0（飛翔中・攻撃）
        // ==========================================
        else if (fb.mode == 0) {
            // --- 攻撃中（直線的に足元へ突撃！） ---

            // ★ タイクラーさん仕様：重力の計算はしない！（直線レーザー）

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
                float returnSpeed = 60.0f; // 帰りは超高速で引き戻す！
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
        returnDelayTimer_ += deltaTime;
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

            int idx = it->originalIndex;
            if (idx >= 0 && idx < defaultSettings.size ()) {
                it->block->SetTranslate (defaultSettings[idx].translate);
                it->block->SetScale (defaultSettings[idx].scale);
                it->block->SetRotation (defaultSettings[idx].rotation);
            }

            it->block->GetTransform ()->isQuaternionMaster = false;
            it = flyingBlocks_.erase (it);
        } else {
            ++it;
        }
    }
}
