#include "EnemyFactory.h"
#include "EnemySlime.h"
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