#define NOMINMAX
#include "Object3d.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "EffectObject3d.h"
#include "SRVManager.h"
#include "CameraManager.h"
#include "SceneManager.h"
#include "GhostRecorder.h"
#include "CollisionManager.h"
#include <cassert>
#include <algorithm> // min, max
#include <ParticleManager.h>
#include <GPUParticleManager.h>
#include <GPUParticleEmitter.h>
#include <DebugConsole.h>
#include <ProfilerManager.h>
#include <fstream>
#include <filesystem>
#include <initializer_list>

namespace {

std::filesystem::path ResolveTerrainCollisionFilePath(const std::string& path) {
    std::filesystem::path filePath(path);
    if (std::filesystem::exists(filePath)) {
        return filePath;
    }

    std::filesystem::path resourcesPath = std::filesystem::path("Resources") / filePath;
    if (std::filesystem::exists(resourcesPath)) {
        return resourcesPath;
    }

    return filePath;
}

bool HasParentInChain(Object3d* parent, const Object3d* child) {
    for (Object3d* current = parent; current != nullptr; current = current->GetParent()) {
        if (current == child) {
            return true;
        }
    }
    return false;
}

void ApplyMatrixToTransform(Transform& transform, const Matrix4x4& matrix) {
    const float epsilon = 0.0001f;

    Vector3 rowX = { matrix.m[0][0], matrix.m[0][1], matrix.m[0][2] };
    Vector3 rowY = { matrix.m[1][0], matrix.m[1][1], matrix.m[1][2] };
    Vector3 rowZ = { matrix.m[2][0], matrix.m[2][1], matrix.m[2][2] };

    transform.scale = {
        std::max(Math::Length(rowX), epsilon),
        std::max(Math::Length(rowY), epsilon),
        std::max(Math::Length(rowZ), epsilon),
    };
    transform.translate = { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };

    Matrix4x4 rotateMatrix = Math::MakeIdentity4x4();
    rotateMatrix.m[0][0] = matrix.m[0][0] / transform.scale.x;
    rotateMatrix.m[0][1] = matrix.m[0][1] / transform.scale.x;
    rotateMatrix.m[0][2] = matrix.m[0][2] / transform.scale.x;
    rotateMatrix.m[1][0] = matrix.m[1][0] / transform.scale.y;
    rotateMatrix.m[1][1] = matrix.m[1][1] / transform.scale.y;
    rotateMatrix.m[1][2] = matrix.m[1][2] / transform.scale.y;
    rotateMatrix.m[2][0] = matrix.m[2][0] / transform.scale.z;
    rotateMatrix.m[2][1] = matrix.m[2][1] / transform.scale.z;
    rotateMatrix.m[2][2] = matrix.m[2][2] / transform.scale.z;

    transform.quaternion = Math::MatrixToQuaternion(rotateMatrix);
    transform.rotate = Math::MatrixToEuler(rotateMatrix);
    transform.isQuaternionMaster = true;
}

}

// ========================================================================
// Object3d JSON保存・読み込み
// ------------------------------------------------------------------------
// Editor保存、シーンロード、Prefab的な複製で使うObject3dの入出力を担当する。
// 保存項目が増えた時は、このファイル内でExport/Importの対応をそろえる。
// ========================================================================
bool Object3d::HasComponentPresenceMarker(const std::string& componentTypeId) const {
    if (componentTypeId.empty()) {
        return false;
    }
    const auto component = opaqueComponents_.find(componentTypeId);
    return component != opaqueComponents_.end() && component->is_object() &&
        component->value("_editorPresent", false);
}

void Object3d::SetComponentPresenceMarker(const std::string& componentTypeId, bool present) {
    if (componentTypeId.empty()) {
        return;
    }

    if (present) {
        json& component = opaqueComponents_[componentTypeId];
        if (!component.is_object()) {
            component = json::object();
        }
        component["_editorPresent"] = true;
        return;
    }

    const auto component = opaqueComponents_.find(componentTypeId);
    if (component == opaqueComponents_.end() || !component->is_object()) {
        return;
    }
    component->erase("_editorPresent");
    if (component->empty()) {
        opaqueComponents_.erase(component);
    }
}

