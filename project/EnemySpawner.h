#pragma once
#include "Object3d.h"
#include <string>
#include <functional> 

// 前方宣言
class Object3dCommon;

class EnemySpawner : public Object3d {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(Object3dCommon* common, const std::string& enemyType, float interval, int maxCount);

    void Update(float deltaTime) override;

    using SpawnCallback = std::function<void(const Vector3&)>;
    void SetOnSpawnCallback(SpawnCallback callback) { onSpawnCallback_ = callback; }

private:
    void Spawn();

private:
    // ★追加: 工場に渡すために保持しておく
    Object3dCommon* common_ = nullptr;

    std::string enemyType_;
    float spawnInterval_ = 1.0f;
    float timer_ = 0.0f;
    SpawnCallback onSpawnCallback_ = nullptr;
    int maxCount_ = 0;    // 最大生成数 
    int spawnCount_ = 0;  // 生成済みカウント
};