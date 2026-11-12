#pragma once
#include "Object3D.h"
#include <functional>
#include <string>

// 前方宣言
namespace engine { class Object3DCommon; }

// 一定間隔で敵を発生させるための配置用オブジェクト
// 関連：生成したBaseEnemyの所有権を登録済みコールバックへ渡す。
// 主な入口：Initialize / Update / SetOnSpawnCallback
class EnemySpawner : public engine::Object3D {
  public:
    /// <summary>
    /// スポナーの初期化
    /// </summary>
    void Initialize(engine::Object3DCommon* common, const std::string& enemyType, float interval, int maxCount);

    // タイマーを進め、条件を満たしたらスポーン通知を出す
    void Update(float deltaTime) override;

    using SpawnCallback = std::function<void(const engine::Vector3&)>;

    // 実際の敵生成はシーン側へ任せるため、座標だけコールバックで通知する
    void SetOnSpawnCallback(SpawnCallback callback) { onSpawnCallback_ = callback; }

  private:
    /// コールバックが設定されていれば、現在位置を生成先へ通知する。
    void Spawn();

  private:
    // Factory に渡す共通描画情報。スポーン時の敵初期化で使う。
    engine::Object3DCommon* common_ = nullptr;

    std::string enemyType_;      // 生成したい敵タイプ名。
    float spawnInterval_ = 1.0f; // 次のスポーンまでの間隔。
    float timer_ = 0.0f;         // 経過時間。
    SpawnCallback onSpawnCallback_ = nullptr;
    int maxCount_ = 0;   // 最大生成数。0以下なら無制限扱い。
    int spawnCount_ = 0; // 生成済みカウント。
};
