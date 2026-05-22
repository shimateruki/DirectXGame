#include "EnemyFactory.h"
#include "EnemySlime.h"
#include "EnemyBomb.h"
#include <BossCore.h>
#include "SceneManager.h"
#include <EnemyBomber.h>
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
        slime->Initialize(common, "Stages/block");

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
        boss->Initialize(common, "Stages/block");

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
    else if (enemyName == "Bomb")
    {
        auto bomb = std::make_unique<EnemyBomb>();
        
        bomb->Initialize(common, "Primitives/sphere");

        if (!bomb->param_.has_value()) {
            bomb->param_.emplace();
        }

        // ボム用ステータス設定
        auto& p = bomb->param_.value();
        p.hp = 30.0f;          // 体力
        p.maxHp = 30.0f;
        p.speed = 0.04f;       // スライムよりやや遅くじわじわ追いかける
        p.gravity = 60.0f;     // 通常重力

        newEnemy = std::move(bomb);
    }

    else if (enemyName == "Bomber")
    {
        auto bomber = std::make_unique<EnemyBomber>();

        // モデルは適宜変更してください
        bomber->Initialize(common, "Stages/block");

        if (!bomber->param_.has_value()) {
            bomber->param_.emplace();
        }

        auto& p = bomber->param_.value();
        p.hp = 60.0f;
        p.maxHp = 60.0f;
        p.speed = 0.0f;       // 立ち止まって投げる想定なら0、動かすなら数値を設定
        p.gravity = 60.0f;
        p.detectionRange = 32.0f;
        bomber->SetDetectionRange(p.detectionRange);

        newEnemy = std::move(bomber);
    }
    //:作った敵に「名札」をつける
    if (newEnemy) {
        newEnemy->SetEnemyType(enemyName);
    } else {
        // デフォルト（ただの置物）の場合
        newEnemy = std::make_unique<BaseEnemy>();
        newEnemy->Initialize(common, "Primitives/cube");
        newEnemy->SetEnemyType(""); // 特になし
    }

    return newEnemy;
}
