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
json Object3d::ExportToJson() {
    json d;

    // 1. 基本設定
    d["name"] = name_;
    d["modelName"] = GetModelName();
    d["type"] = className_;
    d["tag"] = tag_;
    d["layer"] = layer_.empty() ? "Default" : layer_;
    d["saveCategory"] = saveCategory_;
    d["enemyType"] = enemyType_;
    d["gimmickType"] = gimmickType_;
    d["itemType"] = itemType_;
    d["isVisible"] = isVisible_;
    d["isLocked"] = isLocked_;
    d["castShadow"] = castShadow_;

    // 2. Transform
    d["translate"] = { transform_.translate.x, transform_.translate.y, transform_.translate.z };
    d["scale"] = { transform_.scale.x, transform_.scale.y, transform_.scale.z };
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

    // 5. Stats (Param)
    if (param_.has_value()) {
        auto& p = param_.value();
        d["param"]["hp"] = p.hp;
        d["param"]["maxHp"] = p.maxHp;
        d["param"]["speed"] = p.speed;
        json jp;
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
        d["param"] = jp;
    }

    // 6. グラフィックス・マテリアル
    Vector4 col = GetColor();
    d["color"] = { col.x, col.y, col.z, col.w };
    d["blendMode"] = static_cast<int>(GetBlendMode());
    d["materialType"] = GetMaterialType();
    d["meshDrawIndex"] = GetMeshDrawIndex();

    // ★追加: 金属度と粗さ
    d["metallic"] = GetMetallic();
    d["roughness"] = GetRoughness();

    d["meshEffect1"] = meshEffectName1_;
    d["meshEffect2"] = meshEffectName2_;

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
    if (GetMaterialType() >= 8 && GetMaterialType() <= 22 && GetMeshRenderer() && GetMeshRenderer()->GetWaterParamData()) {
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

    // 8. レコーダー (Ghost)
    d["recorder"]["recordPathName"] = recordPathName_;
    d["recorder"]["isRecordLoop"] = isRecordLoop_;
    d["recorder"]["isRecordRelative"] = isRecordRelative_;

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
    if (j.contains("modelName")) SetModel(j["modelName"].get<std::string>());
    if (j.contains("type")) className_ = j["type"];
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
    if (j.contains("castShadow")) castShadow_ = j["castShadow"].get<bool>();

    // 2. Transform
    if (j.contains("translate")) transform_.translate = { j["translate"][0], j["translate"][1], j["translate"][2] };
    if (j.contains("scale")) transform_.scale = { j["scale"][0], j["scale"][1], j["scale"][2] };

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
     if (j.contains("targetID")) SetTargetID(j["targetID"]);
     if (j.contains("myEventID")) SetEventID(j["myEventID"]);

    // 5. Stats (Param)
    if (j.contains("param")) {
        EntityParameter p;
        const auto& jp = j["param"];
        const bool hasHp = jp.contains("hp");
        const bool hasMaxHp = jp.contains("maxHp");
        if (hasHp) p.hp = jp["hp"];
        if (hasMaxHp) p.maxHp = jp["maxHp"];
        if (jp.contains("attackPower")) p.attackPower = jp["attackPower"];
        if (jp.contains("speed")) p.speed = jp["speed"];
        if (jp.contains("gravity")) p.gravity = jp["gravity"];
        if (jp.contains("jumpPower")) p.jumpPower = jp["jumpPower"];
        if (jp.contains("maxFallSpeed")) p.maxFallSpeed = jp["maxFallSpeed"];
        if (jp.contains("morphLimited")) p.morphLimited = jp["morphLimited"];
        if (jp.contains("morphDuration")) p.morphDuration = jp["morphDuration"];
        if (jp.contains("enemyType")) p.enemyType = jp["enemyType"];
        if (jp.contains("gimmickType")) p.gimmickType = jp["gimmickType"];
        if (jp.contains("itemType")) p.itemType = jp["itemType"];
        if (jp.contains("healAmount")) p.healAmount = jp["healAmount"];
        if (jp.contains("interval")) p.interval = jp["interval"];
        if (jp.contains("maxCount")) p.maxCount = jp["maxCount"];
        if (jp.contains("detectionRange")) p.detectionRange = jp["detectionRange"];
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
        p.maxHp = (std::max)(p.maxHp, 1.0f);
        if (hasMaxHp && !hasHp) {
            p.hp = p.maxHp;
        }
        p.hp = (std::max)(p.hp, 0.0f);
        if (p.hp > p.maxHp) {
            p.maxHp = p.hp;
        }
        p.attackPower = (std::max)(p.attackPower, 0.0f);
        param_ = p;
    }

    // 6. グラフィックス・マテリアル
    if (j.contains("color")) SetColor({ j["color"][0], j["color"][1], j["color"][2], j["color"][3] });
    if (j.contains("blendMode")) SetBlendMode(static_cast<BlendMode>(j["blendMode"]));
    if (j.contains("materialType")) SetMaterialType(j["materialType"]);
    if (j.contains("meshDrawIndex")) SetMeshDrawIndex(j["meshDrawIndex"].get<int>());

    // ★追加: 金属度と粗さ
    if (j.contains("metallic")) SetMetallic(j["metallic"].get<float>());
    if (j.contains("roughness")) SetRoughness(j["roughness"].get<float>());

    if (j.contains("meshEffect1")) meshEffectName1_ = j["meshEffect1"].get<std::string>();
    if (j.contains("meshEffect2")) meshEffectName2_ = j["meshEffect2"].get<std::string>();

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
    if (j.contains("waterParam") && GetMaterialType() >= 8 && GetMaterialType() <= 22) {
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

    // 7. アニメーション
    if (j.contains("animation")) {
        const auto& anim = j["animation"];
        if (anim.contains("animName")) animName_ = anim["animName"];
        if (anim.contains("isAnimLoop")) isAnimLoop_ = anim["isAnimLoop"];
        if (anim.contains("recordPathName")) recordPathName_ = anim["recordPathName"]; // 互換性
        if (anim.contains("isAnimRelative")) isRecordRelative_ = anim["isAnimRelative"]; // 互換性
    }

    // 8. レコーダー (Ghost)
    if (j.contains("recorder")) {
        const auto& rec = j["recorder"];
        if (rec.contains("recordPathName")) recordPathName_ = rec["recordPathName"];
        if (rec.contains("isRecordLoop")) isRecordLoop_ = rec["isRecordLoop"];
        if (rec.contains("isRecordRelative")) isRecordRelative_ = rec["isRecordRelative"];
    }
    if (recorder_ && !recordPathName_.empty()) {
        bool isCinematic = (className_ == "CinematicCamera");
        recorder_->Play(recordPathName_, isRecordLoop_, isRecordRelative_, isCinematic);
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