json Object3d::SerializeFeatureComponents() const {
    json components = opaqueComponents_.is_object() ? opaqueComponents_ : json::object();

    const auto clearKnownPayload = [&components](
        const std::string& typeId,
        std::initializer_list<const char*> knownKeys) {
        if (!components.contains(typeId) || !components[typeId].is_object()) {
            return;
        }
        json& payload = components[typeId];
        payload.erase("_editorPresent");
        payload.erase("version");
        for (const char* key : knownKeys) {
            payload.erase(key);
        }
        if (payload.empty()) {
            components.erase(typeId);
        } else {
            payload["_editorPresent"] = false;
        }
    };

    if (particleEmitterComponent_) {
        json& payload = components[std::string(kParticleEmitterComponentType)];
        if (!payload.is_object()) payload = json::object();
        payload["version"] = 1;
        payload["_editorPresent"] = true;
        payload["cpuParticle"] = particleEmitterComponent_->GetCpuParticle();
        payload["gpuParticle"] = particleEmitterComponent_->GetGpuParticle();
    } else {
        clearKnownPayload(std::string(kParticleEmitterComponentType), { "cpuParticle", "gpuParticle" });
    }

    if (meshEffectComponent_) {
        json& payload = components[std::string(kMeshEffectComponentType)];
        if (!payload.is_object()) payload = json::object();
        payload["version"] = 1;
        payload["_editorPresent"] = true;
        payload["primary"] = meshEffectComponent_->GetPrimaryEffect();
        payload["secondary"] = meshEffectComponent_->GetSecondaryEffect();
    } else {
        clearKnownPayload(std::string(kMeshEffectComponentType), { "primary", "secondary" });
    }

    if (pathMoverComponent_) {
        json& payload = components[std::string(kPathMoverComponentType)];
        if (!payload.is_object()) payload = json::object();
        payload["version"] = 1;
        payload["_editorPresent"] = true;
        payload["path"] = pathMoverComponent_->GetPathName();
        payload["loop"] = pathMoverComponent_->IsLoop();
        payload["relative"] = pathMoverComponent_->IsRelative();
    } else {
        clearKnownPayload(std::string(kPathMoverComponentType), { "path", "loop", "relative" });
    }

    if (gameplayLinkComponent_) {
        json& payload = components[std::string(kGameplayLinkComponentType)];
        if (!payload.is_object()) payload = json::object();
        payload["version"] = 1;
        payload["_editorPresent"] = true;
        payload["eventId"] = gameplayLinkComponent_->GetEventId();
        payload["targetId"] = gameplayLinkComponent_->GetTargetId();
    } else {
        clearKnownPayload(std::string(kGameplayLinkComponentType), { "eventId", "targetId" });
    }

    if (navAgentComponent_) {
        json& payload = components[std::string(kNavAgentComponentType)];
        if (!payload.is_object()) payload = json::object();
        payload["version"] = 1;
        payload["_editorPresent"] = true;
        payload["enabled"] = navAgentComponent_->IsEnabled();
        payload["cellSize"] = navAgentComponent_->GetCellSize();
        payload["agentRadius"] = navAgentComponent_->GetAgentRadius();
        payload["agentHeight"] = navAgentComponent_->GetAgentHeight();
        payload["searchPadding"] = navAgentComponent_->GetSearchPadding();
        payload["repathInterval"] = navAgentComponent_->GetRepathInterval();
        payload["stoppingDistance"] = navAgentComponent_->GetStoppingDistance();
        payload["obstacleMask"] = navAgentComponent_->GetObstacleMask();
        payload["allowDiagonal"] = navAgentComponent_->AllowsDiagonal();
    } else {
        clearKnownPayload(std::string(kNavAgentComponentType), {
            "enabled", "cellSize", "agentRadius", "agentHeight", "searchPadding",
            "repathInterval", "stoppingDistance", "obstacleMask", "allowDiagonal"
        });
    }

    return components;
}

