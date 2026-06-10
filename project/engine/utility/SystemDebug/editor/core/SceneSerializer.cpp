#include "SceneSerializer.h"
#include "DebugEditor.h"
#include "Object3d.h"
#include "BaseScene.h"
#include "SceneManager.h"
#include "DebugConsole.h"
#include "Transform.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

void SceneSerializer::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

std::vector<SceneSerializer::SaveTarget> SceneSerializer::BuildSceneSaveTargets(const std::string& currentFilename, SaveMode mode) {
    std::vector<SaveTarget> targets;
    if (!editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) return targets;

    // ベース名の取得 (stage1.json -> stage1)
    std::string baseName = currentFilename;
    size_t extPos = baseName.find(".json");
    if (extPos != std::string::npos) baseName = baseName.substr(0, extPos);

    std::string basePath = "Resources/json/3Dobject/" + baseName;

    // カテゴリ別の箱
    json playerSceneData, enemySceneData, objectSceneData;
    playerSceneData["objects"] = json::array();
    enemySceneData["objects"] = json::array();
    objectSceneData["objects"] = json::array();

    auto& allObjects = editor_->GetSceneManager()->GetCurrentScene()->GetObjects();

    for (auto& obj : allObjects) {
        if (obj->GetName().empty()) continue;
        if (obj->GetClassName() == "EditorOnly" || obj->GetName().rfind("__Editor_", 0) == 0) continue;

        json d = SerializeObject(obj.get());

        // SaveCategoryを見て振り分け
        std::string cat = obj->GetSaveCategory();
        if (cat == "Player") playerSceneData["objects"].push_back(d);
        else if (cat == "Enemy") enemySceneData["objects"].push_back(d);
        else objectSceneData["objects"].push_back(d);
    }

    // モードに応じた書き出し
    if (mode == SaveMode::All || mode == SaveMode::Player) {
        targets.push_back({ "Player", basePath + "_player.json", playerSceneData, false });
    }
    if (mode == SaveMode::All || mode == SaveMode::Enemy) {
        targets.push_back({ "Enemy", basePath + "_enemy.json", enemySceneData, false });
    }
    if (mode == SaveMode::All || mode == SaveMode::Object) {
        targets.push_back({ "Object", basePath + "_object.json", objectSceneData, false });
    }

    // メタデータファイルの作成
    if (mode == SaveMode::All) {
        json dummyData;
        dummyData["_comment"] = "Actual data is in _player, _enemy, and _object.json";
        targets.push_back({ "Meta", "Resources/json/3Dobject/" + currentFilename, dummyData, true });
    }

    return targets;
}

std::vector<SceneSerializer::SaveTarget> SceneSerializer::BuildSingleObjectSaveTargets(Object3d* object, const std::string& filename) {
    std::vector<SaveTarget> targets;
    if (!object) return targets;

    // 1. ファイル名の自動振り分け
    std::string baseName = filename;
    size_t extPos = baseName.find(".json");
    if (extPos != std::string::npos) baseName = baseName.substr(0, extPos);

    std::string cat = object->GetSaveCategory();
    std::string targetFilename;
    if (cat == "Player") targetFilename = "Resources/json/3Dobject/" + baseName + "_player.json";
    else if (cat == "Enemy") targetFilename = "Resources/json/3Dobject/" + baseName + "_enemy.json";
    else targetFilename = "Resources/json/3Dobject/" + baseName + "_object.json";

    // 2. 既存JSONの読み込み
    json sceneData;
    std::ifstream inFile(targetFilename);
    if (inFile.is_open()) {
        try {
            if (inFile.peek() != std::ifstream::traits_type::eof()) inFile >> sceneData;
            else sceneData["objects"] = json::array();
        }
        catch (...) {
            sceneData["objects"] = json::array();
        }
        inFile.close();
    }
    else {
        sceneData["objects"] = json::array();
    }

    // 3. データの構築
    json currentData = SerializeObject(object);

    // 4. 配列内を探して更新 or 追加
    bool found = false;
    if (!sceneData.contains("objects") || !sceneData["objects"].is_array()) {
        sceneData["objects"] = json::array();
    }

    for (auto& objData : sceneData["objects"]) {
        if (objData.contains("name") && objData["name"] == object->GetName()) {
            objData = currentData;
            found = true;
            break;
        }
    }

    if (!found) {
        sceneData["objects"].push_back(currentData);
    }

    targets.push_back({ cat.empty() ? "Object" : cat, targetFilename, sceneData, false });
    return targets;
}

