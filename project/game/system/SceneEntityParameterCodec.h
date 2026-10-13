#pragma once

#include "GameplayStatusManager.h"
#include "Object3d.h"
#include "json.hpp"

#include <algorithm>

// Scene JSON の param と実行時設定の対応を保存・読込で共有します。
namespace SceneEntityParameterCodec {

inline nlohmann::json Serialize(const Object3d& object) {
    if (!object.param_) return nlohmann::json();
    const auto& p = *object.param_;
    nlohmann::json result = nlohmann::json::object();
    const std::string className = object.GetClassName();
    if (GameplayStatusManager::IsManagedCharacter(&object)) {
        if (className == "Enemy" && !p.enemyType.empty()) {
            result["enemyType"] = p.enemyType;
            if (p.enemyType == "PrismSlime" || p.enemyType == "MagmaSlime" || p.enemyType == "FalseKingSlime") {
                result["actionMode"] = p.actionMode;
                result["shakeDuration"] = p.shakeDuration;
                result["startActive"] = p.startActive;
            }
        }
        return result;
    }

#define SCENE_PARAM_WRITE(field) result[#field] = p.field
    SCENE_PARAM_WRITE(hp); SCENE_PARAM_WRITE(maxHp); SCENE_PARAM_WRITE(attackPower);
    SCENE_PARAM_WRITE(speed); SCENE_PARAM_WRITE(gravity); SCENE_PARAM_WRITE(jumpPower);
    SCENE_PARAM_WRITE(maxFallSpeed);
    SCENE_PARAM_WRITE(enemyType); SCENE_PARAM_WRITE(gimmickType); SCENE_PARAM_WRITE(itemType);
    SCENE_PARAM_WRITE(healAmount); SCENE_PARAM_WRITE(interval); SCENE_PARAM_WRITE(maxCount);
    SCENE_PARAM_WRITE(detectionRange); SCENE_PARAM_WRITE(shakeDuration); SCENE_PARAM_WRITE(fallDuration);
    SCENE_PARAM_WRITE(colorType); SCENE_PARAM_WRITE(switchMode); SCENE_PARAM_WRITE(actionMode);
    SCENE_PARAM_WRITE(targetScene); SCENE_PARAM_WRITE(moveAmount); SCENE_PARAM_WRITE(moveSpeed);
    SCENE_PARAM_WRITE(startActive); SCENE_PARAM_WRITE(returnOnOff);
    SCENE_PARAM_WRITE(volumeMode); SCENE_PARAM_WRITE(volumePayload);
    SCENE_PARAM_WRITE(volumeTriggerOnce); SCENE_PARAM_WRITE(volumeTriggerOnExit);
    SCENE_PARAM_WRITE(volumeRearmDelay); SCENE_PARAM_WRITE(volumeBlendDuration);
    SCENE_PARAM_WRITE(copyMemoryTypeA); SCENE_PARAM_WRITE(copyMemoryTypeB);
    SCENE_PARAM_WRITE(copyMemoryTypeC); SCENE_PARAM_WRITE(copyMemoryTypeD);
    SCENE_PARAM_WRITE(copyMemoryActivationRadius); SCENE_PARAM_WRITE(copyMemoryEssenceScale);
    SCENE_PARAM_WRITE(copyMemoryHoverAmount); SCENE_PARAM_WRITE(copyMemoryGlow);
    SCENE_PARAM_WRITE(copyMemoryUnlimitedDuration);
#undef SCENE_PARAM_WRITE
    return result;
}

inline void Apply(Object3d& object, const nlohmann::json& source) {
    if (!source.is_object()) return;
    if (!object.param_) object.param_.emplace();
    auto& p = *object.param_;
    const bool managed = GameplayStatusManager::IsManagedCharacter(&object);
    const bool hasHp = !managed && source.contains("hp");
    const bool hasMaxHp = !managed && source.contains("maxHp");

#define SCENE_PARAM_READ(field) if (source.contains(#field)) p.field = source[#field].get<decltype(p.field)>()
#define SCENE_PARAM_READ_UNMANAGED(field) if (!managed) { SCENE_PARAM_READ(field); }
    SCENE_PARAM_READ_UNMANAGED(hp); SCENE_PARAM_READ_UNMANAGED(maxHp);
    SCENE_PARAM_READ_UNMANAGED(attackPower); SCENE_PARAM_READ_UNMANAGED(speed);
    SCENE_PARAM_READ_UNMANAGED(gravity); SCENE_PARAM_READ_UNMANAGED(jumpPower);
    SCENE_PARAM_READ_UNMANAGED(maxFallSpeed); SCENE_PARAM_READ_UNMANAGED(morphLimited);
    SCENE_PARAM_READ_UNMANAGED(morphDuration); SCENE_PARAM_READ_UNMANAGED(detectionRange);
    SCENE_PARAM_READ(enemyType); SCENE_PARAM_READ(gimmickType); SCENE_PARAM_READ(itemType);
    SCENE_PARAM_READ(healAmount); SCENE_PARAM_READ(interval); SCENE_PARAM_READ(maxCount);
    SCENE_PARAM_READ(shakeDuration); SCENE_PARAM_READ(fallDuration); SCENE_PARAM_READ(colorType);
    SCENE_PARAM_READ(switchMode); SCENE_PARAM_READ(actionMode); SCENE_PARAM_READ(targetScene);
    SCENE_PARAM_READ(moveAmount); SCENE_PARAM_READ(moveSpeed); SCENE_PARAM_READ(startActive);
    SCENE_PARAM_READ(returnOnOff); SCENE_PARAM_READ(volumeMode);
    if (source.contains("volumePayload") && source["volumePayload"].is_string())
        p.volumePayload = source["volumePayload"].get<std::string>();
    SCENE_PARAM_READ(volumeTriggerOnce); SCENE_PARAM_READ(volumeTriggerOnExit);
    SCENE_PARAM_READ(volumeRearmDelay); SCENE_PARAM_READ(volumeBlendDuration);
    if (source.contains("copyMemoryTypeA") && source["copyMemoryTypeA"].is_string())
        p.copyMemoryTypeA = source["copyMemoryTypeA"].get<std::string>();
    if (source.contains("copyMemoryTypeB") && source["copyMemoryTypeB"].is_string())
        p.copyMemoryTypeB = source["copyMemoryTypeB"].get<std::string>();
    if (source.contains("copyMemoryTypeC") && source["copyMemoryTypeC"].is_string())
        p.copyMemoryTypeC = source["copyMemoryTypeC"].get<std::string>();
    if (source.contains("copyMemoryTypeD") && source["copyMemoryTypeD"].is_string())
        p.copyMemoryTypeD = source["copyMemoryTypeD"].get<std::string>();
    SCENE_PARAM_READ(copyMemoryActivationRadius); SCENE_PARAM_READ(copyMemoryEssenceScale);
    SCENE_PARAM_READ(copyMemoryHoverAmount); SCENE_PARAM_READ(copyMemoryGlow);
    SCENE_PARAM_READ(copyMemoryUnlimitedDuration);
#undef SCENE_PARAM_READ_UNMANAGED
#undef SCENE_PARAM_READ

    if (source.contains("volumeRearmDelay")) p.volumeRearmDelay = (std::max)(0.0f, p.volumeRearmDelay);
    if (source.contains("volumeBlendDuration")) p.volumeBlendDuration = (std::max)(0.0f, p.volumeBlendDuration);
    if (source.contains("copyMemoryActivationRadius"))
        p.copyMemoryActivationRadius = (std::max)(0.1f, p.copyMemoryActivationRadius);
    if (!managed) {
        p.maxHp = (std::max)(p.maxHp, 1.0f);
        if (hasMaxHp && !hasHp) p.hp = p.maxHp;
        p.hp = (std::max)(p.hp, 0.0f);
        if (p.hp > p.maxHp) p.maxHp = p.hp;
        p.attackPower = (std::max)(p.attackPower, 0.0f);
        if (object.GetClassName() == "Enemy" || !object.GetEnemyType().empty()) {
            p.morphDuration = (std::max)(p.morphDuration, 0.1f);
        }
    }
}

} // namespace SceneEntityParameterCodec
