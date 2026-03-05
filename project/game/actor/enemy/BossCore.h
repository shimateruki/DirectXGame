#pragma once
#include "BaseEnemy.h"
#include "GhostDirector.h"
#include <memory>
#include <string>

class SceneManager; // 前方宣言

class BossCore : public BaseEnemy {
public:
    enum class State { Idle, Attack, Weak };


    void SetSceneManager(SceneManager* manager) { sceneManager_ = manager; }

    // ★ 引数は BaseEnemy と完全一致させる
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
#ifdef USE_IMGUI
    void DrawImGui(); 
#endif

private:
    void ChangeState(State nextState);
    void UpdateIdle(float deltaTime);
    void UpdateAttack(float deltaTime);
    void UpdateWeak(float deltaTime);


    std::unique_ptr<GhostDirector> director_;

    SceneManager* sceneManager_ = nullptr;
    State state_ = State::Idle;
    bool isFirstFrame_ = true;
};