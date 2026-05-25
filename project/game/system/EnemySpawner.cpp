#include "EnemySpawner.h"
#include "EnemyFactory.h"


void EnemySpawner::Initialize(Object3dCommon* common, const std::string& enemyType, float interval, int maxCount) {
    Object3d::Initialize(common);
    common_ = common;
    enemyType_ = enemyType;
    spawnInterval_ = interval;

    // 最大数を保存
    maxCount_ = maxCount;
    spawnCount_ = 0;      // カウンタリセット
    timer_ = 0.0f;
}

void EnemySpawner::Update(float deltaTime) {
    // 定員オーバーなら何もしないで帰る（仕事終了）
    if (maxCount_ > 0 && spawnCount_ >= maxCount_) {
        return;
    }

    timer_ += deltaTime;

    // 時間経過でスポーン
    if (timer_ >= spawnInterval_) {
        timer_ = 0.0f;
        Spawn();

        // カウントを増やす
        spawnCount_++;
    }

    Object3d::Update(deltaTime);
}

void EnemySpawner::Spawn() {


    if (onSpawnCallback_) {
        // 自分の座標を取得 (Transformのtranslate、またはワールド座標)
        Vector3 spawnPos = { 0,0,0 };
        if (GetTransform()) {
            spawnPos = GetTransform()->translate;
 
        }

        // 「この場所でお願い」と通知
        onSpawnCallback_(spawnPos);
    }
}