std::string SceneSerializer::SaveTargets(const std::vector<SaveTarget>& targets) {
    std::string savedFilesMsg;

    for (const auto& target : targets) {
        SaveToFile(target.path, target.data);
        if (!target.label.empty()) {
            savedFilesMsg += target.label + ", ";
        }
    }

    if (!savedFilesMsg.empty()) {
        savedFilesMsg.pop_back(); savedFilesMsg.pop_back();
    }
    return savedFilesMsg;
}

std::string SceneSerializer::SaveScene(const std::string& currentFilename, SaveMode mode) {
    return SaveTargets(BuildSceneSaveTargets(currentFilename, mode));
}

nlohmann::json SceneSerializer::SerializeObject(Object3d* obj) {
    json d;
    d["name"] = obj->GetName();

    // 1. 基本設定
    std::string className = obj->GetClassName();
    if (className.empty()) className = "Model";
    d["type"] = className;
    d["saveCategory"] = obj->GetSaveCategory();
    d["enemyType"] = obj->GetEnemyType();
    d["gimmickType"] = obj->GetGimmickType();
    d["itemType"] = obj->GetItemType();

    if (className != "InvisibleBox") {
        d["modelName"] = obj->GetModelName();
    }

    // 2. 親子関係
    d["parentName"] = obj->GetParent() ? obj->GetParent()->GetName() : "";

    // 3. Transform
    Transform* t = obj->GetTransform();
    d["position"] = { t->translate.x, t->translate.y, t->translate.z };
    d["rotation"] = { t->rotate.x, t->rotate.y, t->rotate.z };
    d["quaternion"] = { t->quaternion.x, t->quaternion.y, t->quaternion.z, t->quaternion.w };
    d["scale"] = { t->scale.x, t->scale.y, t->scale.z };

    // 4. Collider
    Object3d::ColliderConfig c = obj->GetColliderConfig();
    d["collider"]["type"] = (int)c.type;
    d["collider"]["center"] = { c.center.x, c.center.y, c.center.z };
    d["collider"]["size"] = { c.size.x, c.size.y, c.size.z };
    d["collider"]["rotation"] = { c.rotation.x, c.rotation.y, c.rotation.z };

    // 5. 衝突属性
    d["collisionAttribute"] = obj->GetCollisionAttribute();
    d["collisionMask"] = obj->GetCollisionMask();

    // 6. イベント関連
    d["eventID"] = static_cast<int>(obj->GetEventType());
    d["targetID"] = obj->GetTargetID();
    d["myEventID"] = obj->GetEventID();

    // 7. Stats (Param)
    if (obj->param_.has_value()) {
        auto& p = obj->param_.value();
        d["param"]["hp"] = p.hp;
        d["param"]["maxHp"] = p.maxHp;
        d["param"]["speed"] = p.speed;
        d["param"]["gravity"] = p.gravity;
        d["param"]["jumpPower"] = p.jumpPower;
        d["param"]["maxFallSpeed"] = p.maxFallSpeed;
        d["param"]["enemyType"] = p.enemyType;
        d["param"]["gimmickType"] = p.gimmickType;
        d["param"]["itemType"] = p.itemType;
        d["param"]["healAmount"] = p.healAmount;
        d["param"]["interval"] = p.interval;
        d["param"]["maxCount"] = p.maxCount;
        d["param"]["shakeDuration"] = p.shakeDuration;
        d["param"]["fallDuration"] = p.fallDuration;
        d["param"]["colorType"] = p.colorType;
        d["param"]["detectionRange"] = p.detectionRange;
        d["param"]["switchMode"] = p.switchMode;
        d["param"]["actionMode"] = p.actionMode;
        d["param"]["moveAmount"] = p.moveAmount;
        d["param"]["moveSpeed"] = p.moveSpeed;
        d["param"]["startActive"] = p.startActive;
        d["param"]["returnOnOff"] = p.returnOnOff;
    }

    // 8. グラフィックス
    Vector4 color = obj->GetColor();
    d["color"] = { color.x, color.y, color.z, color.w };
    d["blendMode"] = static_cast<int>(obj->GetBlendMode());
    d["materialType"] = obj->GetMaterialType();
    d["metallic"] = obj->GetMetallic();
    d["roughness"] = obj->GetRoughness();
    d["enableNormalMap"] = obj->GetEnableNormalMap();
    d["normalMapPath"] = obj->GetNormalMapPath();
    d["ormMapPath"] = obj->GetOrmMapPath();
    d["texturePath"] = obj->GetTexturePath();
    d["enableEnvMap"] = obj->GetEnableEnvMap();
    d["envIntensity"] = obj->GetEnvIntensity();
    d["emissive"] = obj->GetEmissive();
    d["particleName"] = obj->GetParticleName();
    d["gpuParticleName"] = obj->GetGPUParticleName();
    d["meshEffect1"] = obj->GetMeshEffect1Name();
    d["meshEffect2"] = obj->GetMeshEffect2Name();
    // 9. アニメーション
    d["animation"]["animName"] = obj->animName_;
    d["animation"]["isAnimLoop"] = obj->isAnimLoop_;

    // 10. レコーダー (Ghost)
    d["recorder"]["recordPathName"] = obj->recordPathName_;
    d["recorder"]["isRecordLoop"] = obj->isRecordLoop_;
    d["recorder"]["isRecordRelative"] = obj->isRecordRelative_;

    // 11. ローカルフォグ
    if (auto* fogData = obj->GetLocalFogData()) {
        d["localFog"]["color"] = { fogData->fogColor.x, fogData->fogColor.y, fogData->fogColor.z, fogData->fogColor.w };
        d["localFog"]["density"] = fogData->fogDensity;
        d["localFog"]["edgeFade"] = fogData->edgeFade;
        d["localFog"]["noiseSpeed"] = fogData->noiseSpeed;
        d["localFog"]["noiseScale"] = fogData->noiseScale;
        d["localFog"]["scatteringG"] = fogData->scatteringG;
        d["localFog"]["scatteringIntensity"] = fogData->scatteringIntensity;
    }
    if (obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 22 && obj->GetMeshRenderer() && obj->GetMeshRenderer()->GetWaterParamData()) {
        auto* water = obj->GetMeshRenderer()->GetWaterParamData();
        json jw;
        jw["waveSpeed"] = water->waveSpeed;
        jw["waveHeight"] = water->waveHeight;
        jw["waveFrequency"] = water->waveFrequency;
        jw["flowSpeedX"] = water->flowSpeedX;
        jw["flowSpeedY"] = water->flowSpeedY;
        jw["effectType"] = water->effectType;
        jw["effectScale"] = water->effectScale;
        jw["effectSoftness"] = water->effectSoftness;
        jw["effectIntensity"] = water->effectIntensity;
        jw["billboardScale"] = water->billboardScale;

        d["waterParam"] = jw;
    }
    return d;
}

// =========================================================
// 単体オブジェクトの更新保存 (UpdateObjectInSceneJSON)
// =========================================================
void SceneSerializer::UpdateObjectInSceneJSON(Object3d* object, const std::string& filename) {
    if (!object) return;

    SaveTargets(BuildSingleObjectSaveTargets(object, filename));
    DebugConsole::GetInstance()->AddLog("Updated object JSON: " + object->GetName());
}

void SceneSerializer::SaveToFile(const std::string& path, const json& data) {
    std::ofstream f(path);
    if (f.is_open()) {
        f << data.dump(4);
        DebugConsole::GetInstance()->AddLog("Saved JSON to " + path);
    }
}
