#include "BossCore.h"

void BossCore::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);

    director_ = std::make_unique<GhostDirector>();
    if (sceneManager_) {
        director_->Initialize(sceneManager_);
    }

}

void BossCore::Update(float deltaTime) {
    // =========================================================
    // ★追加：最初の1フレーム目（すべての準備が整った瞬間）にシナリオを読む！
    // =========================================================
    if (isFirstFrame_) {
        ChangeState(State::Idle); // ここで Idle にして boss_attack_1.json をロード
        isFirstFrame_ = false;    // 二度と呼ばれないようにする
    }

    // 純粋な3Dオブジェクトとしての更新
    Object3d::Update(deltaTime);


    // 状態ごとの更新処理
    switch (state_) {
    case State::Idle:   UpdateIdle(deltaTime);   break;
    case State::Attack: UpdateAttack(deltaTime); break;
    case State::Weak:   UpdateWeak(deltaTime);   break;
    }

    if (director_) {
        director_->Update();
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
        director_->LoadScenario("boss_attack_1.json");
        director_->PlayScenario();
        
        break;

    case State::Attack: {
        // 次の攻撃パターンを1〜10からランダムに選ぶ！
        int nextAttack = rand() % 10 + 1;
        std::string attackName = "boss_attack_" + std::to_string(nextAttack) + ".json";

        //director_->LoadScenario(attackName);
        //director_->PlayScenario();
        break;
    }

    case State::Weak:
        //director_->LoadScenario("boss_weak.json");
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
            // 例: そのパーツの当たり判定をONにする
            // eventInfo.targetObject->SetCollisionEnable(true);
        } else if (eventInfo.id == 2) {
            // 例: そのパーツの位置で砂煙パーティクルを出す
            // ParticleManager::GetInstance()->Emit("Dust", spawnPos);
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