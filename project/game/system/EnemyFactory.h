#pragma once
#include <memory>
#include <string>
#include "BaseEnemy.h"
#include "Object3dCommon.h"

class EnemyFactory {
public:
    // シングルトンインスタンス取得
    static EnemyFactory* GetInstance();

    // 名前から敵を作成する関数
    std::unique_ptr<BaseEnemy> CreateEnemy(const std::string& enemyName, Object3dCommon* common);


private:
    EnemyFactory() = default;
    ~EnemyFactory() = default;
    EnemyFactory(const EnemyFactory&) = delete;
    const EnemyFactory& operator=(const EnemyFactory&) = delete;
};