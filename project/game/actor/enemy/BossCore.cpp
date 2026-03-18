#include "BossCore.h"
#include "InputManager.h"
#include "imgui.h"
#include "easing.h" // 追加
#include "DebugConsole.h"
#include <cmath>
#include <numbers>

// =================================================================
// ★ 新規：待機アニメーション用のタイマーと軌道計算関数
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

        // ==========================================
        // ★ 新規：スケールをランダム生成（初回のみ計算して記憶させる！）
        // static を付けることで、関数を抜けても記憶が保持されます。
        // ==========================================
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

        // ==========================================
        // ★ 修正：記憶したランダムな大きさを適用する！
        // ==========================================
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
}

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
    float preTimer = colorResetTimer_;
    // 1. 基本更新（行列計算など）
    BaseEnemy::Update(deltaTime);
    if (target_ && damageCooldownTimer_ <= 0.0f && state_ != State::Weak) {

        // ① 先ほど作った関数で、腕の先などにある剣を確実に見つける！
        Object3d* weapon = FindWeaponRecursive(target_);

        // ② 振られている剣を見つけたら、ブロックとの当たり判定をチェック！
        if (weapon) {
            for (Object3d* block : armorBlocks_) {
                if (!block) continue;

                // ★超重要：待機中のブロックは「壁(kGround)」になっているので、
                // マスク設定で弾かれないように一瞬だけ判定を全開放(0xFFFFFFFF)する！
                uint32_t originalMask = block->GetCollisionMask();
                block->SetCollisionMask(0xFFFFFFFF);

                CollisionInfo info = block->CheckCollision(weapon);

                // 判定が終わったらすぐに元のマスクに戻す
                block->SetCollisionMask(originalMask);

                if (info.isColliding) {
                    // 当たった！！
                    TakeBarrierDamage(10.0f); // バリアに10ダメージ！
                    break; // 1フレームに多段ヒットしないように抜ける
                }
            }
        }
    }

 
    // ==========================================
    // ブロックの色を元に戻す処理
    // ==========================================
    if (preTimer > 0.0f && colorResetTimer_ <= 0.0f) {
        for (Object3d* block : armorBlocks_) {
            if (block) block->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); // 白に戻す
        }
    }

    // ★ 新規：ゲーム再生中は待機タイマーを常に進める！
    // ==========================================
    if (SceneManager::GetInstance()->IsPlaying()) {
        s_globalIdleTimer += deltaTime;
    }

    // ==========================================
    // ★ 飛んでいるブロックの更新
    // ==========================================
    UpdateFlyingBlocks(deltaTime);

    // ★ 3. アニメーションシーケンスを優先実行
    UpdateAnimationSequence(deltaTime);

    // 【重要】アニメーション実行中（Phase 1～3）は、
    // 下の既存ステート（Idle/Attackなど）を走らせないようにガードをかける！
    if (animPhase_ != 0 && animPhase_ != 4) {
        return;
    }

    // 4. 通常のステート更新（アニメーション中以外に動く）
    if (isFirstFrame_) {
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

    // 現在のステートに応じた属性を決定する
    uint32_t targetAttribute = (state_ == State::Attack) ? kEnemyAttack : kEnemy;

    // ① コア本体の属性を切り替え
    SetCollisionAttribute(targetAttribute);

    // ② ★追加：周りのブロックたちの属性も全部一緒に切り替える！
    for (Object3d* block : armorBlocks_) {
        if (block) {
            block->SetCollisionAttribute(targetAttribute);
        }
    }

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
    //if (director_ && director_->IsFinished()) {
    //    ChangeState(State::Attack);
    //}
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
        }
        else if (eventInfo.id == 2) {
            // イベントID 2 の処理 (例: 飛び道具の発射など)
        }
    }

    // 攻撃シナリオが終了したら、弱点露出ステートへ移行
    if (director_->IsFinished()) {
        ChangeState(State::Weak);
    }
}

