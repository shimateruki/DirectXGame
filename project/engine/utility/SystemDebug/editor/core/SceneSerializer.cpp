#include "SceneSerializer.h"
#include "DebugEditor.h"
#include "Object3d.h"
#include "BaseScene.h"
#include "SceneManager.h"
#include "DebugConsole.h"
#include "Transform.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
constexpr const char* kSceneObjectDirectory = "Resources/json/3Dobject";
constexpr const char* kSceneSpriteDirectory = "Resources/json/sprite";
constexpr const char* kSceneCategorySuffixes[] = {
    "_player.json",
    "_enemy.json",
    "_object.json",
    "_camera.json"
};

std::string GetSceneBaseName(std::string filename) {
    const size_t slash = filename.find_last_of("/\\");
    if (slash != std::string::npos) {
        filename = filename.substr(slash + 1);
    }
    if (filename.size() >= 5 && filename.substr(filename.size() - 5) == ".json") {
        filename.resize(filename.size() - 5);
    }
    return filename;
}

bool HasSceneCategorySuffix(const std::string& filename) {
    for (const char* suffix : kSceneCategorySuffixes) {
        const size_t suffixLength = std::char_traits<char>::length(suffix);
        if (filename.size() >= suffixLength &&
            filename.compare(filename.size() - suffixLength, suffixLength, suffix) == 0) {
            return true;
        }
    }
    return false;
}

bool IsValidSceneId(const std::string& sceneId, std::string& errorMessage) {
    if (sceneId.empty()) {
        errorMessage = "Scene IDを入力してください。";
        return false;
    }
    if (sceneId.size() > 80) {
        errorMessage = "Scene IDは80文字以内にしてください。";
        return false;
    }
    for (unsigned char character : sceneId) {
        if (!std::isalnum(character) && character != '_' && character != '-') {
            errorMessage = "Scene IDには半角英数字、_、-だけを使用できます。";
            return false;
        }
    }
    const std::string filename = sceneId + ".json";
    if (HasSceneCategorySuffix(filename)) {
        errorMessage = "_player、_enemy、_object、_cameraで終わるIDは使用できません。";
        return false;
    }
    return true;
}

json ReadJsonObject(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        return json::object();
    }
    try {
        json data;
        stream >> data;
        return data.is_object() ? data : json::object();
    }
    catch (...) {
        return json::object();
    }
}

bool WriteJsonObject(const fs::path& path, const json& data, std::string& errorMessage) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) {
        errorMessage = "保存先フォルダーを作成できませんでした: " + path.parent_path().string();
        return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        errorMessage = "ファイルを作成できませんでした: " + path.string();
        return false;
    }
    stream << data.dump(4);
    if (!stream.good()) {
        errorMessage = "ファイルの書き込みに失敗しました: " + path.string();
        return false;
    }
    return true;
}

bool RegenerateCopiedSceneObjectGuids(
    const std::vector<fs::path>& paths,
    std::string& errorMessage) {
    struct SceneDocument {
        fs::path path;
        json data;
    };

    std::vector<SceneDocument> documents;
    std::unordered_map<std::string, std::string> guidRemap;
    for (const fs::path& path : paths) {
        if (!fs::exists(path)) continue;

        json data = ReadJsonObject(path);
        if (!data.contains("objects") || !data["objects"].is_array()) {
            continue;
        }
        for (json& object : data["objects"]) {
            if (!object.is_object()) continue;

            const std::string oldGuid = object.value("guid", std::string());
            const std::string newGuid = Object3d::GeneratePersistentGuid();
            object["guid"] = newGuid;
            if (Object3d::IsPersistentGuidValid(oldGuid)) {
                guidRemap.emplace(oldGuid, newGuid);
            }
        }
        documents.push_back({ path, std::move(data) });
    }

    for (SceneDocument& document : documents) {
        for (json& object : document.data["objects"]) {
            if (!object.is_object() || !object.contains("parentGuid") ||
                !object["parentGuid"].is_string()) {
                continue;
            }
            const auto parent = guidRemap.find(object["parentGuid"].get<std::string>());
            if (parent != guidRemap.end()) {
                object["parentGuid"] = parent->second;
            }
        }
        if (!WriteJsonObject(document.path, document.data, errorMessage)) {
            return false;
        }
    }
    return true;
}

json MakeEmptyObjectLayout() {
    json data;
    data["objects"] = json::array();
    return data;
}

json MakeEmptySpriteLayout() {
    json data;
    data["designResolution"] = { 1920.0f, 1080.0f };
    data["scaleToWindow"] = true;
    data["sprites"] = json::array();
    return data;
}

