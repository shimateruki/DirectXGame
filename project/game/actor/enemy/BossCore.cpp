#include "BossCore.h"

void BossCore::Initialize(Object3dCommon* common, const std::string& modelName, SceneManager* sceneManager) {
    BaseEnemy::Initialize(common, modelName);

    // =========================================================
    // 登場時に全モーション（待機・隙・攻撃10種）を雇っておく
    // =========================================================

    // 待機モーション
    directors_["Idle"] = std::make_unique<GhostDirector>();
    directors_["Idle"]->Initialize(sceneManager);
    // directors_["Idle"]->LoadScenario("boss_idle.json");

    // 弱点露出（隙）モーション
    directors_["Weak"] = std::make_unique<GhostDirector>();
    directors_["Weak"]->Initialize(sceneManager);
    // directors_["Weak"]->LoadScenario("boss_weak.json");

    // 攻撃パターン1〜10を一括ロード
    for (int i = 1; i <= 10; ++i) {
        std::string attackName = "Attack" + std::to_string(i);
        directors_[attackName] = std::make_unique<GhostDirector>();
        directors_[attackName]->Initialize(sceneManager);
        // directors_[attackName]->LoadScenario("boss_attack_" + std::to_string(i) + ".json");
    }

    // 最初の状態を待機（Idle）にセットして再生開始！
    ChangeState(State::Idle);
}

void BossCore::Update(float deltaTime) {
    BaseEnemy::Update(deltaTime);

    switch (state_) {
    case State::Idle:   UpdateIdle(deltaTime);   break;
    case State::Attack: UpdateAttack(deltaTime); break;
    case State::Weak:   UpdateWeak(deltaTime);   break;
    }

    if (currentDirector_) {
        currentDirector_->Update();
    }
}

// =========================================================
// 状態切り替えと同時に、該当するモーションの再生を開始する関数
// =========================================================
void BossCore::ChangeState(State nextState) {
    state_ = nextState;

    switch (state_) {
    case State::Idle:
        currentDirector_ = directors_["Idle"].get();
        if (currentDirector_) currentDirector_->PlayScenario();
        break;

    case State::Attack: {
        // 次の攻撃パターンを1〜10からランダムに選ぶ！
        int nextAttack = rand() % 10 + 1;
        std::string attackName = "Attack" + std::to_string(nextAttack);

        currentDirector_ = directors_[attackName].get();
        if (currentDirector_) currentDirector_->PlayScenario();
        break;
    }

    case State::Weak:
        currentDirector_ = directors_["Weak"].get();
        if (currentDirector_) currentDirector_->PlayScenario();
        break;
    }
}

// =========================================================
// 各状態の更新処理（すべて IsFinished で次に進む）
// =========================================================

void BossCore::UpdateIdle(float deltaTime) {
    // 待機モーション（フワフワ等）の再生が終わったら、攻撃へ！
    if (currentDirector_ && currentDirector_->IsFinished()) {
        ChangeState(State::Attack);
    }
}

void BossCore::UpdateAttack(float deltaTime) {
    if (!currentDirector_) return;

    // ★監督から「何のイベントが」「誰で」起きたかを聞き出す想定の処理
     ActiveEvent eventInfo = currentDirector_->GetActiveEvent();



    if (eventInfo.id != 0 && eventInfo.targetObject) {

        // イベントを起こしたパーツの座標を取得！
        Vector3 spawnPos = eventInfo.targetObject->GetWorldPosition();

        if (eventInfo.id == 1) {
            // 例: そのパーツの当たり判定をONにする
            // eventInfo.targetObject->SetCollisionEnable(true);
        } else if (eventInfo.id == 2) {
            // 例: そのパーツの位置で砂煙パーティクルを出す
            // ParticleManager::GetInstance()->Emit("Dust", spawnPos);
        }
    }

    // 攻撃モーション（シナリオ）が最後まで終わったら、隙を晒す状態へ！
    if (currentDirector_->IsFinished()) {
        ChangeState(State::Weak);
    }
}

void BossCore::UpdateWeak(float deltaTime) {
    // 弱点露出のモーションが終わったら、再び待機へ戻る！
    if (currentDirector_ && currentDirector_->IsFinished()) {
        ChangeState(State::Idle);
    }
}