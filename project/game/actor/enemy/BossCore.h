#pragma once
#include "BaseEnemy.h" // ※既存の基底クラスがあればそれに合わせます
#include "GhostDirector.h"
#include <memory>
#include <string>

class BossCore : public BaseEnemy {
public:
    // ボスの状態
    enum class State {
        Idle,       // 待機中（フワフワ浮いているなど）
        Attack,     // 攻撃中（GhostDirectorでシナリオ再生中）
        Weak,       // 攻撃後の隙（コアが露出してダメージが通る）
        Dead        // 撃破
    };

    void Initialize(SceneManager* sceneManager);
    void Update() override;
    void Draw() override;

    // 特定の攻撃シナリオをロードして再生準備する
    void LoadAttackScenario(const std::string& scenarioName);



private:
    State state_ = State::Idle;
    float stateTimer_ = 0.0f;

    // ボス自身が「専用の監督（シナリオ再生機）」を持つ！
    std::unique_ptr<GhostDirector> director_;

    // 状態ごとの更新処理
    void UpdateIdle();
    void UpdateAttack();
    void UpdateWeak();

    // シナリオ名と監督（ディレクター）をセットで保持しておく辞書（マップ）
    std::unordered_map<std::string, std::unique_ptr<GhostDirector>> directors_;
    GhostDirector* currentDirector_ = nullptr; // 今再生している監督
};