void ApplySceneAssetMetadata(
    json& data,
    const std::string& sceneId,
    const std::string& displayName,
    const std::string& runtimeScene) {
    data["_sceneAsset"]["version"] = 3;
    data["_sceneAsset"]["id"] = sceneId;
    data["_sceneAsset"]["displayName"] = displayName.empty() ? sceneId : displayName;
    data["_sceneAsset"]["runtimeScene"] = runtimeScene.empty() ? "SCENE_EDITOR" : runtimeScene;
    if (!data["_sceneAsset"].contains("controller")) {
        data["_sceneAsset"]["controller"] = "DEFAULT";
    }
    if (!data["_sceneAsset"].contains("resources") || !data["_sceneAsset"]["resources"].is_object()) {
        data["_sceneAsset"]["resources"] = json::object();
    }
    json& resources = data["_sceneAsset"]["resources"];
    if (!resources.contains("bgm")) resources["bgm"] = "";
    if (!resources.contains("light")) resources["light"] = "";
    if (!resources.contains("camera")) resources["camera"] = "";
    if (!resources.contains("skybox")) resources["skybox"] = "";
    data["_sceneAsset"]["spriteLayout"] =
        std::string(kSceneSpriteDirectory) + "/" + sceneId + "_sprite.json";
    if (!data.contains("objects")) {
        data["_comment"] = "Actual data is in _player, _enemy, _object, and _camera.json";
    }
}

std::vector<fs::path> BuildObjectAssetPaths(const std::string& sceneId) {
    const fs::path base = fs::path(kSceneObjectDirectory) / sceneId;
    return {
        fs::path(base.string() + ".json"),
        fs::path(base.string() + "_player.json"),
        fs::path(base.string() + "_enemy.json"),
        fs::path(base.string() + "_object.json"),
        fs::path(base.string() + "_camera.json")
    };
}

bool AnySceneDestinationExists(const std::string& sceneId) {
    for (const fs::path& path : BuildObjectAssetPaths(sceneId)) {
        if (fs::exists(path)) {
            return true;
        }
    }
    return fs::exists(fs::path(kSceneSpriteDirectory) / (sceneId + "_sprite.json")) ||
        fs::exists(fs::path(kSceneSpriteDirectory) / (sceneId + ".json"));
}

std::string ResolveExistingSpritePath(const std::string& sceneId) {
    const fs::path standard = fs::path(kSceneSpriteDirectory) / (sceneId + "_sprite.json");
    if (fs::exists(standard)) {
        return standard.generic_string();
    }
    const fs::path legacy = fs::path(kSceneSpriteDirectory) / (sceneId + ".json");
    if (fs::exists(legacy)) {
        return legacy.generic_string();
    }
    return {};
}

bool IsRuntimeReferencedScene(const std::string& sceneId, std::string& referenceDescription) {
    static const char* kFixedRuntimeScenes[] = {
        "titleScene",
        "stageSelect",
        "gameOverScene",
        "gameClearScene",
        "sample",
        "tutorial",
        "stage1",
        "stage2",
        "stage3",
        "stage4",
        "stage5"
    };
    for (const char* fixedScene : kFixedRuntimeScenes) {
        if (sceneId == fixedScene) {
            referenceDescription = "ゲーム用Sceneクラスから固定パスで参照されています。";
            return true;
        }
    }

    const fs::path stageDefinitions = "Resources/json/stage_select/stages.json";
    const json root = ReadJsonObject(stageDefinitions);
    if (!root.contains("stages") || !root["stages"].is_array()) {
        return false;
    }

    const std::string expectedLevelPath =
        (fs::path(kSceneObjectDirectory) / (sceneId + ".json")).generic_string();
    for (const json& stage : root["stages"]) {
        if (!stage.is_object()) {
            continue;
        }
        if (stage.value("levelPath", std::string()) == expectedLevelPath) {
            referenceDescription = "stages.jsonのステージ「" +
                stage.value("name", stage.value("id", sceneId)) + "」から参照されています。";
            return true;
        }
    }
    return false;
}