void BossCore::UpdateWeak(float deltaTime) {
    animTimer_ += deltaTime;

    // ==========================================
    // 演出1：機能停止して小刻みに震える（プルプル）
    // ==========================================
    // sin波を使って、高速で小刻みに揺らす
    float shakeX = std::sin(animTimer_ * 50.0f) * 0.05f;
    float shakeZ = std::cos(animTimer_ * 45.0f) * 0.05f;
    SetRotation({ shakeX, GetRotation().y, shakeZ });
    GetTransform()->isQuaternionMaster = false;

    // ==========================================
    // 演出2：全体の色を暗くして「ショートした」感を出す
    // ==========================================
    SetColor({ 0.3f, 0.3f, 0.3f, 1.0f }); // コアをダークグレーに
    for (Object3d* block : armorBlocks_) {
        if (block) {
            block->SetColor({ 0.3f, 0.3f, 0.3f, 1.0f }); // ブロックもダークグレーに
        }
    }

    // ==========================================
    // 3秒経過で復帰（再起動）
    // ==========================================
    if (animTimer_ >= 3.0f) {
        animTimer_ = 0.0f;

        // 姿勢を真っ直ぐに戻す
        SetRotation({ 0.0f, GetRotation().y, 0.0f });

        // 色を元の白(再起動)に戻す
        SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        for (Object3d* block : armorBlocks_) {
            if (block) block->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        }

        // 待機状態(Idle)へ戻る
        ChangeState(State::Idle);
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

        // ==========================================
        // ★ 修正：待機中は常にブロックを周回軌道に乗せる！
        // ==========================================
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            OrbitData orbit = GetIdleOrbit(i);
            armorBlocks_[i]->SetTranslate(orbit.pos);
            armorBlocks_[i]->SetScale(orbit.scale);
            armorBlocks_[i]->SetRotation(orbit.rot);
            armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
        }

        if (input->IsKeyTriggered(DIK_1)) {
            // モード1：形態変化からの突進
            attackMode_ = 1;
            animPhase_ = 1; // ★ 変形フェーズからスタート！
            animTimer_ = 0.0f;
            ChangeState(State::Attack);
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
            ChangeState(State::Attack);
        }
        // ==========================================
        // 3キーで「こぶし落下攻撃」を発動！
        // ==========================================
        else if (input->IsKeyTriggered(DIK_3)) {
            attackMode_ = 3;
            animPhase_ = 20; // フェーズ20からスタート
            animTimer_ = 0.0f;
            ChangeState(State::Attack);
        }
        // ==========================================
        // 4キーで「モーション4：巨大な壁の横断攻撃」を発動！
        // ==========================================
        else if (input->IsKeyTriggered(DIK_4)) {
            attackMode_ = 4;
            animPhase_ = 39;
            animTimer_ = 0.0f;
            shotCount_ = 0;
            ChangeState(State::Attack);
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
            float duration = 3.0f; // 3秒かけてゆっくり戻る
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = Easing::OutExpo(t);

            // ボスの回転だけは 0 にリセット（直立姿勢へ）
            SetRotation({ 0.0f, 0.0f, 0.0f });
            GetTransform()->isQuaternionMaster = false;

            // ==========================================
            // ★ 修正：固定位置ではなく、常に動き続ける軌道(GetIdleOrbit)にLerpする！
            // ==========================================
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
                ChangeState(State::Idle);
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

                blockStartPos_.clear();
                blockTargetPos_.clear();

                struct BlockSetting {
                    Vector3 translate;
                    Vector3 scale;
                    Vector3 rotation;
                };

                float turnY = std::numbers::pi_v<float> / 2.0f;

                std::vector<BlockSetting> settings = {
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

            if (target_) {
                Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
                float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                SetRotation({ GetRotation().x, angleY, GetRotation().z });
                GetTransform()->isQuaternionMaster = false;
            }

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
            // ==========================================
            // ★ 修正：戻ってきたブロックから順に待機軌道に乗せる！
            // ==========================================
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
                if (animTimer_ >= 1.0f) { // すべて戻ってきてから1秒の隙を晒す
                    animPhase_ = 0;
                    attackMode_ = 0;
                    animTimer_ = 0.0f;
                    ChangeState(State::Idle);
                }
            }
        }
    }
    // ======================================
    // 攻撃モード3：ハンマー合体 ＆ 目の前で叩き潰す！
    // ======================================
    else if (attackMode_ == 3) {

        // --- Phase 20: 瞬時にハンマー形態へ変形
        if (animPhase_ == 20) {
            if (animTimer_ == 0.0f) {
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

            float moveDuration = 4.5f;
            float rotDuration = 3.0f;

            float moveT = std::min(animTimer_ / moveDuration, 1.0f);
            float rotT = std::min(animTimer_ / rotDuration, 1.0f);

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

            Vector3 toPlayer = targetPos - currentPos;
            float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);

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

            float startRotZ = -70.0f * (std::numbers::pi_v<float> / 180.0f);
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

            if (animTimer_ == 0.0f) {
                animStartPos_ = GetRotation(); // ボスの全回転角度を記憶
            }

            animTimer_ += deltaTime;

            if (animTimer_ >= 3.0f) {
                animPhase_ = 24;
                animTimer_ = 0.0f;
            }
        }
        // --- Phase 24: 待機軌道に向かって復帰する ---
        else if (animPhase_ == 24) {

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

            // ==========================================
            // ★ 修正：固定位置ではなく、常に動き続ける軌道(GetIdleOrbit)にLerpする！
            // ==========================================
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
                ChangeState(State::Idle);
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
                    // 2回目以降はそのまま現在地をスタートに！
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
            float duration = 5.0f; // 横断速度
            float t = std::min(animTimer_ / duration, 1.0f);
            float easeT = std::pow(t, 2.0f);

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                Vector3 blockPos = armorBlocks_[i]->GetTranslate();

                if (shotCount_ == 0) {
                    blockPos.z = Math::Lerp(150.0f, -150.0f, easeT); // 奥から手前
                }
                else if (shotCount_ == 1) {
                    blockPos.z = Math::Lerp(-150.0f, 150.0f, easeT); // 手前から奥
                }
                else if (shotCount_ == 2) {
                    blockPos.x = Math::Lerp(150.0f, -150.0f, easeT); // 右から左
                }
                else if (shotCount_ == 3) {
                    blockPos.x = Math::Lerp(-150.0f, 150.0f, easeT); // 左から右
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

            // ==========================================
            // ★ 修正：固定位置ではなく、常に動き続ける軌道(GetIdleOrbit)にLerpする！
            // ==========================================
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
                ChangeState(State::Idle);
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
        DebugConsole::GetInstance()->AddLog("★☆ Barrier BROKEN! Boss is STUNNED! ☆★");
        barrierHp_ = maxBarrierHp_; // 復帰後のためにリセットしておく

        // どんな攻撃アニメーション中だろうと強制中断させる！
        animPhase_ = 0;
        attackMode_ = 0;
        animTimer_ = 0.0f;
        shotCount_ = 0; // ★念のためこれもリセット

        // =======================================
        // ★修正：全てのブロックを強制的にボスの周りにワープさせる！（置いてけぼり完全防止）
        // ==========================================
        // ① 飛んでいるブロックのリストを完全に消去して「飛ぶのをやめさせる」
        flyingBlocks_.clear();

        // ② 全ブロックの親をボスに戻し、瞬時に待機軌道(Orbit)へセットする！
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            Object3d* block = armorBlocks_[i];
            if (block) {
                // 親をボス本体に戻す（射撃や壁攻撃で外れていたのを強制修復！）
                block->SetParent(this);

                // 待機軌道の定位置を計算して、瞬時にそこにスナップさせる！
                OrbitData orbit = GetIdleOrbit(i);
                block->SetTranslate(orbit.pos);
                block->SetScale(orbit.scale);
                block->SetRotation(orbit.rot);
                block->GetTransform()->isQuaternionMaster = false;
            }
        }

        // ③ ボス本体の回転などもまっすぐにリセットしておく
        SetRotation({ 0.0f, 0.0f, 0.0f });
        GetTransform()->isQuaternionMaster = false;

        // ダウン状態(Weak)へ強制移行！
        ChangeState(State::Weak);
    }
}