void Object3d::DeserializeFeatureComponents(const json& objectData) {
    opaqueComponents_ = objectData.contains("components") && objectData["components"].is_object()
        ? objectData["components"]
        : json::object();

    particleEmitterComponent_.reset();
    meshEffectComponent_.reset();
    pathMoverComponent_.reset();
    gameplayLinkComponent_.reset();
    navAgentComponent_.reset();
    gpuEmitter_.reset();
    currentMeshEffect1_.clear();
    currentMeshEffect2_.clear();
    attachedEffects1_.clear();
    attachedEffects2_.clear();
    if (recorder_) recorder_->Stop();

    const auto getPayload = [this](std::string_view typeId) -> const json* {
        const std::string key(typeId);
        return opaqueComponents_.contains(key) && opaqueComponents_[key].is_object()
            ? &opaqueComponents_[key]
            : nullptr;
    };
    const auto resolvePresence = [](const json* payload, bool legacyPresent) {
        if (payload && payload->contains("_editorPresent") && (*payload)["_editorPresent"].is_boolean()) {
            return (*payload)["_editorPresent"].get<bool>();
        }
        return payload != nullptr || legacyPresent;
    };

    const json* particlePayload = getPayload(kParticleEmitterComponentType);
    const std::string legacyCpu = objectData.value("particleName", "");
    const std::string legacyGpu = objectData.value("gpuParticleName", "");
    if (resolvePresence(particlePayload, !legacyCpu.empty() || !legacyGpu.empty())) {
        ParticleEmitterComponent* component = EnsureParticleEmitterComponent();
        component->SetCpuParticle(
            particlePayload && particlePayload->contains("cpuParticle")
                ? particlePayload->value("cpuParticle", "") : legacyCpu);
        component->SetGpuParticle(
            particlePayload && particlePayload->contains("gpuParticle")
                ? particlePayload->value("gpuParticle", "") : legacyGpu);
    }

    const json* meshEffectPayload = getPayload(kMeshEffectComponentType);
    const std::string legacyPrimary = objectData.value("meshEffect1", "");
    const std::string legacySecondary = objectData.value("meshEffect2", "");
    if (resolvePresence(meshEffectPayload, !legacyPrimary.empty() || !legacySecondary.empty())) {
        MeshEffectComponent* component = EnsureMeshEffectComponent();
        component->SetPrimaryEffect(
            meshEffectPayload && meshEffectPayload->contains("primary")
                ? meshEffectPayload->value("primary", "") : legacyPrimary);
        component->SetSecondaryEffect(
            meshEffectPayload && meshEffectPayload->contains("secondary")
                ? meshEffectPayload->value("secondary", "") : legacySecondary);
    }

    const json* recorder = objectData.contains("recorder") && objectData["recorder"].is_object()
        ? &objectData["recorder"] : nullptr;
    const json* animation = objectData.contains("animation") && objectData["animation"].is_object()
        ? &objectData["animation"] : nullptr;
    std::string legacyPath;
    bool legacyLoop = false;
    bool legacyRelative = false;
    if (animation) {
        legacyPath = animation->value("recordPathName", "");
        legacyRelative = animation->value("isAnimRelative", false);
    }
    if (recorder) {
        legacyPath = recorder->value("recordPathName", legacyPath);
        legacyLoop = recorder->value("isRecordLoop", false);
        legacyRelative = recorder->value("isRecordRelative", legacyRelative);
    }
    const json* pathPayload = getPayload(kPathMoverComponentType);
    if (resolvePresence(pathPayload, !legacyPath.empty())) {
        PathMoverComponent* component = EnsurePathMoverComponent();
        component->SetPathName(
            pathPayload && pathPayload->contains("path")
                ? pathPayload->value("path", "") : legacyPath);
        component->SetLoop(
            pathPayload && pathPayload->contains("loop")
                ? pathPayload->value("loop", false) : legacyLoop);
        component->SetRelative(
            pathPayload && pathPayload->contains("relative")
                ? pathPayload->value("relative", false) : legacyRelative);
    }

    const int legacyEventId = objectData.value("myEventID", -1);
    const int legacyTargetId = objectData.value("targetID", -1);
    const json* linkPayload = getPayload(kGameplayLinkComponentType);
    if (resolvePresence(linkPayload, legacyEventId >= 0 || legacyTargetId >= 0)) {
        GameplayLinkComponent* component = EnsureGameplayLinkComponent();
        component->SetEventId(
            linkPayload && linkPayload->contains("eventId")
                ? linkPayload->value("eventId", -1) : legacyEventId);
        component->SetTargetId(
            linkPayload && linkPayload->contains("targetId")
                ? linkPayload->value("targetId", -1) : legacyTargetId);
    }

    const json* navPayload = getPayload(kNavAgentComponentType);
    if (resolvePresence(navPayload, false)) {
        NavAgentComponent* component = EnsureNavAgentComponent();
        if (navPayload) {
            component->SetCellSize(navPayload->value("cellSize", 1.0f));
            component->SetAgentRadius(navPayload->value("agentRadius", 0.55f));
            component->SetAgentHeight(navPayload->value("agentHeight", 1.5f));
            component->SetSearchPadding(navPayload->value("searchPadding", 5.0f));
            component->SetRepathInterval(navPayload->value("repathInterval", 0.35f));
            component->SetStoppingDistance(navPayload->value("stoppingDistance", 0.25f));
            component->SetObstacleMask(navPayload->value("obstacleMask", kAllSolid));
            component->SetAllowDiagonal(navPayload->value("allowDiagonal", true));
            component->SetEnabled(navPayload->value("enabled", true));
        }
    }
}

