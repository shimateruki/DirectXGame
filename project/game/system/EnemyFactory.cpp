#include "EnemyFactory.h"
#include "EnemySlime.h"
#include "EnemyBomb.h"
#include "EnemyMushroom.h"
#include "EnemyFireSlime.h"
#include "EnemyThunderSlime.h"
#include "EnemyGiantSlime.h"
#include "EnemyBat.h"
#include "EnemyBeamDrone.h"
#include <BossCore.h>
#include "SceneManager.h"
#include <EnemyBomber.h>
#include <algorithm>
#include <fstream>
#include "json.hpp"
// 他の敵タイプを追加する場合は include と CreateEnemy の分岐を増やす

namespace {
using json = nlohmann::json;
constexpr int kSlimeSoftMaterialType = 25;

// スライム系は共通して大きめのスケールにそろえる
bool IsSlimeEnemyType(const std::string& enemyType) {
    return enemyType == "Slime" ||
        enemyType == "Bomber" ||
        enemyType == "FireSlime" ||
        enemyType == "ThunderSlime" ||
        enemyType == "GiantSlime";
}

void ApplySlimeDefaults(BaseEnemy* enemy) {
    if (!enemy || !IsSlimeEnemyType(enemy->GetEnemyType())) {
        return;
    }

    if (enemy->GetEnemyType() == "GiantSlime") {
        enemy->SetScale({ 3.6f, 3.6f, 3.6f });
    }
    else {
        enemy->SetScale({ 2.0f, 2.0f, 2.0f });
    }
    enemy->SetMaterialType(kSlimeSoftMaterialType);
}

void ReadFloat(const json& source, const char* key, float& value) {
    if (source.is_object() && source.contains(key) && source.at(key).is_number()) {
        value = source.at(key).get<float>();
    }
}

void ReadBool(const json& source, const char* key, bool& value) {
    if (source.is_object() && source.contains(key) && source.at(key).is_boolean()) {
        value = source.at(key).get<bool>();
    }
}

bool ReadVector3(const json& source, const char* key, Vector3& value) {
    if (!source.is_object() || !source.contains(key) || !source.at(key).is_array()) {
        return false;
    }
    const json& array = source.at(key);
    if (array.size() < 3 || !array[0].is_number() || !array[1].is_number() || !array[2].is_number()) {
        return false;
    }
    value = { array[0].get<float>(), array[1].get<float>(), array[2].get<float>() };
    return true;
}

bool IsStatusScaleEnabled(const Vector3& scale) {
    return scale.x > 0.0f && scale.y > 0.0f && scale.z > 0.0f;
}

std::string ReadString(const json& source, const char* key, const std::string& fallback = "") {
    if (source.is_object() && source.contains(key) && source.at(key).is_string()) {
        return source.at(key).get<std::string>();
    }
    return fallback;
}

void ApplyGameplayStatusPreset(BaseEnemy* enemy) {
    if (!enemy || enemy->GetEnemyType().empty()) {
        return;
    }

    std::ifstream file("Resources/json/gameplay/status_presets.json");
    if (!file) {
        return;
    }

    try {
        json root;
        file >> root;
        if (!root.contains("enemies") || !root["enemies"].is_object()) {
            return;
        }

        const std::string enemyType = enemy->GetEnemyType();
        const json& enemies = root["enemies"];
        if (!enemies.contains(enemyType) || !enemies.at(enemyType).is_object()) {
            return;
        }

        const json& preset = enemies.at(enemyType);
        if (!enemy->param_.has_value()) {
            enemy->param_.emplace();
        }

        auto& p = enemy->param_.value();
        ReadFloat(preset, "hp", p.hp);
        ReadFloat(preset, "maxHp", p.maxHp);
        ReadFloat(preset, "attackPower", p.attackPower);
        ReadFloat(preset, "speed", p.speed);
        ReadFloat(preset, "gravity", p.gravity);
        ReadFloat(preset, "jumpPower", p.jumpPower);
        ReadFloat(preset, "detectionRange", p.detectionRange);
        ReadBool(preset, "morphLimited", p.morphLimited);
        ReadFloat(preset, "morphDuration", p.morphDuration);
        p.maxHp = (std::max)(p.maxHp, 1.0f);
        p.hp = (std::max)(p.hp, 0.0f);
        if (p.hp > p.maxHp) {
            p.maxHp = p.hp;
        }
        p.attackPower = (std::max)(p.attackPower, 0.0f);
        p.speed = (std::max)(p.speed, 0.0f);
        p.jumpPower = (std::max)(p.jumpPower, 0.0f);
        p.detectionRange = (std::max)(p.detectionRange, 0.0f);
        p.morphDuration = (std::max)(p.morphDuration, 0.1f);
        p.enemyType = enemyType;
        enemy->SetDetectionRange(p.detectionRange);

        const std::string modelName = ReadString(preset, "modelName");
        if (!modelName.empty()) {
            enemy->SetModel(modelName);
        }

        Vector3 scale = { 0.0f, 0.0f, 0.0f };
        if (ReadVector3(preset, "scale", scale) && IsStatusScaleEnabled(scale)) {
            enemy->SetScale(scale);
        }
    } catch (...) {
    }
}
}

