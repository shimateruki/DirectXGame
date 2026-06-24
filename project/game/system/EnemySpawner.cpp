#include "EnemySpawner.h"
#include "EnemyFactory.h"

void EnemySpawner::Initialize(Object3dCommon* common, const std::string& enemyType, float interval, int maxCount) {
    Object3d::Initialize(common);
    common_ = common;
    enemyType_ = enemyType;
    spawnInterval_ = interval;

    // 最大生成数と内部カウンタを初期化する
    maxCount_ = maxCount;
    spawnCount_ = 0;
    timer_ = 0.0f;
}

void EnemySpawner::Update(float deltaTime) {
    // 最大生成数に達したら、それ以上は発生させない
    if (maxCount_ > 0 && spawnCount_ >= maxCount_) {
        return;
    }

    timer_ += deltaTime;

    // 指定間隔を超えたらスポーン位置を通知する
    if (timer_ >= spawnInterval_) {
        timer_ = 0.0f;
        Spawn();
        spawnCount_++;
    }

    Object3d::Update(deltaTime);
}

void EnemySpawner::Spawn() {
    if (onSpawnCallback_) {
        // 自分の配置座標をスポーン位置として使う
        Vector3 spawnPos = { 0, 0, 0 };
        if (GetTransform()) {
            spawnPos = GetTransform()->translate;
        }

        // シーン側へ「この場所に敵を出してほしい」と通知する
        onSpawnCallback_(spawnPos);
    }
}