json Object3d::ExportToJson() {
    json d;
    const bool isManagedCharacter = className_ == "Player" || className_ == "Enemy";

    // 1. 基本設定
    d["guid"] = EnsurePersistentGuid();
    d["name"] = name_;
    const json components = SerializeFeatureComponents();
    if (!components.empty()) {
        d["components"] = components;
    }
    if (!isManagedCharacter) {
        d["modelName"] = GetModelName();
    }
    d["type"] = className_;
    d["tag"] = tag_;
    d["layer"] = layer_.empty() ? "Default" : layer_;
    d["saveCategory"] = saveCategory_;
    d["enemyType"] = enemyType_;
    d["gimmickType"] = gimmickType_;
    d["itemType"] = itemType_;
    d["isVisible"] = isVisible_;
    d["isLocked"] = isLocked_;
    d["isStatic"] = isStatic_;
    d["castShadow"] = castShadow_;
    if (prefabInstanceInfo_.IsLinked()) {
        d["prefabInstance"]["assetId"] = prefabInstanceInfo_.assetId;
        d["prefabInstance"]["prefabName"] = prefabInstanceInfo_.prefabName;
        d["prefabInstance"]["instanceId"] = prefabInstanceInfo_.instanceId;
        d["prefabInstance"]["sourceObjectId"] = prefabInstanceInfo_.sourceObjectId;
        d["prefabInstance"]["isRoot"] = prefabInstanceInfo_.isRoot;
    } else {
        // EditorのUndoスナップショットでは、Unpack済み状態を明示的に表します。
        d["prefabInstance"] = nullptr;
    }
    if (IsCameraObject()) {
        d["camera"] = SerializeSceneCameraSettings(sceneCameraSettings_);
    }

    // 2. Transform
    d["translate"] = { transform_.translate.x, transform_.translate.y, transform_.translate.z };
    if (!isManagedCharacter) {
        d["scale"] = { transform_.scale.x, transform_.scale.y, transform_.scale.z };
    }
    d["rotate"] = { transform_.rotate.x, transform_.rotate.y, transform_.rotate.z };
    d["quaternion"] = { transform_.quaternion.x, transform_.quaternion.y, transform_.quaternion.z, transform_.quaternion.w };

    // 3. Collider ＆ 衝突属性
    if (collider_) {
        const auto& c = collider_->GetConfig();
        d["collider"]["type"] = static_cast<int>(c.type);
        d["collider"]["size"] = { c.size.x, c.size.y, c.size.z };
        d["collider"]["center"] = { c.center.x, c.center.y, c.center.z };
        d["collider"]["rotation"] = { c.rotation.x, c.rotation.y, c.rotation.z };
    }
    d["collisionAttribute"] = GetCollisionAttribute();
    d["collisionMask"] = GetCollisionMask();
    if (!terrainCollisionPath_.empty()) {
        d["terrainCollisionPath"] = terrainCollisionPath_;
    }

    // 4. イベント関連
    d["eventType"] = static_cast<int>(eventType_);
     d["targetID"] = GetTargetID();
     d["myEventID"] = GetEventID();

    // 5. パラメータ。Player/Enemyの共通ステータスは配置プリセットへ保存しません。
    if (param_.has_value()) {
        auto& p = param_.value();
        json jp;
        if (isManagedCharacter) {
            if (className_ == "Enemy" && !p.enemyType.empty()) {
                jp["enemyType"] = p.enemyType;
            }
        } else {
            jp["hp"] = p.hp;
            jp["maxHp"] = p.maxHp;
            jp["attackPower"] = p.attackPower;
            jp["speed"] = p.speed;
            jp["gravity"] = p.gravity;
            jp["jumpPower"] = p.jumpPower;
            jp["maxFallSpeed"] = p.maxFallSpeed;
            jp["morphLimited"] = p.morphLimited;
            jp["morphDuration"] = p.morphDuration;
            jp["enemyType"] = p.enemyType;
            jp["gimmickType"] = p.gimmickType;
            jp["itemType"] = p.itemType;
            jp["healAmount"] = p.healAmount;
            jp["interval"] = p.interval;
            jp["maxCount"] = p.maxCount;
            jp["detectionRange"] = p.detectionRange;
            jp["colorType"] = p.colorType;
            jp["shakeDuration"] = p.shakeDuration;
            jp["fallDuration"] = p.fallDuration;
            jp["switchMode"] = p.switchMode;
            jp["actionMode"] = p.actionMode;
            jp["targetScene"] = p.targetScene;
            jp["moveAmount"] = p.moveAmount;
            jp["moveSpeed"] = p.moveSpeed;
            jp["startActive"] = p.startActive;
            jp["returnOnOff"] = p.returnOnOff;
        }
        if (!jp.empty()) {
            d["param"] = jp;
        }
    }

    // 6. グラフィックス・マテリアル
    if (!materialInstancePath_.empty()) {
        d["materialInstance"] = materialInstancePath_;
    }
    Vector4 col = GetColor();
    d["color"] = { col.x, col.y, col.z, col.w };
    d["blendMode"] = static_cast<int>(GetBlendMode());
    d["materialType"] = GetMaterialType();
    d["meshDrawIndex"] = GetMeshDrawIndex();

    // PBR用の金属度と粗さ。
    d["metallic"] = GetMetallic();
    d["roughness"] = GetRoughness();

    d["meshEffect1"] = GetMeshEffect1Name();
    d["meshEffect2"] = GetMeshEffect2Name();
    d["particleName"] = GetParticleName();
    d["gpuParticleName"] = GetGPUParticleName();

    d["enableNormalMap"] = GetEnableNormalMap();
    d["normalMapPath"] = GetNormalMapPath();
    d["ormMapPath"] = GetOrmMapPath();
    d["texturePath"] = GetTexturePath();
    Vector2 tiling = GetTextureTiling();
    d["textureTiling"] = { tiling.x, tiling.y };
    d["autoTextureTiling"] = GetAutoTextureTiling();
    d["enableLighting"] = GetEnableLighting();
    d["enableEnvMap"] = GetEnableEnvMap();
    d["envIntensity"] = GetEnvIntensity();
    d["emissive"] = GetEmissive();
    if (decalSettings_.enabled) {
        d["decal"] = {
            { "enabled", true },
            { "size", { decalSettings_.size.x, decalSettings_.size.y } },
            { "depthOffset", decalSettings_.depthOffset },
            { "lifetime", decalSettings_.lifetime },
            { "fadeIn", decalSettings_.fadeIn },
            { "fadeOut", decalSettings_.fadeOut },
            { "transient", decalSettings_.transient }
        };
    }
    if (HasLodLevels()) {
        json lodJson;
        lodJson["enabled"] = IsLodEnabled();
        lodJson["levels"] = json::array();
        for (const auto& lod : GetLodLevels()) {
            json levelJson;
            levelJson["level"] = lod.level;
            levelJson["modelName"] = lod.modelName;
            levelJson["distance"] = lod.distance;
            lodJson["levels"].push_back(levelJson);
        }
        d["lod"] = lodJson;
    }
    if (((GetMaterialType() >= 8 && GetMaterialType() <= 22) || GetMaterialType() == 26) && GetMeshRenderer() && GetMeshRenderer()->GetWaterParamData()) {
        auto* water = GetMeshRenderer()->GetWaterParamData();
        json jw;
        jw["waveSpeed"] = water->waveSpeed;
        jw["waveHeight"] = water->waveHeight;
        jw["waveFrequency"] = water->waveFrequency;
        jw["flowSpeedX"] = water->flowSpeedX;
        jw["flowSpeedY"] = water->flowSpeedY;
        jw["effectType"] = water->effectType;
        jw["effectScale"] = water->effectScale;
        jw["effectScaleX"] = water->effectScaleX;
        jw["effectScaleY"] = water->effectScaleY;
        jw["effectScaleZ"] = water->effectScaleZ;
        jw["effectSoftness"] = water->effectSoftness;
        jw["effectIntensity"] = water->effectIntensity;
        jw["billboardScale"] = water->billboardScale;
        d["waterParam"] = jw;
    }
    // 7. アニメーション
    d["animation"]["animName"] = animName_;
    d["animation"]["isAnimLoop"] = isAnimLoop_;
    d["animation"]["animatorController"] = animatorControllerPath_;

    // 8. レコーダー (Ghost)
    d["recorder"]["recordPathName"] = GetRecordPathName();
    d["recorder"]["isRecordLoop"] = IsRecordLoop();
    d["recorder"]["isRecordRelative"] = IsRecordRelative();

    // 9. ローカルフォグ
    if (auto* fogData = GetLocalFogData()) {
        d["localFog"]["color"] = { fogData->fogColor.x, fogData->fogColor.y, fogData->fogColor.z, fogData->fogColor.w };
        d["localFog"]["density"] = fogData->fogDensity;
        d["localFog"]["edgeFade"] = fogData->edgeFade;
        d["localFog"]["noiseSpeed"] = fogData->noiseSpeed;
        d["localFog"]["noiseScale"] = fogData->noiseScale;
        d["localFog"]["scatteringG"] = fogData->scatteringG;
        d["localFog"]["scatteringIntensity"] = fogData->scatteringIntensity;
    }

    return d;
}

