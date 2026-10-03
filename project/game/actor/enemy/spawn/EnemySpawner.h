#pragma once
#include "Object3d.h"
#include <functional>
#include <string>

// 前方宣言
class Object3dCommon;

// 一定間隔で敵を発生させるための配置用オブジェクト
class EnemySpawner : public Object3d {
public:
    /// <summary>
    /// スポナーの初期化
    /// </summary>
    void Initialize(Object3dCommon* common, const std::string& enemyType, float interval, int maxCount);

    // タイマーを進め、条件を満たしたらスポーン通知を出す
    void Update(float deltaTime) override;

    using SpawnCallback = std::function<void(const Vector3&)>;
    // 実際の敵生成はシーン側へ任せるため、座標だけコールバックで通知する
    void SetOnSpawnCallback(SpawnCallback callback) { onSpawnCallback_ = callback; }

private:
    void Spawn();

private:
    // Factory に渡す共通描画情報。スポーン時の敵初期化で使う。
    Object3dCommon* common_ = nullptr;

    std::string enemyType_; // 生成したい敵タイプ名。
    float spawnInterval_ = 1.0f; // 次のスポーンまでの間隔。
    float timer_ = 0.0f;         // 経過時間。
    SpawnCallback onSpawnCallback_ = nullptr;
    int maxCount_ = 0;    // 最大生成数。0以下なら無制限扱い。
    int spawnCount_ = 0;  // 生成済みカウント。
};
