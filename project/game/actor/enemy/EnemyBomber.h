#pragma once
#include "BaseEnemy.h"
#include <functional>
#include <memory>

class EnemyBomber : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;

    // ゲームシーンに動的にボムを追加するためのコールバック
    void SetSpawnCallback(std::function<void(std::unique_ptr<BaseEnemy>)> callback) {
        spawnCallback_ = callback;
    }

private:
    void ThrowBomb();

    float throwTimer_ = 0.0f;
    float throwInterval_ = 3.0f; // 3秒に1回投げる
    std::function<void(std::unique_ptr<BaseEnemy>)> spawnCallback_ = nullptr;
    Object3dCommon* common_ = nullptr; // ボム初期化用に保持しておく
};