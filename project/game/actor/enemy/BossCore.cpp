#include "BossCore.h"
#include "imgui.h"

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
    // 1. 基本的な3Dオブジェクトとしての更新
    Object3d::Update(deltaTime);

    // 2. エディタ操作中(非プレイ中)はAI処理をスキップ
    if (sceneManager_ && !sceneManager_->IsPlaying()) {
        return;
    }

    // 3. 初回フレーム時の初期ステートセット
    if (isFirstFrame_) {
        ChangeState(State::Idle);
        isFirstFrame_ = false;
    }

    // 4. 現在のステート(状態)に応じた更新処理
    switch (state_) {
    case State::Idle:   UpdateIdle(deltaTime);   break;
    case State::Attack: UpdateAttack(deltaTime); break;
    case State::Weak:   UpdateWeak(deltaTime);   break;
    }

    // 5. ディレクター(シナリオ・モーション)の更新
    if (director_) {
        director_->Update(deltaTime);
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