EnemyFactory* EnemyFactory::GetInstance() {
    static EnemyFactory instance;
    return &instance;
}

// 敵タイプ名ごとに専用クラスを作り、基本ステータスをここでまとめて設定する
std::unique_ptr<BaseEnemy> EnemyFactory::CreateEnemy(const std::string& enemyName, Object3dCommon* common) {
    std::unique_ptr<BaseEnemy> newEnemy = nullptr;
    if (enemyName == "Slime") {
        auto slime = std::make_unique<EnemySlime>();

        // モデル読み込みと共通初期化
        slime->Initialize(common, "Characters/slime_pink");

        if (!slime->param_.has_value()) {
            slime->param_.emplace();
        }

        // 基本スライムのステータス
        auto& p = slime->param_.value();
        p.hp = 50.0f;          // 体力
        p.maxHp = 50.0f;       // 最大体力
        p.attackPower = 1.0f;  // 攻撃力倍率
        p.speed = 0.1f;        // 移動速度
        p.gravity = 60.0f;     // 重力 
        p.jumpPower = 18.0f;

        newEnemy = std::move(slime);
    }
    else if (enemyName == "BossCore") 
    {
        auto boss = std::make_unique<BossCore>();

        boss->SetSceneManager(SceneManager::GetInstance());
        // ボスはブロックモデルをコア/パーツ制御の基準として使う
        boss->Initialize(common, "Stages/block");

        if (!boss->param_.has_value()) {
            boss->param_.emplace();
        }

        // ボス用ステータス設定
        auto& p = boss->param_.value();
        p.hp = 1000.0f;        // ボスなので体力多め
        p.maxHp = 1000.0f;
        p.attackPower = 1.5f;
        p.speed = 0.05f;       // ゆっくり動く、あるいは浮遊など
        p.gravity = 0.0f;      // 浮遊ボスなので重力は切る

        newEnemy = std::move(boss);
    }
    else if (enemyName == "Bomb")
    {
        auto bomb = std::make_unique<EnemyBomb>();
        
        bomb->Initialize(common, "Gimmicks/blob");

        if (!bomb->param_.has_value()) {
            bomb->param_.emplace();
        }

        // ボム用ステータス設定
        auto& p = bomb->param_.value();
        p.hp = 30.0f;          // 体力
        p.maxHp = 30.0f;
        p.attackPower = 1.0f;
        p.speed = 0.04f;       // スライムよりやや遅くじわじわ追いかける
        p.gravity = 60.0f;     // 通常重力
        p.jumpPower = 16.0f;

        newEnemy = std::move(bomb);
    }

    else if (enemyName == "Bomber")
    {
        auto bomber = std::make_unique<EnemyBomber>();

        bomber->Initialize(common, "Characters/slime_black");

        if (!bomber->param_.has_value()) {
            bomber->param_.emplace();
        }

        auto& p = bomber->param_.value();
        p.hp = 60.0f;
        p.maxHp = 60.0f;
        p.attackPower = 1.15f;
        p.speed = 0.0f;       // 自分で歩かず、足運び処理側で距離を調整する
        p.gravity = 60.0f;
        p.jumpPower = 16.0f;
        p.detectionRange = 32.0f;
        bomber->SetDetectionRange(p.detectionRange);

        newEnemy = std::move(bomber);
    }
    else if (enemyName == "Mushroom")
    {
        auto mushroom = std::make_unique<EnemyMushroom>();
        mushroom->Initialize(common, "Primitives/cylinder");

        if (!mushroom->param_.has_value()) {
            mushroom->param_.emplace();
        }

        auto& p = mushroom->param_.value();
        p.hp = 35.0f;
        p.maxHp = 35.0f;
        p.attackPower = 1.0f;
        p.speed = 2.1f;
        p.gravity = 60.0f;
        p.jumpPower = 16.0f;
        p.detectionRange = 16.0f;
        mushroom->SetDetectionRange(p.detectionRange);

        newEnemy = std::move(mushroom);
    }
    else if (enemyName == "FireSlime")
    {
        auto fireSlime = std::make_unique<EnemyFireSlime>();
        fireSlime->Initialize(common, "Characters/slime_red");

        if (!fireSlime->param_.has_value()) {
            fireSlime->param_.emplace();
        }

        auto& p = fireSlime->param_.value();
        p.hp = 45.0f;
        p.maxHp = 45.0f;
        p.attackPower = 1.0f;
        p.speed = 2.35f;
        p.gravity = 60.0f;
        p.jumpPower = 18.0f;
        p.detectionRange = 24.0f;
        fireSlime->SetDetectionRange(p.detectionRange);

        newEnemy = std::move(fireSlime);
    }
    else if (enemyName == "ThunderSlime")
    {
        auto thunderSlime = std::make_unique<EnemyThunderSlime>();
        thunderSlime->Initialize(common, "Characters/slime_yellow");

        if (!thunderSlime->param_.has_value()) {
            thunderSlime->param_.emplace();
        }

        auto& p = thunderSlime->param_.value();
        p.hp = 45.0f;
        p.maxHp = 45.0f;
        p.attackPower = 1.0f;
        p.speed = 3.0f;
        p.gravity = 62.0f;
        p.jumpPower = 18.0f;
        p.detectionRange = 20.0f;
        thunderSlime->SetDetectionRange(p.detectionRange);

        newEnemy = std::move(thunderSlime);
    }
    else if (enemyName == "GiantSlime")
    {
        auto giantSlime = std::make_unique<EnemyGiantSlime>();
        giantSlime->Initialize(common, "Characters/slime");

        if (!giantSlime->param_.has_value()) {
            giantSlime->param_.emplace();
        }

        auto& p = giantSlime->param_.value();
        p.hp = 160.0f;
        p.maxHp = 160.0f;
        p.attackPower = 1.25f;
        p.speed = 0.0f;
        p.gravity = 70.0f;
        p.jumpPower = 24.0f;
        p.detectionRange = 26.0f;
        giantSlime->SetDetectionRange(p.detectionRange);

        newEnemy = std::move(giantSlime);
    }
    else if (enemyName == "Bat")
    {
        auto bat = std::make_unique<EnemyBat>();
        bat->Initialize(common, "Characters/bat");

        if (!bat->param_.has_value()) {
            bat->param_.emplace();
        }

        auto& p = bat->param_.value();
        p.hp = 25.0f;
        p.maxHp = 25.0f;
        p.attackPower = 0.8f;
        p.speed = 2.6f;
        p.gravity = 0.0f;
        p.detectionRange = 24.0f;
        bat->SetDetectionRange(p.detectionRange);

        newEnemy = std::move(bat);
    }
    else if (enemyName == "BeamDrone")
    {
        auto beamDrone = std::make_unique<EnemyBeamDrone>();
        beamDrone->Initialize(common, "Characters/eye");

        if (!beamDrone->param_.has_value()) {
            beamDrone->param_.emplace();
        }

        auto& p = beamDrone->param_.value();
        p.hp = 45.0f;
        p.maxHp = 45.0f;
        p.attackPower = 1.0f;
        p.speed = 4.0f;
        p.gravity = 0.0f;
        p.detectionRange = 30.0f;
        beamDrone->SetDetectionRange(p.detectionRange);

        newEnemy = std::move(beamDrone);
    }
    // 作った敵にタイプ名を保存し、タイプ共通の見た目補正をかける
    if (newEnemy) {
        newEnemy->SetEnemyType(enemyName);
        ApplySlimeDefaults(newEnemy.get());
        ApplyGameplayStatusPreset(newEnemy.get());
    } else {
        // 未登録タイプの場合は、落ちずに確認できる仮の敵を置く
        newEnemy = std::make_unique<BaseEnemy>();
        newEnemy->Initialize(common, "Primitives/cube");
        newEnemy->SetEnemyType("");
    }

    return newEnemy;
}
