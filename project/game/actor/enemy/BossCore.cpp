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

    // --- 【開始判定】待機中(0) に 1キーが押されたら ---
    if (animPhase_ == 0) {
        if (input->IsKeyTriggered (DIK_1)) {
            //DebugConsole::Log ("Boss Charge Start!\n"); // ログを出して確認！
            animPhase_ = 1;
            animTimer_ = 0.0f;
        }
    }

    if (animPhase_ == 0) return;

    // --- フェーズ1: 移動 (x = -50) ---
    if (animPhase_ == 1) {
        if (animTimer_ == 0.0f) animStartPos_ = GetTranslate ();
        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min (animTimer_ / duration, 1.0f);

        Vector3 pos = GetTranslate ();
        pos.x = Math::Lerp (animStartPos_.x, -50.0f, Easing::OutExpo (t));
        SetTranslate (pos);

        if (t >= 1.0f) {
            animPhase_ = 2;
            animTimer_ = 0.0f;
            animStartPos_ = GetTranslate ();
        }
    }
    // --- フェーズ2: シェイク & プレイヤー注視 ---
    else if (animPhase_ == 2) {
        animTimer_ += deltaTime;
        float duration = 1.0f;
        float t = std::min (animTimer_ / duration, 1.0f);

        if (target_) {
            Vector3 toPlayer = target_->GetWorldPosition () - GetWorldPosition ();
            float angleY = std::atan2 (toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
            SetRotation ({ GetRotation ().x, angleY, GetRotation ().z });
        }

        Vector3 pos = animStartPos_;
        float shake = 0.3f;
        pos.x += ((float)rand () / RAND_MAX * 2.0f - 1.0f) * shake;
        pos.y += ((float)rand () / RAND_MAX * 2.0f - 1.0f) * shake;
        SetTranslate (pos);

        if (t >= 1.0f) {
            animPhase_ = 3;
            animTimer_ = 0.0f;
            animStartPos_ = GetTranslate ();
            if (target_) animTargetPos_ = target_->GetWorldPosition ();
        }
    }
    // --- フェーズ3: 加速突進 ---
    else if (animPhase_ == 3) {
        animTimer_ += deltaTime;
        float duration = 0.5f; // 少し速くしました
        float t = std::min (animTimer_ / duration, 1.0f);
        float easedT = std::pow (t, 4.0f);

        SetTranslate (Math::Lerp (animStartPos_, animTargetPos_, easedT));

        float totalRotation = std::numbers::pi_v<float> * 2.0f * 5.0f;
        SetRotation ({ easedT * totalRotation, GetRotation ().y, GetRotation ().z });

        if (t >= 1.0f) {
            animPhase_ = 4;
            //DebugConsole::Log ("Boss Charge Finished.\n");
        }
    }
    // --- フェーズ4: 自動リセット ---
    else if (animPhase_ == 4) {
        // ここで 0 に戻すことで、再び 1キーが効くようになります
        animPhase_ = 0;
        animTimer_ = 0.0f;
    }
}