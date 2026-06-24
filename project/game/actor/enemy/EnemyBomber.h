#pragma once
#include "BaseEnemy.h"
#include <functional>
#include <memory>

// 距離を保ちながら爆弾を生成して投げる、ボム使いの敵
class EnemyBomber : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void SetCarried(bool isCarried) override;
    void ExecuteAbility(class Player* player) override;
    void UpdateCarriedAbility(class Player* player, float deltaTime) override;

    void SetSpawnCallback(std::function<void(std::unique_ptr<BaseEnemy>)> callback) {
        spawnCallback_ = callback;
    }

private:
    // 投げる前のタメと実際の生成を分けて、予兆を作る
    enum class ThrowState {
        Idle,
        Windup
    };

    bool IsTargetInRange(float* outDistance = nullptr, Vector3* outDirection = nullptr) const;
    void UpdateFacing(const Vector3& direction);
    void UpdateFootwork(float deltaTime, const Vector3& direction, float distance, bool inRange);
    void BeginThrow();
    void ThrowBomb();
    void ThrowCarryBomb(class Player* player);
    void SpawnBombObject(std::unique_ptr<BaseEnemy> bomb);
    Vector3 GetPlayerForward(class Player* player) const;

    float throwTimer_ = 0.0f;          // 野生時の投擲間隔。
    float throwInterval_ = 2.8f;       // 次の爆弾を投げるまでの基準時間。
    float windupTimer_ = 0.0f;         // 投げる前のタメ時間。
    float throwWindup_ = 0.7f;         // タメ演出の長さ。
    float initialThrowDelay_ = 0.6f;   // 出現直後に即投げしないための猶予。
    float footworkTimer_ = 0.0f;       // 左右移動の切り替えタイマー。
    float footworkDirection_ = 1.0f;   // 横移動方向。
    float carriedThrowCooldown_ = 0.0f; // 持ち運び能力の再使用待ち。
    float carriedEffectTimer_ = 0.0f;   // 持ち運び発動後の色演出。
    ThrowState throwState_ = ThrowState::Idle;
    std::function<void(std::unique_ptr<BaseEnemy>)> spawnCallback_ = nullptr;
    Object3dCommon* common_ = nullptr;
};
