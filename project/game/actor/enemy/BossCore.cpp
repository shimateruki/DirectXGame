#include "BossCore.h"
#include "imgui.h"
void BossCore::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);

    director_ = std::make_unique<GhostDirector>();
    if (sceneManager_) {
        director_->Initialize(sceneManager_);
    }

}

void BossCore::Update(float deltaTime) {
    // 1. 純粋な3Dオブジェクトとしての更新
    Object3d::Update(deltaTime);

    // =========================================================
    // ★ SceneManagerに今の状態を聞く！
    // =========================================================
    if (sceneManager_ && !sceneManager_->IsPlaying()) {
        // 停止中（エディタ操作中）ならここで処理を終わらせる
        return;
    }

    // =========================================================
    // 以降はプレイ中のみ実行される自律AIの処理
    // =========================================================
    if (isFirstFrame_) {
        ChangeState(State::Idle);
        isFirstFrame_ = false;
    }

    switch (state_) {
    case State::Idle:   UpdateIdle(deltaTime);   break;
    case State::Attack: UpdateAttack(deltaTime); break;
    case State::Weak:   UpdateWeak(deltaTime);   break;
    }

    if (director_) {
        director_->Update(deltaTime);
    }
}
// =========================================================
// 状態切り替えと同時に、該当するモーションのビデオテープを入れ替えて再生する関数
// =========================================================
void BossCore::ChangeState(State nextState) {
    state_ = nextState;

    if (!director_) return;

    switch (state_) {
    case State::Idle:
        // .json を削除
        director_->LoadScenario("boss_attack_1");
        director_->PlayScenario(false,false);
        break;

    case State::Attack: {
        int nextAttack = rand() % 10 + 1;
        // .json を削除
        std::string attackName = "boss_attack_" + std::to_string(nextAttack);

        //director_->LoadScenario(attackName);
        //director_->PlayScenario();
        break;
    }

    case State::Weak:
        // .json を削除
        //director_->LoadScenario("boss_weak");
        //director_->PlayScenario();
        break;
    }
}

// =========================================================
// 各状態の更新処理（すべて IsFinished で次に進む）
// =========================================================

void BossCore::UpdateIdle(float deltaTime) {
    // 待機モーション（フワフワ等）の再生が終わったら、攻撃へ！
    if (director_ && director_->IsFinished()) {
        ChangeState(State::Attack);
    }
}

void BossCore::UpdateAttack(float deltaTime) {
    if (!director_) return;

    // ★監督から「何のイベントが」「誰で」起きたかを聞き出す
    ActiveEvent eventInfo = director_->GetActiveEvent();

    if (eventInfo.id != 0 && eventInfo.targetObject) {

        // イベントを起こしたパーツの座標を取得！
        Vector3 spawnPos = eventInfo.targetObject->GetWorldPosition();

        if (eventInfo.id == 1) {
  
        } else if (eventInfo.id == 2) {
        
        }
    }

    // 攻撃モーション（シナリオ）が最後まで終わったら、隙を晒す状態へ！
    if (director_->IsFinished()) {
        ChangeState(State::Weak);
    }
}

void BossCore::UpdateWeak(float deltaTime) {
    // 弱点露出のモーションが終わったら、再び待機へ戻る！
    if (director_ && director_->IsFinished()) {
        ChangeState(State::Idle);
    }
}

