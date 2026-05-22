#pragma once
#include "BaseEnemy.h"

class EnemyBomb : public BaseEnemy {
public:
    enum class State {
        Chase,      // プレイヤー追跡
        Ignited,    // 点火カウントダウン
        Exploded    // 爆発完了
    };

    EnemyBomb() = default;
    ~EnemyBomb() override = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    
    void SetCarried(bool isCarried) override;
    void ExecuteAbility(class Player* player) override;
    void Ignite(float fuseTime = 3.0f);

private:
    void UpdateChase(float deltaTime);
    void UpdateIgnited(float deltaTime);
    void Explode();

private:
    State state_ = State::Chase;

    // カウントダウン関連
    float fuseTimer_ = 2.0f;       // 爆発までの残り時間（2秒）
    float flashTimer_ = 0.0f;      // 赤色点滅用タイマー
    bool flashState_ = false;       // 点滅状態のオン/オフ
    float pulseTimer_ = 0.0f;      // ドクンドクンと震える伸縮アニメーション用タイマー

    bool isThrown_ = false;        // プレイヤーによって投げられたか
    bool isAbilityExecuted_ = false; // ExecuteAbility（自爆能力）が発動されたか
};
