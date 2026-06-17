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
void RegisterBuiltInPreset(const std::string& presetName, std::unique_ptr<Object3d> object, const std::string& displayName) {
    if (!object || PresetManager::GetInstance()->HasPreset(presetName)) {
        return;
    }

    object->SetName(displayName);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    json data = object->ExportToJson();
    data["name"] = displayName;
    data["builtinCreateTemplate"] = true;
    PresetManager::GetInstance()->GetPreset(presetName) = data;
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

    const size_t presetCount = PresetManager::GetInstance()->GetPresets().size();

    const std::vector<std::pair<std::string, std::string>> enemies = {
        { "Slime", "スライム" },
        { "Bomb", "ボム" },
        { "Bomber", "ボマー" },
        { "Mushroom", "キノコ" },
        { "GiantSlime", "巨大スライム" },
        { "Bat", "コウモリ" },
        { "BeamDrone", "目玉ビーム" },
        { "BossCore", "ボスコア" },
    };
    for (const auto& [type, label] : enemies) {
        RegisterBuiltInPreset("Builtin/Enemy/" + type, EnemyFactory::GetInstance()->CreateEnemy(type, common), label);
    }

    const std::vector<std::pair<std::string, std::string>> gimmicks = {
        { "FireCannon", "火球砲台" },
        { "MovingFloor", "動く床" },
        { "Trampoline", "ジャンプ台" },
        { "SinkingFloor", "沈む床" },
        { "SeesawFloor", "シーソー床" },
        { "DashPanel", "ダッシュパネル" },
        { "IceFloor", "氷の床" },
        { "TimedSwitch", "時限スイッチ" },
        { "AppearingFloor", "出現床" },
        { "Switch", "汎用スイッチ" },
        { "EventReceiver", "イベント受信" },
        { "HookAnchor", "フックアンカー" },
        { "HookPullBlock", "フック可動ブロック" },
        { "OneWayFloor", "一方通行床" },
        { "LiquidLevel", "水位/マグマ上下" },
        { "ChainCollapseFloor", "連鎖崩れ床" },
        { "RotatingFloor", "回転床" },
        { "RotatingPillar", "回転柱" },
        { "PhaseFlipFloor", "時間反転床" },
        { "LaserEmitter", "レーザー発射" },
        { "LaserNode", "レーザーノード" },
        { "StageGate", "ステージゲート" },
    };
    for (const auto& [type, label] : gimmicks) {
        RegisterBuiltInPreset("Builtin/Gimmick/" + type, GimmickFactory::GetInstance()->CreateGimmick(type, common), label);
    }

    RegisterBuiltInPreset("Builtin/Item/Heal", ItemFactory::GetInstance()->CreateItem("Heal", common), "体力回復");
    RegisterBuiltInPreset("Builtin/Utility/SpriteCard2_5D", CreateSpriteCardPreset(common), "2.5Dスプライト板");

    if (PresetManager::GetInstance()->GetPresets().size() != presetCount) {
        PresetManager::GetInstance()->SaveAll();
    }
}
