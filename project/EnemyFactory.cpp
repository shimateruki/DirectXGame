#include "EnemyFactory.h"
#include "EnemySlime.h"
// 他の敵ができたらここに追加 (#include "EnemyRobot.h" 等)

EnemyFactory* EnemyFactory::GetInstance() {
    static EnemyFactory instance;
    return &instance;
}

std::unique_ptr<BaseEnemy> EnemyFactory::CreateEnemy(const std::string& enemyName, Object3dCommon* common) {

    std::unique_ptr<BaseEnemy> newEnemy = nullptr;

    // 名前を見て、生成するクラスを変える
    if (enemyName == "Slime") {
        auto slime = std::make_unique<EnemySlime>();
        // 初期化
        slime->Initialize(common, "cube");
        newEnemy = std::move(slime);
    } else {
        // 知らない名前ならデフォルトの敵 (ただの置物)
        newEnemy = std::make_unique<BaseEnemy>();
        newEnemy->Initialize(common, "cube"); // とりあえず箱モデルなどを指定
    }

    return newEnemy;
}