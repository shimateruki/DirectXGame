#include "EnemyFactory.h"
#include "EnemySlime.h"
#include "EnemyBomb.h"
#include "EnemyMushroom.h"
#include "EnemyFireSlime.h"
#include "EnemyThunderSlime.h"
#include "EnemyWindSlime.h"
#include "EnemyGiantSlime.h"
#include "EnemyPrismSlime.h"
#include "EnemyBat.h"
#include "EnemyBeamDrone.h"
#include <BossCore.h>
#include "SceneManager.h"
#include <EnemyBomber.h>
#include "GameplayStatusManager.h"
#include <algorithm>
// 他の敵タイプを追加する場合は include と CreateEnemy の分岐を増やす

namespace {
constexpr int kSlimeSoftMaterialType = 25;
constexpr int kPrismCrystalMaterialType = 27;

bool IsSlimeEnemyType(const std::string& enemyType) {
    return enemyType == "Slime" ||
        enemyType == "Bomber" ||
        enemyType == "FireSlime" ||
        enemyType == "ThunderSlime" ||
        enemyType == "WindSlime" ||
        enemyType == "GiantSlime" ||
        enemyType == "PrismSlime";
}

void ApplySlimeMaterialDefault(BaseEnemy* enemy) {
    if (!enemy || !IsSlimeEnemyType(enemy->GetEnemyType())) {
        return;
    }
    enemy->SetMaterialType(enemy->GetEnemyType() == "PrismSlime"
        ? kPrismCrystalMaterialType
        : kSlimeSoftMaterialType);
}
}

EnemyFactory* EnemyFactory::GetInstance() {
    static EnemyFactory instance;
    return &instance;
}

// 敵タイプ名ごとに専用クラスを生成します。共通ステータスはGameplayStatusManagerが設定します。
std::unique_ptr<BaseEnemy> EnemyFactory::CreateEnemy(const std::string& enemyName, Object3dCommon* common) {
    std::unique_ptr<BaseEnemy> newEnemy = nullptr;
    if (enemyName == "Slime") {
        auto slime = std::make_unique<EnemySlime>();

        // モデル読み込みと共通初期化
        slime->Initialize(common, "Characters/slime_pink");

        newEnemy = std::move(slime);
    }
    else if (enemyName == "BossCore") 
    {
        auto boss = std::make_unique<BossCore>();

        boss->SetSceneManager(SceneManager::GetInstance());
        // ボスはブロックモデルをコア/パーツ制御の基準として使う
        boss->Initialize(common, "Stages/block");

        newEnemy = std::move(boss);
    }
    else if (enemyName == "Bomb")
    {
        auto bomb = std::make_unique<EnemyBomb>();
        
        bomb->Initialize(common, "Gimmicks/blob");

        newEnemy = std::move(bomb);
    }

    else if (enemyName == "Bomber")
    {
        auto bomber = std::make_unique<EnemyBomber>();

        bomber->Initialize(common, "Characters/slime_black");

        newEnemy = std::move(bomber);
    }
    else if (enemyName == "Mushroom")
    {
        auto mushroom = std::make_unique<EnemyMushroom>();
        mushroom->Initialize(common, "Primitives/cylinder");

        newEnemy = std::move(mushroom);
    }
    else if (enemyName == "FireSlime")
    {
        auto fireSlime = std::make_unique<EnemyFireSlime>();
        fireSlime->Initialize(common, "Characters/slime_red");

        newEnemy = std::move(fireSlime);
    }
    else if (enemyName == "ThunderSlime")
    {
        auto thunderSlime = std::make_unique<EnemyThunderSlime>();
        thunderSlime->Initialize(common, "Characters/slime_yellow");

        newEnemy = std::move(thunderSlime);
    }
    else if (enemyName == "WindSlime")
    {
        auto windSlime = std::make_unique<EnemyWindSlime>();
        windSlime->Initialize(common, "Characters/slime_wind");

        newEnemy = std::move(windSlime);
    }
    else if (enemyName == "GiantSlime")
    {
        auto giantSlime = std::make_unique<EnemyGiantSlime>();
        giantSlime->Initialize(common, "Characters/slime");

        newEnemy = std::move(giantSlime);
    }
    else if (enemyName == "PrismSlime")
    {
        auto prismSlime = std::make_unique<EnemyPrismSlime>();
        prismSlime->Initialize(common, "Characters/prism_slime");

        newEnemy = std::move(prismSlime);
    }
    else if (enemyName == "Bat")
    {
        auto bat = std::make_unique<EnemyBat>();
        bat->Initialize(common, "Characters/bat");

        newEnemy = std::move(bat);
    }
    else if (enemyName == "BeamDrone")
    {
        auto beamDrone = std::make_unique<EnemyBeamDrone>();
        beamDrone->Initialize(common, "Characters/eye");

        newEnemy = std::move(beamDrone);
    }
    // 作った敵にタイプ名を保存し、タイプ共通設定を一元管理から適用します。
    if (newEnemy) {
        newEnemy->SetEnemyType(enemyName);
        newEnemy->SetClassName("Enemy");
        ApplySlimeMaterialDefault(newEnemy.get());
        auto* statusManager = GameplayStatusManager::GetInstance();
        statusManager->Initialize();
        statusManager->ApplyEnemyStatus(newEnemy.get(), true);
    } else {
        // 未登録タイプの場合は、落ちずに確認できる仮の敵を置く
        newEnemy = std::make_unique<BaseEnemy>();
        newEnemy->Initialize(common, "Primitives/cube");
        newEnemy->SetEnemyType("");
    }

    return newEnemy;
}
