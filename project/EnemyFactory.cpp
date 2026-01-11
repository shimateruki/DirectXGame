#include "EnemyFactory.h"
#include "EnemySlime.h"
// 他の敵ができたらここに追加 (#include "EnemyRobot.h" 等)

EnemyFactory* EnemyFactory::GetInstance() {
    static EnemyFactory instance;
    return &instance;
}

std::unique_ptr<BaseEnemy> EnemyFactory::CreateEnemy(const std::string& enemyName, Object3dCommon* common) {
    std::unique_ptr<BaseEnemy> newEnemy = nullptr;

    if (enemyName == "Slime") {
        auto slime = std::make_unique<EnemySlime>();
        slime->Initialize(common, "cube"); 
        newEnemy = std::move(slime);
    }
    // 将来ここに追加していく
    // else if (enemyName == "Robot") { ... }

    //:作った敵に「名札」をつける
    if (newEnemy) {
        newEnemy->SetEnemyType(enemyName);
    } else {
        // デフォルト（ただの置物）の場合
        newEnemy = std::make_unique<BaseEnemy>();
        newEnemy->Initialize(common, "cube");
        newEnemy->SetEnemyType(""); // 特になし
    }

    return newEnemy;
}