void Object3d::ImportFromJson(const json& j) {
    // 1. 基本設定
    if (j.contains("guid") && j["guid"].is_string()) {
        SetPersistentGuid(j["guid"].get<std::string>());
    }
    EnsurePersistentGuid();
    // 実体Componentと未知Payloadを一度に復元し、旧Top-Level形式も受け入れます。
    DeserializeFeatureComponents(j);
    if (j.contains("type")) className_ = j["type"];
    const bool isManagedCharacter = className_ == "Player" || className_ == "Enemy";
    if (!isManagedCharacter && j.contains("modelName")) SetModel(j["modelName"].get<std::string>());
    if (j.contains("tag") && j["tag"].is_string()) {
        tag_ = j["tag"].get<std::string>();
    }
    if (j.contains("layer") && j["layer"].is_string()) {
        SetLayer(j["layer"].get<std::string>());
    } else if (layer_.empty()) {
        layer_ = "Default";
    }
    if (j.contains("saveCategory")) saveCategory_ = j["saveCategory"];
    if (j.contains("enemyType")) enemyType_ = j["enemyType"];
    if (j.contains("gimmickType")) gimmickType_ = j["gimmickType"];
    if (j.contains("itemType")) itemType_ = j["itemType"];
    if (j.contains("isVisible")) isVisible_ = j["isVisible"];
    if (j.contains("isLocked")) isLocked_ = j["isLocked"];
    if (j.contains("isStatic")) isStatic_ = j["isStatic"];
    if (j.contains("castShadow")) castShadow_ = j["castShadow"].get<bool>();
    if (j.contains("prefabInstance")) {
        if (j["prefabInstance"].is_object()) {
            const auto& prefab = j["prefabInstance"];
            prefabInstanceInfo_.assetId = prefab.value("assetId", "");
            prefabInstanceInfo_.prefabName = prefab.value("prefabName", "");
            prefabInstanceInfo_.instanceId = prefab.value("instanceId", "");
            prefabInstanceInfo_.sourceObjectId = prefab.value("sourceObjectId", "");
            prefabInstanceInfo_.isRoot = prefab.value("isRoot", false);
        } else if (j["prefabInstance"].is_null()) {
            prefabInstanceInfo_ = PrefabInstanceInfo{};
        }
    }
    if (IsCameraObject()) {
        // 旧CinematicCameraも読み込み時に新しいCamera Objectとして扱います。
        className_ = "Camera";
        saveCategory_ = "Camera";
        if (j.contains("camera")) {
            DeserializeSceneCameraSettings(j["camera"], sceneCameraSettings_);
        }
    }

    // 2. Transform
    if (j.contains("translate")) transform_.translate = { j["translate"][0], j["translate"][1], j["translate"][2] };
    if (!isManagedCharacter && j.contains("scale")) transform_.scale = { j["scale"][0], j["scale"][1], j["scale"][2] };

    if (j.contains("quaternion")) {
        transform_.quaternion = { j["quaternion"][0], j["quaternion"][1], j["quaternion"][2], j["quaternion"][3] };
        transform_.isQuaternionMaster = true;
        if (j.contains("rotate")) transform_.rotate = { j["rotate"][0], j["rotate"][1], j["rotate"][2] };
    }
    else if (j.contains("rotate")) {
        transform_.rotate = { j["rotate"][0], j["rotate"][1], j["rotate"][2] };
        transform_.isQuaternionMaster = false;
    }
    transform_.UpdateMatrix();

    // 3. Collider ＆ 衝突属性
    if (j.contains("collider") && collider_) {
        const auto& col = j["collider"];
        ColliderConfig config = collider_->GetConfig();
        if (col.contains("type")) config.type = static_cast<ColliderType>(col["type"]);
        if (col.contains("size")) config.size = { col["size"][0], col["size"][1], col["size"][2] };
        if (col.contains("center")) config.center = { col["center"][0], col["center"][1], col["center"][2] };
        if (col.contains("rotation")) config.rotation = { col["rotation"][0], col["rotation"][1], col["rotation"][2] };
        collider_->SetConfig(config);
    }
    if (j.contains("collisionAttribute")) SetCollisionAttribute(j["collisionAttribute"]);
    if (j.contains("collisionMask")) SetCollisionMask(j["collisionMask"]);
    if (j.contains("terrainCollisionPath") && j["terrainCollisionPath"].is_string()) {
        LoadTerrainCollisionFromFile(j["terrainCollisionPath"].get<std::string>());
    }

    // 4. イベント関連
    if (j.contains("eventType")) eventType_ = static_cast<EventType>(j["eventType"]);

    // 5. Stats (Param)
    if (j.contains("param")) {
        EntityParameter p = isManagedCharacter && param_.has_value() ? param_.value() : EntityParameter{};
        const auto& jp = j["param"];
        const bool hasHp = !isManagedCharacter && jp.contains("hp");
        const bool hasMaxHp = !isManagedCharacter && jp.contains("maxHp");
        if (hasHp) p.hp = jp["hp"];
        if (hasMaxHp) p.maxHp = jp["maxHp"];
        if (!isManagedCharacter && jp.contains("attackPower")) p.attackPower = jp["attackPower"];
        if (!isManagedCharacter && jp.contains("speed")) p.speed = jp["speed"];
        if (!isManagedCharacter && jp.contains("gravity")) p.gravity = jp["gravity"];
        if (!isManagedCharacter && jp.contains("jumpPower")) p.jumpPower = jp["jumpPower"];
        if (!isManagedCharacter && jp.contains("maxFallSpeed")) p.maxFallSpeed = jp["maxFallSpeed"];
        if (!isManagedCharacter && jp.contains("morphLimited")) p.morphLimited = jp["morphLimited"];
        if (!isManagedCharacter && jp.contains("morphDuration")) p.morphDuration = jp["morphDuration"];
        if (jp.contains("enemyType")) p.enemyType = jp["enemyType"];
        if (jp.contains("gimmickType")) p.gimmickType = jp["gimmickType"];
        if (jp.contains("itemType")) p.itemType = jp["itemType"];
        if (jp.contains("healAmount")) p.healAmount = jp["healAmount"];
        if (jp.contains("interval")) p.interval = jp["interval"];
        if (jp.contains("maxCount")) p.maxCount = jp["maxCount"];
        if (!isManagedCharacter && jp.contains("detectionRange")) p.detectionRange = jp["detectionRange"];
        if (jp.contains("colorType")) p.colorType = jp["colorType"];
        if (jp.contains("shakeDuration")) p.shakeDuration = jp["shakeDuration"];
        if (jp.contains("fallDuration")) p.fallDuration = jp["fallDuration"];
        if (jp.contains("switchMode")) p.switchMode = jp["switchMode"];
        if (jp.contains("actionMode")) p.actionMode = jp["actionMode"];
        if (jp.contains("targetScene")) p.targetScene = jp["targetScene"].get<std::string>();
        if (jp.contains("moveAmount")) p.moveAmount = jp["moveAmount"];
        if (jp.contains("moveSpeed")) p.moveSpeed = jp["moveSpeed"];
        if (jp.contains("startActive")) p.startActive = jp["startActive"];
        if (jp.contains("returnOnOff")) p.returnOnOff = jp["returnOnOff"];
        if (!isManagedCharacter) {
            p.maxHp = (std::max)(p.maxHp, 1.0f);
            if (hasMaxHp && !hasHp) {
                p.hp = p.maxHp;
            }
            p.hp = (std::max)(p.hp, 0.0f);
            if (p.hp > p.maxHp) {
                p.maxHp = p.hp;
            }
            p.attackPower = (std::max)(p.attackPower, 0.0f);
        }
        param_ = p;
    }

    // 6. グラフィックス・マテリアル
    if (j.contains("color")) SetColor({ j["color"][0], j["color"][1], j["color"][2], j["color"][3] });
    if (j.contains("blendMode")) SetBlendMode(static_cast<BlendMode>(j["blendMode"]));
    if (j.contains("materialType")) SetMaterialType(j["materialType"]);
    if (j.contains("meshDrawIndex")) SetMeshDrawIndex(j["meshDrawIndex"].get<int>());

    // PBR用の金属度と粗さ。
    if (j.contains("metallic")) SetMetallic(j["metallic"].get<float>());
    if (j.contains("roughness")) SetRoughness(j["roughness"].get<float>());


    if (j.contains("enableNormalMap")) SetEnableNormalMap(j["enableNormalMap"]);
    if (j.contains("normalMapPath")) SetNormalMap(j["normalMapPath"]);
    if (j.contains("ormMapPath")) SetOrmMap(j["ormMapPath"]);
    if (j.contains("texturePath")) SetTexture(j["texturePath"]);
    if (j.contains("textureTiling") && j["textureTiling"].is_array() && j["textureTiling"].size() >= 2) {
        SetTextureTiling({ j["textureTiling"][0].get<float>(), j["textureTiling"][1].get<float>() });
    }
    if (j.contains("autoTextureTiling")) SetAutoTextureTiling(j["autoTextureTiling"].get<bool>());
    if (j.contains("enableLighting")) SetEnableLighting(j["enableLighting"].get<bool>());
    if (j.contains("enableEnvMap")) SetEnableEnvMap(j["enableEnvMap"]);
    if (j.contains("envIntensity")) SetEnvIntensity(j["envIntensity"]);
    if (j.contains("emissive")) SetEmissive(j["emissive"].get<float>());
    if (j.contains("lod") && j["lod"].is_object()) {
        const auto& lodJson = j["lod"];
        const bool enabled = lodJson.value("enabled", true);
        if (lodJson.contains("levels") && lodJson["levels"].is_array()) {
            std::vector<LodLevel> levels;
            for (const auto& levelJson : lodJson["levels"]) {
                if (!levelJson.is_object()) continue;
                LodLevel level;
                level.level = levelJson.value("level", 0);
                level.modelName = levelJson.value("modelName", "");
                level.distance = levelJson.value("distance", 0.0f);
                if (level.level > 0 && !level.modelName.empty()) {
                    levels.push_back(level);
                }
            }
            SetLodLevels(levels);
        }
        SetLodEnabled(enabled);
    }
    if (j.contains("waterParam") && ((GetMaterialType() >= 8 && GetMaterialType() <= 22) || GetMaterialType() == 26)) {
        if (GetMeshRenderer() && GetMeshRenderer()->GetWaterParamData()) {
            auto* water = GetMeshRenderer()->GetWaterParamData();
            const auto& jw = j["waterParam"];
            if (jw.contains("waveSpeed")) water->waveSpeed = jw["waveSpeed"];
            if (jw.contains("waveHeight")) water->waveHeight = jw["waveHeight"];
            if (jw.contains("waveFrequency")) water->waveFrequency = jw["waveFrequency"];
            if (jw.contains("flowSpeedX")) water->flowSpeedX = jw["flowSpeedX"];
            if (jw.contains("flowSpeedY")) water->flowSpeedY = jw["flowSpeedY"];
            if (jw.contains("effectType")) water->effectType = jw["effectType"];
            if (jw.contains("effectScale")) water->effectScale = jw["effectScale"];
            if (jw.contains("effectScaleX")) water->effectScaleX = jw["effectScaleX"];
            if (jw.contains("effectScaleY")) water->effectScaleY = jw["effectScaleY"];
            if (jw.contains("effectScaleZ")) water->effectScaleZ = jw["effectScaleZ"];
            if (jw.contains("effectSoftness")) water->effectSoftness = jw["effectSoftness"];
            if (jw.contains("effectIntensity")) water->effectIntensity = jw["effectIntensity"];
            if (jw.contains("billboardScale")) water->billboardScale = jw["billboardScale"];
        }
    }

    if (j.contains("materialInstance") && j["materialInstance"].is_string()) {
        const std::string assetPath = j["materialInstance"].get<std::string>();
        std::string materialError;
        if (!ApplyMaterialInstance(assetPath, &materialError)) {
            // Assetが一時的に見つからなくても、Scene内の解決済み値とリンク先は保持します。
            materialInstancePath_ = assetPath;
        }
    }
    if (j.contains("decal") && j["decal"].is_object()) {
        const auto& decal = j["decal"];
        DecalSettings settings;
        settings.enabled = decal.value("enabled", true);
        if (decal.contains("size") && decal["size"].is_array() && decal["size"].size() >= 2) {
            settings.size = { decal["size"][0].get<float>(), decal["size"][1].get<float>() };
        }
        settings.depthOffset = decal.value("depthOffset", 0.012f);
        settings.lifetime = decal.value("lifetime", 0.0f);
        settings.fadeIn = decal.value("fadeIn", 0.0f);
        settings.fadeOut = decal.value("fadeOut", 0.35f);
        settings.transient = decal.value("transient", false);
        SetDecalSettings(settings);
        RestartDecalPlayback();
    }
    // 7. アニメーション
    if (j.contains("animation")) {
        const auto& anim = j["animation"];
        if (anim.contains("animName")) animName_ = anim["animName"];
        if (anim.contains("isAnimLoop")) isAnimLoop_ = anim["isAnimLoop"];
        if (anim.contains("animatorController")) {
            const std::string controller = anim.value("animatorController", "");
            if (!controller.empty()) {
                SetAnimatorController(controller);
            } else {
                ClearAnimatorController();
            }
        }
    }

    // 8. レコーダー (Ghost)
    if (recorder_ && !GetRecordPathName().empty()) {
        bool isCinematic = IsCameraObject();
        recorder_->Play(GetRecordPathName(), IsRecordLoop(), IsRecordRelative(), isCinematic);
    }

    // 9. ローカルフォグ
    if (j.contains("localFog")) {
        if (auto* fogData = GetLocalFogData()) {
            const auto& jf = j["localFog"];
            if (jf.contains("color")) fogData->fogColor = { jf["color"][0], jf["color"][1], jf["color"][2], jf["color"][3] };
            if (jf.contains("density")) fogData->fogDensity = jf["density"];
            if (jf.contains("edgeFade")) fogData->edgeFade = jf["edgeFade"];
            if (jf.contains("noiseSpeed")) fogData->noiseSpeed = jf["noiseSpeed"];
            if (jf.contains("noiseScale")) fogData->noiseScale = jf["noiseScale"];
            if (jf.contains("scatteringG")) fogData->scatteringG = jf["scatteringG"];
            if (jf.contains("scatteringIntensity")) fogData->scatteringIntensity = jf["scatteringIntensity"];
        }
    }
}
