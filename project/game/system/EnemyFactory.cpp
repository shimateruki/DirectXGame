#include "EnemyFactory.h"
#include "EnemySlime.h"
#include <BossCore.h>
#include "SceneManager.h"
// 他の敵ができたらここに追加 (#include "EnemyRobot.h" 等)

EnemyFactory* EnemyFactory::GetInstance() {
    static EnemyFactory instance;
    return &instance;
}

std::unique_ptr<BaseEnemy> EnemyFactory::CreateEnemy(const std::string& enemyName, Object3dCommon* common) {
    std::unique_ptr<BaseEnemy> newEnemy = nullptr;
    if (enemyName == "Slime") { // ← 例: ゴブリンやスライム
        auto slime = std::make_unique<EnemySlime>();

        // 1. 初期化 (モデル読み込み)
        slime->Initialize(common, "block");

        if (!slime->param_.has_value()) {
            slime->param_.emplace();
        }

        // ステータス設定
        auto& p = slime->param_.value();
        p.hp = 50.0f;          // 体力
        p.maxHp = 50.0f;       // 最大体力
        p.speed = 0.1f;        // 移動速度
        p.gravity = 60.0f;     // 重力 

        newEnemy = std::move(slime);
    }
    else if (enemyName == "BossCore") 
    {
        auto boss = std::make_unique<BossCore>();

     
    boss->SetSceneManager(SceneManager::GetInstance());
        // 2. 引数を気にせずオーバーライドした Initialize を呼べる！
        boss->Initialize(common, "block");

        if (!boss->param_.has_value()) {
            boss->param_.emplace();
        }

        // ボス用ステータス設定
        auto& p = boss->param_.value();
        p.hp = 1000.0f;        // ボスなので体力多め
        p.maxHp = 1000.0f;
        p.speed = 0.05f;       // ゆっくり動く、あるいは浮遊など
        p.gravity = 0.0f;      // 無相の雷のように常に浮いているなら重力を切るのもあり

        newEnemy = std::move(boss);
    }

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