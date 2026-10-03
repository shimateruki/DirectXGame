#pragma once
#include <memory>
#include <string>
#include "BaseEnemy.h"
#include "Object3dCommon.h"

// 文字列の敵タイプ名から、対応する敵インスタンスを生成するファクトリ
class EnemyFactory {
public:
    // シングルトンインスタンスを取得する
    static EnemyFactory* GetInstance();

    // enemyName に対応する敵を生成し、モデル・ステータス・検知範囲を設定する
    std::unique_ptr<BaseEnemy> CreateEnemy(const std::string& enemyName, Object3dCommon* common);

private:
    EnemyFactory() = default;
    ~EnemyFactory() = default;
    EnemyFactory(const EnemyFactory&) = delete;
    const EnemyFactory& operator=(const EnemyFactory&) = delete;
};
