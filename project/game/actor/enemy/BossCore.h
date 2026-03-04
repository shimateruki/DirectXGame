#pragma once
#include "BaseEnemy.h"
#include "GhostDirector.h"
#include <memory>
#include <string>
#include <unordered_map>

class SceneManager;

class BossCore : public BaseEnemy {
public:
    enum class State {
        Idle,       // 待機（Idleモーション再生）
        Attack,     // 攻撃中（Attack1〜10のいずれかを再生）
        Weak,       // 攻撃後の隙（Weakモーション再生）
        Dead        // 撃破
    };

    void Initialize(Object3dCommon* common, const std::string& modelName, SceneManager* sceneManager);
    void Update(float deltaTime) override;

private:
    State state_ = State::Idle;

    // 状態（ステート）を切り替える便利関数
    void ChangeState(State nextState);

    // 全モーションの監督を保持する辞書
    std::unordered_map<std::string, std::unique_ptr<GhostDirector>> directors_;
    GhostDirector* currentDirector_ = nullptr;

    void UpdateIdle(float deltaTime);
    void UpdateAttack(float deltaTime);
    void UpdateWeak(float deltaTime);
};