std::string InferLegacyRuntimeScene(const std::string& sceneId) {
    if (sceneId == "titleScene") return "TITLE";
    if (sceneId == "stageSelect") return "SELECT";
    if (sceneId == "gameOverScene") return "GAMEOVER";
    if (sceneId == "gameClearScene") return "GAMECLEAR";
    if (sceneId == "sample") return "PREVIEW";
    if (sceneId == "tutorial") return "TUTORIAL";

    const json root = ReadJsonObject("Resources/json/stage_select/stages.json");
    if (root.contains("stages") && root["stages"].is_array()) {
        const std::string expectedPath =
            (fs::path(kSceneObjectDirectory) / (sceneId + ".json")).generic_string();
        for (const json& stage : root["stages"]) {
            if (stage.is_object() && stage.value("levelPath", std::string()) == expectedPath) {
                return "GAMEPLAY";
            }
        }
    }
    return {};
}
}

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
    json playerSceneData, enemySceneData, objectSceneData, cameraSceneData;
    playerSceneData["objects"] = json::array();
    enemySceneData["objects"] = json::array();
    objectSceneData["objects"] = json::array();
    cameraSceneData["objects"] = json::array();

    BaseScene* currentScene = editor_->GetSceneManager()->GetCurrentScene();
    currentScene->EnsureUniquePersistentObjectGuids();
    auto& allObjects = currentScene->GetObjects();

    for (auto& obj : allObjects) {
        if (obj->GetName().empty()) continue;
        if (obj->IsEditorInternal() || obj->GetClassName() == "EditorOnly" || obj->GetName().rfind("__Editor_", 0) == 0) continue;

        json d = SerializeObject(obj.get());

        // SaveCategoryを見て振り分け
        std::string cat = obj->GetSaveCategory();
        if (obj->IsCameraObject() || cat == "Camera") cameraSceneData["objects"].push_back(d);
        else if (cat == "Player") playerSceneData["objects"].push_back(d);
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
    if (mode == SaveMode::All || mode == SaveMode::Camera) {
        targets.push_back({ "Camera", basePath + "_camera.json", cameraSceneData, false });
    }

    // メタデータファイルの作成。既存の表示名などは保存後も維持します。
    if (mode == SaveMode::All) {
        const fs::path metadataPath = fs::path(kSceneObjectDirectory) / currentFilename;
        json metadata = ReadJsonObject(metadataPath);
        metadata.erase("objects");
        std::string displayName = baseName;
        if (metadata.contains("_sceneAsset") && metadata["_sceneAsset"].is_object()) {
            displayName = metadata["_sceneAsset"].value("displayName", baseName);
        }
        const std::string runtimeScene = metadata.contains("_sceneAsset") && metadata["_sceneAsset"].is_object()
            ? metadata["_sceneAsset"].value("runtimeScene", std::string("SCENE_EDITOR"))
            : "SCENE_EDITOR";
        ApplySceneAssetMetadata(metadata, baseName, displayName, runtimeScene);
        targets.push_back({ "Meta", metadataPath.generic_string(), metadata, true });
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
    if (object->IsCameraObject() || cat == "Camera") targetFilename = "Resources/json/3Dobject/" + baseName + "_camera.json";
    else if (cat == "Player") targetFilename = "Resources/json/3Dobject/" + baseName + "_player.json";
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
        const bool sameGuid = objData.contains("guid") && objData["guid"].is_string() &&
            objData["guid"] == object->GetPersistentGuid();
        const bool legacyNameMatch = !objData.contains("guid") &&
            objData.contains("name") && objData["name"] == object->GetName();
        if (sameGuid || legacyNameMatch) {
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
    d["guid"] = obj->EnsurePersistentGuid();
    d["name"] = obj->GetName();
    const json components = obj->SerializeFeatureComponents();
    if (!components.empty()) {
        d["components"] = components;
    }

    // 1. 基本設定
    std::string className = obj->GetClassName();
    if (className.empty()) className = "Model";
    const bool isManagedCharacter = className == "Player" || className == "Enemy";
    d["type"] = className;
    d["tag"] = obj->GetTag();
    d["layer"] = obj->GetLayer().empty() ? "Default" : obj->GetLayer();
    d["saveCategory"] = obj->GetSaveCategory();
    d["enemyType"] = obj->GetEnemyType();
    d["gimmickType"] = obj->GetGimmickType();
    d["itemType"] = obj->GetItemType();
    if (obj->IsPrefabInstance()) {
        const auto& prefab = obj->GetPrefabInstanceInfo();
        d["prefabInstance"]["assetId"] = prefab.assetId;
        d["prefabInstance"]["prefabName"] = prefab.prefabName;
        d["prefabInstance"]["instanceId"] = prefab.instanceId;
        d["prefabInstance"]["sourceObjectId"] = prefab.sourceObjectId;
        d["prefabInstance"]["isRoot"] = prefab.isRoot;
    }
    if (obj->IsCameraObject()) {
        d["type"] = "Camera";
        d["saveCategory"] = "Camera";
        d["camera"] = SerializeSceneCameraSettings(obj->GetSceneCameraSettings());
    }

    if (className != "InvisibleBox" && !obj->IsCameraObject() && !isManagedCharacter) {
        d["modelName"] = obj->GetModelName();
    }

    // 2. 親子関係
    d["parentName"] = obj->GetParent() ? obj->GetParent()->GetName() : "";
    d["parentGuid"] = obj->GetParent() ? obj->GetParent()->EnsurePersistentGuid() : "";

    // 3. Transform
    Transform* t = obj->GetTransform();
    d["position"] = { t->translate.x, t->translate.y, t->translate.z };
    d["rotation"] = { t->rotate.x, t->rotate.y, t->rotate.z };
    d["quaternion"] = { t->quaternion.x, t->quaternion.y, t->quaternion.z, t->quaternion.w };
    if (!isManagedCharacter) {
        d["scale"] = { t->scale.x, t->scale.y, t->scale.z };
    }

    if (obj->IsCameraObject()) {
        d["animation"]["animName"] = obj->animName_;
        d["animation"]["isAnimLoop"] = obj->isAnimLoop_;
        d["animation"]["animatorController"] = obj->GetAnimatorControllerPath();
        d["recorder"]["recordPathName"] = obj->GetRecordPathName();
        d["recorder"]["isRecordLoop"] = obj->IsRecordLoop();
        d["recorder"]["isRecordRelative"] = obj->IsRecordRelative();
        return d;
    }

    // 4. Collider
    Object3d::ColliderConfig c = obj->GetColliderConfig();
    d["collider"]["type"] = (int)c.type;
    d["collider"]["center"] = { c.center.x, c.center.y, c.center.z };
    d["collider"]["size"] = { c.size.x, c.size.y, c.size.z };
    d["collider"]["rotation"] = { c.rotation.x, c.rotation.y, c.rotation.z };

    // 5. 衝突属性
    d["collisionAttribute"] = obj->GetCollisionAttribute();
    d["collisionMask"] = obj->GetCollisionMask();
    if (!obj->GetTerrainCollisionPath().empty()) {
        d["terrainCollisionPath"] = obj->GetTerrainCollisionPath();
    }

    // 6. イベント関連
    d["eventID"] = static_cast<int>(obj->GetEventType());
    d["targetID"] = obj->GetTargetID();
    d["myEventID"] = obj->GetEventID();

    // 7. パラメータ。Player/Enemyの共通ステータスはGameplayStatusManagerだけが保存元です。
    if (obj->param_.has_value()) {
        auto& p = obj->param_.value();
        if (isManagedCharacter) {
            if (className == "Enemy" && !p.enemyType.empty()) {
                d["param"]["enemyType"] = p.enemyType;
            }
        } else {
            d["param"]["hp"] = p.hp;
            d["param"]["maxHp"] = p.maxHp;
            d["param"]["attackPower"] = p.attackPower;
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
            d["param"]["targetScene"] = p.targetScene;
            d["param"]["moveAmount"] = p.moveAmount;
            d["param"]["moveSpeed"] = p.moveSpeed;
            d["param"]["startActive"] = p.startActive;
            d["param"]["returnOnOff"] = p.returnOnOff;
        }
    }

    // 8. グラフィックス
    Vector4 color = obj->GetColor();
    d["color"] = { color.x, color.y, color.z, color.w };
    d["blendMode"] = static_cast<int>(obj->GetBlendMode());
    d["materialType"] = obj->GetMaterialType();
    d["meshDrawIndex"] = obj->GetMeshDrawIndex();
    d["metallic"] = obj->GetMetallic();
    d["roughness"] = obj->GetRoughness();
    d["enableNormalMap"] = obj->GetEnableNormalMap();
    d["normalMapPath"] = obj->GetNormalMapPath();
    d["ormMapPath"] = obj->GetOrmMapPath();
    d["texturePath"] = obj->GetTexturePath();
    Vector2 tiling = obj->GetTextureTiling();
    d["textureTiling"] = { tiling.x, tiling.y };
    d["autoTextureTiling"] = obj->GetAutoTextureTiling();
    d["enableLighting"] = obj->GetEnableLighting();
    d["enableEnvMap"] = obj->GetEnableEnvMap();
    d["envIntensity"] = obj->GetEnvIntensity();
    d["emissive"] = obj->GetEmissive();
    d["castShadow"] = obj->GetCastShadow();
    d["isStatic"] = obj->IsStatic();
    d["particleName"] = obj->GetParticleName();
    d["gpuParticleName"] = obj->GetGPUParticleName();
    d["meshEffect1"] = obj->GetMeshEffect1Name();
    d["meshEffect2"] = obj->GetMeshEffect2Name();
    if (obj->HasLodLevels()) {
        json lodJson;
        lodJson["enabled"] = obj->IsLodEnabled();
        lodJson["levels"] = json::array();
        for (const auto& lod : obj->GetLodLevels()) {
            json levelJson;
            levelJson["level"] = lod.level;
            levelJson["modelName"] = lod.modelName;
            levelJson["distance"] = lod.distance;
            lodJson["levels"].push_back(levelJson);
        }
        d["lod"] = lodJson;
    }
    // 9. アニメーション
    d["animation"]["animName"] = obj->animName_;
    d["animation"]["isAnimLoop"] = obj->isAnimLoop_;
    d["animation"]["animatorController"] = obj->GetAnimatorControllerPath();

    // 10. レコーダー (Ghost)
    d["recorder"]["recordPathName"] = obj->GetRecordPathName();
    d["recorder"]["isRecordLoop"] = obj->IsRecordLoop();
    d["recorder"]["isRecordRelative"] = obj->IsRecordRelative();

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
    if (((obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 22) || obj->GetMaterialType() == 26) && obj->GetMeshRenderer() && obj->GetMeshRenderer()->GetWaterParamData()) {
        auto* water = obj->GetMeshRenderer()->GetWaterParamData();
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

std::vector<SceneSerializer::SceneAssetInfo> SceneSerializer::DiscoverSceneAssets() const {
    std::vector<SceneAssetInfo> assets;
    const fs::path directory = kSceneObjectDirectory;
    if (!fs::exists(directory)) {
        return assets;
    }

    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        const std::string filename = entry.path().filename().string();
        if (HasSceneCategorySuffix(filename)) {
            continue;
        }

        SceneAssetInfo asset;
        asset.filename = filename;
        asset.id = GetSceneBaseName(filename);
        asset.displayName = asset.id;
        asset.objectLayoutPath = entry.path().generic_string();

        const json metadata = ReadJsonObject(entry.path());
        const bool hasSceneAssetMetadata =
            metadata.contains("_sceneAsset") && metadata["_sceneAsset"].is_object();
        if (hasSceneAssetMetadata) {
            const json& sceneAsset = metadata["_sceneAsset"];
            asset.displayName = sceneAsset.value("displayName", asset.id);
            asset.runtimeScene = sceneAsset.value(
                "runtimeScene",
                std::string("SCENE_EDITOR"));
            asset.controllerName = sceneAsset.value("controller", std::string("DEFAULT"));
            if (sceneAsset.contains("resources") && sceneAsset["resources"].is_object()) {
                const json& resources = sceneAsset["resources"];
                asset.bgmPath = resources.value("bgm", std::string());
                asset.lightPath = resources.value("light", std::string());
                asset.cameraPath = resources.value("camera", std::string());
                asset.skyboxPath = resources.value("skybox", std::string());
            }
        }
        else {
            asset.runtimeScene = InferLegacyRuntimeScene(asset.id);
            if (asset.runtimeScene.empty()) {
                continue;
            }
            asset.usesLegacyMetadata = true;
        }

        const std::vector<fs::path> objectPaths = BuildObjectAssetPaths(asset.id);
        for (size_t index = 1; index < objectPaths.size(); ++index) {
            if (fs::exists(objectPaths[index])) {
                asset.usesSplitFiles = true;
                break;
            }
        }

        asset.spriteLayoutPath = ResolveExistingSpritePath(asset.id);
        asset.hasSpriteLayout = !asset.spriteLayoutPath.empty();
        assets.push_back(std::move(asset));
    }

    std::sort(assets.begin(), assets.end(), [](const SceneAssetInfo& left, const SceneAssetInfo& right) {
        if (left.displayName != right.displayName) {
            return left.displayName < right.displayName;
        }
        return left.id < right.id;
    });
    return assets;
}

bool SceneSerializer::CreateSceneAsset(
    const std::string& sceneIdInput,
    const std::string& displayName,
    const std::string& runtimeScene,
    SceneAssetTemplate sceneTemplate,
    std::string& createdFilename,
    std::string& errorMessage) {
    errorMessage.clear();
    createdFilename.clear();
    const std::string sceneId = GetSceneBaseName(sceneIdInput);
    if (!IsValidSceneId(sceneId, errorMessage)) {
        return false;
    }
    if (runtimeScene.empty() || !editor_ || !editor_->GetSceneManager() ||
        !editor_->GetSceneManager()->IsSceneRegistered(runtimeScene)) {
        errorMessage = "登録されていない実行クラスです: " + runtimeScene;
        return false;
    }
    if (AnySceneDestinationExists(sceneId)) {
        errorMessage = "同じScene IDのファイルが既に存在します。";
        return false;
    }

    const std::string filename = sceneId + ".json";
    if (sceneTemplate == SceneAssetTemplate::CurrentScene) {
        if (!editor_ || !editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene()) {
            errorMessage = "複製元となる現在のシーンがありません。";
            return false;
        }

        std::vector<SaveTarget> targets = BuildSceneSaveTargets(filename, SaveMode::All);
        for (SaveTarget& target : targets) {
            if (target.isMetadata) {
                ApplySceneAssetMetadata(target.data, sceneId, displayName, runtimeScene);
            }
        }
        SaveTargets(targets);

        const std::string currentId = GetSceneBaseName(editor_->GetCurrentSceneFilenameBuffer());
        const std::string currentSpritePath = ResolveExistingSpritePath(currentId);
        const fs::path newSpritePath = fs::path(kSceneSpriteDirectory) / (sceneId + "_sprite.json");
        if (!currentSpritePath.empty()) {
            std::error_code copyError;
            fs::copy_file(currentSpritePath, newSpritePath, fs::copy_options::none, copyError);
            if (copyError) {
                errorMessage = "Spriteレイアウトの複製に失敗しました: " + copyError.message();
                DeleteSceneAsset(filename, errorMessage);
                return false;
            }
        }
        else if (!WriteJsonObject(newSpritePath, MakeEmptySpriteLayout(), errorMessage)) {
            DeleteSceneAsset(filename, errorMessage);
            return false;
        }
    }
    else {
        std::vector<fs::path> createdPaths;
        auto writeAndTrack = [&](const fs::path& path, const json& data) {
            if (!WriteJsonObject(path, data, errorMessage)) {
                return false;
            }
            createdPaths.push_back(path);
            return true;
        };

        json metadata = json::object();
        ApplySceneAssetMetadata(metadata, sceneId, displayName, runtimeScene);
        const std::vector<fs::path> objectPaths = BuildObjectAssetPaths(sceneId);
        if (!writeAndTrack(objectPaths[0], metadata)) {
            return false;
        }
        for (size_t index = 1; index < objectPaths.size(); ++index) {
            if (!writeAndTrack(objectPaths[index], MakeEmptyObjectLayout())) {
                for (const fs::path& createdPath : createdPaths) {
                    std::error_code removeError;
                    fs::remove(createdPath, removeError);
                }
                return false;
            }
        }
        if (!writeAndTrack(fs::path(kSceneSpriteDirectory) / (sceneId + "_sprite.json"), MakeEmptySpriteLayout())) {
            for (const fs::path& createdPath : createdPaths) {
                std::error_code removeError;
                fs::remove(createdPath, removeError);
            }
            return false;
        }
    }

    createdFilename = filename;
    return true;
}

bool SceneSerializer::DuplicateSceneAsset(
    const std::string& sourceFilename,
    const std::string& newSceneIdInput,
    const std::string& displayName,
    std::string& createdFilename,
    std::string& errorMessage) {
    errorMessage.clear();
    createdFilename.clear();
    const std::string sourceId = GetSceneBaseName(sourceFilename);
    const std::string newSceneId = GetSceneBaseName(newSceneIdInput);
    if (!IsValidSceneId(newSceneId, errorMessage)) {
        return false;
    }
    if (AnySceneDestinationExists(newSceneId)) {
        errorMessage = "同じScene IDのファイルが既に存在します。";
        return false;
    }

    const std::vector<fs::path> sourcePaths = BuildObjectAssetPaths(sourceId);
    const std::vector<fs::path> destinationPaths = BuildObjectAssetPaths(newSceneId);
    if (!fs::exists(sourcePaths[0])) {
        errorMessage = "複製元のScene Assetが見つかりません。";
        return false;
    }

    std::vector<fs::path> createdPaths;
    for (size_t index = 0; index < sourcePaths.size(); ++index) {
        if (!fs::exists(sourcePaths[index])) {
            continue;
        }
        std::error_code copyError;
        fs::copy_file(sourcePaths[index], destinationPaths[index], fs::copy_options::none, copyError);
        if (copyError) {
            errorMessage = "Scene Assetの複製に失敗しました: " + copyError.message();
            for (const fs::path& path : createdPaths) {
                std::error_code removeError;
                fs::remove(path, removeError);
            }
            return false;
        }
        createdPaths.push_back(destinationPaths[index]);
    }

    if (!RegenerateCopiedSceneObjectGuids(destinationPaths, errorMessage)) {
        for (const fs::path& path : createdPaths) {
            std::error_code removeError;
            fs::remove(path, removeError);
        }
        return false;
    }

    const std::string sourceSpritePath = ResolveExistingSpritePath(sourceId);
    const fs::path destinationSpritePath = fs::path(kSceneSpriteDirectory) / (newSceneId + "_sprite.json");
    if (!sourceSpritePath.empty()) {
        std::error_code copyError;
        fs::copy_file(sourceSpritePath, destinationSpritePath, fs::copy_options::none, copyError);
        if (copyError) {
            errorMessage = "Spriteレイアウトの複製に失敗しました: " + copyError.message();
            for (const fs::path& path : createdPaths) {
                std::error_code removeError;
                fs::remove(path, removeError);
            }
            return false;
        }
        createdPaths.push_back(destinationSpritePath);
    }
    else if (!WriteJsonObject(destinationSpritePath, MakeEmptySpriteLayout(), errorMessage)) {
        for (const fs::path& path : createdPaths) {
            std::error_code removeError;
            fs::remove(path, removeError);
        }
        return false;
    }

    json metadata = ReadJsonObject(destinationPaths[0]);
    const std::string runtimeScene = metadata.contains("_sceneAsset") && metadata["_sceneAsset"].is_object()
        ? metadata["_sceneAsset"].value("runtimeScene", std::string("SCENE_EDITOR"))
        : "SCENE_EDITOR";
    ApplySceneAssetMetadata(metadata, newSceneId, displayName, runtimeScene);
    if (!WriteJsonObject(destinationPaths[0], metadata, errorMessage)) {
        for (const fs::path& path : createdPaths) {
            std::error_code removeError;
            fs::remove(path, removeError);
        }
        return false;
    }

    createdFilename = newSceneId + ".json";
    return true;
}

bool SceneSerializer::RenameSceneAsset(
    const std::string& sourceFilename,
    const std::string& newSceneIdInput,
    const std::string& displayName,
    std::string& renamedFilename,
    std::string& errorMessage) {
    errorMessage.clear();
    renamedFilename.clear();
    const std::string sourceId = GetSceneBaseName(sourceFilename);
    const std::string newSceneId = GetSceneBaseName(newSceneIdInput);
    if (!IsValidSceneId(newSceneId, errorMessage)) {
        return false;
    }
    if (sourceId != newSceneId && AnySceneDestinationExists(newSceneId)) {
        errorMessage = "同じScene IDのファイルが既に存在します。";
        return false;
    }
    if (sourceId != newSceneId) {
        std::string referenceDescription;
        if (IsRuntimeReferencedScene(sourceId, referenceDescription)) {
            errorMessage = "このScene IDは変更できません。" + referenceDescription;
            return false;
        }
    }

    const std::vector<fs::path> sourcePaths = BuildObjectAssetPaths(sourceId);
    const std::vector<fs::path> destinationPaths = BuildObjectAssetPaths(newSceneId);
    if (!fs::exists(sourcePaths[0])) {
        errorMessage = "名前を変更するScene Assetが見つかりません。";
        return false;
    }

    struct MovedPath {
        fs::path source;
        fs::path destination;
    };
    std::vector<MovedPath> movedPaths;
    if (sourceId != newSceneId) {
        for (size_t index = 0; index < sourcePaths.size(); ++index) {
            if (!fs::exists(sourcePaths[index])) {
                continue;
            }
            std::error_code moveError;
            fs::rename(sourcePaths[index], destinationPaths[index], moveError);
            if (moveError) {
                errorMessage = "Scene Assetの名前変更に失敗しました: " + moveError.message();
                for (auto rollback = movedPaths.rbegin(); rollback != movedPaths.rend(); ++rollback) {
                    std::error_code rollbackError;
                    fs::rename(rollback->destination, rollback->source, rollbackError);
                }
                return false;
            }
            movedPaths.push_back({ sourcePaths[index], destinationPaths[index] });
        }

        const std::string sourceSpritePath = ResolveExistingSpritePath(sourceId);
        if (!sourceSpritePath.empty()) {
            const fs::path destinationSpritePath = fs::path(kSceneSpriteDirectory) / (newSceneId + "_sprite.json");
            std::error_code moveError;
            fs::rename(sourceSpritePath, destinationSpritePath, moveError);
            if (moveError) {
                errorMessage = "Spriteレイアウトの名前変更に失敗しました: " + moveError.message();
                for (auto rollback = movedPaths.rbegin(); rollback != movedPaths.rend(); ++rollback) {
                    std::error_code rollbackError;
                    fs::rename(rollback->destination, rollback->source, rollbackError);
                }
                return false;
            }
            movedPaths.push_back({ sourceSpritePath, destinationSpritePath });
        }
    }

    const fs::path metadataPath = destinationPaths[0];
    json metadata = ReadJsonObject(metadataPath);
    const std::string runtimeScene = metadata.contains("_sceneAsset") && metadata["_sceneAsset"].is_object()
        ? metadata["_sceneAsset"].value("runtimeScene", std::string("SCENE_EDITOR"))
        : "SCENE_EDITOR";
    ApplySceneAssetMetadata(metadata, newSceneId, displayName, runtimeScene);
    if (!WriteJsonObject(metadataPath, metadata, errorMessage)) {
        for (auto rollback = movedPaths.rbegin(); rollback != movedPaths.rend(); ++rollback) {
            std::error_code rollbackError;
            fs::rename(rollback->destination, rollback->source, rollbackError);
        }
        return false;
    }

    renamedFilename = newSceneId + ".json";
    return true;
}

bool SceneSerializer::DeleteSceneAsset(const std::string& filename, std::string& errorMessage) {
    errorMessage.clear();
    const std::string sceneId = GetSceneBaseName(filename);
    std::string referenceDescription;
    if (IsRuntimeReferencedScene(sceneId, referenceDescription)) {
        errorMessage = "このScene Assetは削除できません。" + referenceDescription;
        return false;
    }
    bool removedAny = false;
    for (const fs::path& path : BuildObjectAssetPaths(sceneId)) {
        if (!fs::exists(path)) {
            continue;
        }
        std::error_code removeError;
        if (!fs::remove(path, removeError) || removeError) {
            errorMessage = "Scene Assetを削除できませんでした: " + path.string();
            return false;
        }
        removedAny = true;
    }

    const fs::path spritePaths[] = {
        fs::path(kSceneSpriteDirectory) / (sceneId + "_sprite.json"),
        fs::path(kSceneSpriteDirectory) / (sceneId + ".json")
    };
    for (const fs::path& path : spritePaths) {
        if (!fs::exists(path)) {
            continue;
        }
        std::error_code removeError;
        if (!fs::remove(path, removeError) || removeError) {
            errorMessage = "Spriteレイアウトを削除できませんでした: " + path.string();
            return false;
        }
        removedAny = true;
    }

    if (!removedAny) {
        errorMessage = "削除するScene Assetが見つかりません。";
        return false;
    }
    return true;
}

bool SceneSerializer::SetSceneAssetRuntimeScene(
    const std::string& filename,
    const std::string& runtimeScene,
    std::string& errorMessage) {
    errorMessage.clear();
    if (runtimeScene.empty() || !editor_ || !editor_->GetSceneManager() ||
        !editor_->GetSceneManager()->IsSceneRegistered(runtimeScene)) {
        errorMessage = "登録されていない実行クラスです: " + runtimeScene;
        return false;
    }

    const fs::path metadataPath = fs::path(kSceneObjectDirectory) / (GetSceneBaseName(filename) + ".json");
    if (!fs::exists(metadataPath)) {
        errorMessage = "Scene Assetが見つかりません: " + filename;
        return false;
    }

    json metadata = ReadJsonObject(metadataPath);
    const std::string sceneId = GetSceneBaseName(filename);
    const std::string displayName = metadata.contains("_sceneAsset") && metadata["_sceneAsset"].is_object()
        ? metadata["_sceneAsset"].value("displayName", sceneId)
        : sceneId;
    ApplySceneAssetMetadata(metadata, sceneId, displayName, runtimeScene);
    return WriteJsonObject(metadataPath, metadata, errorMessage);
}

bool SceneSerializer::SetSceneAssetRuntimeSettings(
    const std::string& filename,
    const std::string& controllerName,
    const std::string& bgmPath,
    const std::string& lightPath,
    const std::string& cameraPath,
    const std::string& skyboxPath,
    std::string& errorMessage) {
    errorMessage.clear();
    if (controllerName.empty()) {
        errorMessage = "Controller名を入力してください。";
        return false;
    }

    const std::string sceneId = GetSceneBaseName(filename);
    const fs::path metadataPath = fs::path(kSceneObjectDirectory) / (sceneId + ".json");
    if (!fs::exists(metadataPath)) {
        errorMessage = "Scene Assetが見つかりません: " + filename;
        return false;
    }

    json metadata = ReadJsonObject(metadataPath);
    const bool hasSceneAsset = metadata.contains("_sceneAsset") && metadata["_sceneAsset"].is_object();
    const std::string displayName = hasSceneAsset
        ? metadata["_sceneAsset"].value("displayName", sceneId)
        : sceneId;
    std::string runtimeScene = hasSceneAsset
        ? metadata["_sceneAsset"].value("runtimeScene", std::string())
        : InferLegacyRuntimeScene(sceneId);
    if (runtimeScene.empty()) {
        runtimeScene = "SCENE_EDITOR";
    }

    ApplySceneAssetMetadata(metadata, sceneId, displayName, runtimeScene);
    metadata["_sceneAsset"]["controller"] = controllerName;
    json& resources = metadata["_sceneAsset"]["resources"];
    resources["bgm"] = bgmPath;
    resources["light"] = lightPath;
    resources["camera"] = cameraPath;
    resources["skybox"] = skyboxPath;
    return WriteJsonObject(metadataPath, metadata, errorMessage);
}

SceneSerializer::SceneAssetValidationResult SceneSerializer::ValidateSceneAsset(
    const std::string& filename) const {
    SceneAssetValidationResult result;
    const std::vector<SceneAssetInfo> assets = DiscoverSceneAssets();
    const auto asset = std::find_if(assets.begin(), assets.end(), [&](const SceneAssetInfo& candidate) {
        return candidate.filename == filename;
    });
    if (asset == assets.end()) {
        result.errors.push_back("Scene Assetが見つかりません: " + filename);
        return result;
    }

    if (!fs::exists(asset->objectLayoutPath)) {
        result.errors.push_back("Objectレイアウトが見つかりません: " + asset->objectLayoutPath);
    }
    if (!asset->spriteLayoutPath.empty() && !fs::exists(asset->spriteLayoutPath)) {
        result.errors.push_back("Spriteレイアウトが見つかりません: " + asset->spriteLayoutPath);
    }
    if (asset->usesLegacyMetadata) {
        result.warnings.push_back("旧形式Sceneです。実行設定を保存するとScene Asset形式へ更新されます。");
    }
    if (!asset->bgmPath.empty() && !fs::exists(asset->bgmPath)) {
        result.warnings.push_back("BGMが見つかりません: " + asset->bgmPath);
    }
    if (!asset->lightPath.empty() && !fs::exists(asset->lightPath)) {
        result.warnings.push_back("Light JSONが見つかりません: " + asset->lightPath);
    }
    if (!asset->cameraPath.empty() &&
        !fs::exists(fs::path("Resources/json/camera") / asset->cameraPath)) {
        result.warnings.push_back("Camera JSONが見つかりません: " + asset->cameraPath);
    }
    if (!asset->skyboxPath.empty() && !fs::exists(asset->skyboxPath)) {
        result.warnings.push_back("Skyboxが見つかりません: " + asset->skyboxPath);
    }

    bool hasPlayer = false;
    bool hasCamera = false;
    bool hasGoal = false;
    std::unordered_set<std::string> objectNames;
    std::unordered_set<std::string> duplicateNames;
    const std::vector<fs::path> objectPaths = BuildObjectAssetPaths(asset->id);
    for (const fs::path& path : objectPaths) {
        if (!fs::exists(path)) {
            continue;
        }
        const json data = ReadJsonObject(path);
        if (!data.contains("objects") || !data["objects"].is_array()) {
            continue;
        }
        for (const json& object : data["objects"]) {
            if (!object.is_object()) {
                continue;
            }
            const std::string name = object.value("name", std::string());
            const std::string category = object.value("saveCategory", std::string());
            hasPlayer = hasPlayer || category == "Player" || name == "player";
            hasCamera = hasCamera || category == "Camera" || object.contains("camera");
            hasGoal = hasGoal || name == "goal" || name == "Goal";
            if (!name.empty() && !objectNames.insert(name).second) {
                duplicateNames.insert(name);
            }
        }
    }

    if (asset->runtimeScene == "GAMEPLAY") {
        if (!hasPlayer) {
            result.errors.push_back("GAMEPLAYにはPlayerが1体必要です。");
        }
        if (!hasCamera) {
            result.warnings.push_back("Camera Objectがありません。固定Camera設定だけで成立するか確認してください。");
        }
        if (!hasGoal) {
            result.warnings.push_back("goal Objectがありません。クリア条件が別実装か確認してください。");
        }
        if (InferLegacyRuntimeScene(asset->id) != "GAMEPLAY") {
            result.warnings.push_back(
                "stages.jsonに同じStage IDがありません。解放進行へ使用する場合はStage定義へ登録してください。");
        }
    }
    for (const std::string& duplicateName : duplicateNames) {
        result.warnings.push_back("Object名が重複しています: " + duplicateName);
    }
    return result;
}

std::string SceneSerializer::ResolveSceneAssetObjectPath(const std::string& filename) const {
    const std::string sceneId = GetSceneBaseName(filename);
    return (fs::path(kSceneObjectDirectory) / (sceneId + ".json")).generic_string();
}

std::string SceneSerializer::ResolveSceneAssetSpritePath(const std::string& filename) const {
    return ResolveExistingSpritePath(GetSceneBaseName(filename));
}
