#include "BuiltInCreatePresetRegistry.h"

#include "EnemyFactory.h"
#include "GimmickFactory.h"
#include "ItemFactory.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "PresetManager.h"
#include "json.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace {
bool RegisterOrRefreshEnemyPreset(const std::string& presetName, std::unique_ptr<Object3d> object, const std::string& displayName) {
    if (!object) {
        return false;
    }

    PresetManager* manager = PresetManager::GetInstance();
    if (manager->HasPreset(presetName)) {
        const json& current = manager->GetPreset(presetName);
        if (!current.value("builtinCreateTemplate", false)) {
            return false;
        }
    }

    object->SetName(displayName);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    json data = object->ExportToJson();
    data["name"] = displayName;
    data["builtinCreateTemplate"] = true;

    if (manager->HasPreset(presetName) && manager->GetPreset(presetName) == data) {
        return false;
    }

    manager->GetPreset(presetName) = data;
    return true;
}

bool RegisterPresetOnce(const std::string& presetName, std::unique_ptr<Object3d> object, const std::string& displayName) {
    if (!object || PresetManager::GetInstance()->HasPreset(presetName)) {
        return false;
    }

    object->SetName(displayName);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    json data = object->ExportToJson();
    data["name"] = displayName;
    data["builtinCreateTemplate"] = true;
    PresetManager::GetInstance()->GetPreset(presetName) = data;
    return true;
}

std::unique_ptr<Object3d> CreateSpriteCardPreset(Object3dCommon* common) {
    auto object = std::make_unique<Object3d>();
    object->Initialize(common);
    object->SetClassName("Model");
    object->SetModel("Primitives/plane");
    object->SetName("SpriteCard_2_5D");
    object->SetScale({ 1.8f, 1.8f, 1.0f });
    object->SetTexture("Resources/sprite/common/white.png");
    object->SetBlendMode(BlendMode::kNormal);
    object->SetMaterialType(0);
    object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    object->SetEnableLighting(false);
    object->SetEnableEnvMap(false);
    object->SetEmissive(1.35f);
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);

    ColliderConfig colliderConfig = object->GetColliderConfig();
    colliderConfig.type = ColliderType::kNone;
    object->SetColliderConfig(colliderConfig);

    return object;
}
}

void BuiltInCreatePresetRegistry::EnsureRegistered(Object3dCommon* common) {
    if (!common) {
        return;
    }

    bool changed = false;

    const std::vector<std::pair<std::string, std::string>> enemies = {
        { "FireSlime", "ファイアスライム" },
        { "ThunderSlime", "サンダースライム" },
        { "WindSlime", "風スライム" },
        { "Slime", "ピンクスライム" },
        { "Bomb", "ボム" },
        { "Bomber", "ボムスライム" },
        { "Mushroom", "キノコ" },
        { "GiantSlime", "巨大スライム" },
        { "PrismSlime", "プリズムスライム（中ボス）" },
        { "Bat", "コウモリ" },
        { "BeamDrone", "ビームドローン" },
        { "BossCore", "ボスコア" },
    };
    for (const auto& [type, label] : enemies) {
        changed |= RegisterOrRefreshEnemyPreset("Builtin/Enemy/" + type, EnemyFactory::GetInstance()->CreateEnemy(type, common), label);
    }

    const std::vector<std::pair<std::string, std::string>> gimmicks = {
        { "FireCannon", "火炎砲台" },
        { "BreakableBlock", "破壊ブロック" },
        { "MovingFloor", "動く床" },
        { "Trampoline", "ジャンプ床" },
        { "SinkingFloor", "沈む床" },
        { "SeesawFloor", "シーソー床" },
        { "DashPanel", "ダッシュパネル" },
        { "IceFloor", "氷の床" },
        { "TimedSwitch", "時間スイッチ" },
        { "AppearingFloor", "出現床" },
        { "Switch", "汎用スイッチ" },
        { "EventReceiver", "イベント受信" },
        { "HookAnchor", "フックアンカー" },
        { "HookPullBlock", "フック可動ブロック" },
        { "OneWayFloor", "一方通行床" },
        { "LiquidLevel", "水位/マグマ上下" },
        { "MagmaHazard", "マグマダメージ床" },
        { "MagmaGeyser", "周期式マグマ噴出口" },
        { "ChainCollapseFloor", "連鎖崩れ床" },
        { "RotatingFloor", "回転床" },
        { "RotatingPillar", "回転柱" },
        { "PhaseFlipFloor", "時間反転床" },
        { "LaserEmitter", "レーザー発射" },
        { "LaserNode", "レーザーノード" },
        { "StageGate", "ステージゲート" },
    };
    for (const auto& [type, label] : gimmicks) {
        changed |= RegisterPresetOnce("Builtin/Gimmick/" + type, GimmickFactory::GetInstance()->CreateGimmick(type, common), label);
    }

    changed |= RegisterPresetOnce("Builtin/Item/Heal", ItemFactory::GetInstance()->CreateItem("Heal", common), "体力回復");
    changed |= RegisterPresetOnce("Builtin/Utility/SpriteCard2_5D", CreateSpriteCardPreset(common), "2.5Dスプライト板");

    if (changed) {
        PresetManager::GetInstance()->SaveAll();
    }
}
