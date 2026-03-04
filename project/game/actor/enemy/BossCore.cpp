#include "BossCore.h"

void BossCore::Initialize(SceneManager* sceneManager) {
    BaseEnemy::Initialize(); // 親クラスの初期化

    // ボス専用のシナリオディレクターを生成
    director_ = std::make_unique<GhostDirector>();
    director_->Initialize(sceneManager);

    // テストとして攻撃パターン1を読み込んでおく
    // LoadAttackScenario("boss_attack_1.json");

    state_ = State::Idle;
    stateTimer_ = 0.0f;
}

void BossCore::Update() {
    BaseEnemy::Update(); // 共通の更新処理（重力など）

    // 状態ごとの処理を振り分け
    switch (state_) {
    case State::Idle:
        UpdateIdle();
        break;
    case State::Attack:
        UpdateAttack();
        break;
    case State::Weak:
        UpdateWeak();
        break;
    }

    // ★監督（ディレクター）に時間を進めさせる
    if (director_) {
        director_->Update();
    }
}

void BossCore::UpdateIdle() {
    stateTimer_ += 1.0f / 60.0f; // ※実際のdeltaTimeを使ってください

    // 3秒待機したら攻撃モードへ移行！
    if (stateTimer_ >= 3.0f) {
        state_ = State::Attack;
        stateTimer_ = 0.0f;

        // 攻撃シナリオの再生開始！
        if (director_) {
            director_->PlayScenario();
        }
    }
}

void BossCore::UpdateAttack() {
    // 監督が再生を終えたか（シナリオが完了したか）をチェック
    if (director_ && director_->IsFinished()) {
        // 攻撃が終わったら、疲れてコアを露出する（弱点状態）
        state_ = State::Weak;
        stateTimer_ = 0.0f;
    }
}

void BossCore::UpdateWeak() {
    stateTimer_ += 1.0f / 60.0f;

    // 5秒間隙を晒したら、再び待機状態に戻る（ループ）
    if (stateTimer_ >= 5.0f) {
        state_ = State::Idle;
        stateTimer_ = 0.0f;
    }
}

void BossCore::UpdateAttack() {
    if (!currentDirector_) return;

    // ★監督から今のイベントIDを聞き出す！
    int eventID = currentDirector_->GetActiveEventID();

    // IDに応じた処理（録画エディタで仕込んだ数字と連動！）
    if (eventID == 1) {
        // 例: ID=1 のフレームならダメージ判定ON！
        // collider_->SetEnable(true);
    } else if (eventID == 2) {
        // 例: ID=2 のフレームならドーン！と砂煙パーティクルを出す
        // ParticleManager::GetInstance()->Emit("ImpactDust", GetWorldPosition());
    }

    // 再生が終わったら待機モードへ
    if (currentDirector_->IsFinished()) {
        state_ = State::Idle;
    }
}