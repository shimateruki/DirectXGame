#include "EditorPropertyRegistry.h"
#include "SceneSerializer.h"
#include "engine/graphics/3d/material/MaterialRenderPolicy.h"
#include "engine/graphics/3d/material/WaterParamJsonCodec.h"
#include "game/system/scene/SceneEntityParameterCodec.h"
#include "DebugEditor.h"
#include "Object3d.h"
#include "CollisionConfig.h"
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
    // ディレクトリと拡張子を除き、分割されたScene JSONに共通のIDを取り出す。
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
    // 分割ファイルを独立したシーンとして一覧へ重複登録しないための判定。
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
    // ファイル名として安全な文字だけ許し、カテゴリ別JSONの接尾辞とも衝突させない。
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
    // シーンデータをオブジェクト型のJSONとして読み、不正な内容は空値へ戻す。
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
    // 保存先を用意してからJSONを書き、失敗理由をEditorへ返す。
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

// 複製したSceneの全Objectに新GUIDを振り、親GUID参照も同じ対応表で更新します。
bool RegenerateCopiedSceneObjectGuids(
    const std::vector<fs::path>& paths,
    std::string& errorMessage) {
    // GUID の付け替えを行う文書と保存先。複数ファイル間の参照を同じ remap で更新する。
    struct SceneDocument {
        fs::path path;
        json data;
    };

    std::vector<SceneDocument> documents;
    std::unordered_map<std::string, std::string> guidRemap;

    for (const fs::path& path : paths) {
        if (!fs::exists(path))
            continue;

        json data = ReadJsonObject(path);

        if (!data.contains("objects") || !data["objects"].is_array()) {
            continue;
        }

        for (json& object : data["objects"]) {
            if (!object.is_object())
                continue;

            const std::string oldGuid = object.value("guid", std::string());
            const std::string newGuid = engine::Object3d::GeneratePersistentGuid();
            object["guid"] = newGuid;

            if (engine::Object3d::IsPersistentGuidValid(oldGuid)) {
                guidRemap.emplace(oldGuid, newGuid);
            }
        }

        documents.push_back({ path, std::move(data) });
    }

    // 全ファイルの旧GUIDを集めた後で親参照を置き換え、ファイル間の親子関係も保ちます。
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

// 新規Scene用の空のObject配置JSONを作ります。
json MakeEmptyObjectLayout() {
    json data;
    data["objects"] = json::array();
    return data;
}

// 設計解像度を含む空のSprite配置JSONを作ります。
json MakeEmptySpriteLayout() {
    json data;
    data["designResolution"] = { 1920.0f, 1080.0f };
    data["scaleToWindow"] = true;
    data["sprites"] = json::array();
    return data;
}

// Scene Assetの識別子、実行クラス、参照リソースの既定値を保存JSONへ補います。
void ApplySceneAssetMetadata(
    json& data,
    const std::string& sceneId,
    const std::string& displayName,
    const std::string& runtimeScene) {
    data["_sceneAsset"]["version"] = 3;
    data["_sceneAsset"]["id"] = sceneId;
    data["_sceneAsset"]["displayName"] = displayName.empty() ? sceneId : displayName;
    data["_sceneAsset"]["runtimeScene"] = runtimeScene.empty() ? "SCENE_EDITOR" : runtimeScene;

    // 既存アセットで選ばれたControllerは保持し、未設定時だけ既定値を補います。
    if (!data["_sceneAsset"].contains("controller")) {
        data["_sceneAsset"]["controller"] = "DEFAULT";
    }

    if (!data["_sceneAsset"].contains("resources") || !data["_sceneAsset"]["resources"].is_object()) {
        data["_sceneAsset"]["resources"] = json::object();
    }

    // 個別リソースの参照は維持し、欠けたキーだけ空値で揃えます。
    json& resources = data["_sceneAsset"]["resources"];
    if (!resources.contains("bgm"))
        resources["bgm"] = "";
    if (!resources.contains("light"))
        resources["light"] = "";
    if (!resources.contains("camera"))
        resources["camera"] = "";
    if (!resources.contains("skybox"))
        resources["skybox"] = "";
    data["_sceneAsset"]["spriteLayout"] =
        std::string(kSceneSpriteDirectory) + "/" + sceneId + "_sprite.json";
    if (!data.contains("objects")) {
        data["_comment"] = "Actual data is in _player, _enemy, _object, and _camera.json";
    }
}

std::vector<fs::path> BuildObjectAssetPaths(const std::string& sceneId) {
    // 単一ファイルと種類別ファイルを同じ順序で列挙し、複製・削除で取りこぼさない。
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
    // ObjectとSpriteの両形式を調べ、作成・複製で既存ファイルを上書きしない。
    for (const fs::path& path : BuildObjectAssetPaths(sceneId)) {
        if (fs::exists(path)) {
            return true;
        }
    }

    return fs::exists(fs::path(kSceneSpriteDirectory) / (sceneId + "_sprite.json")) ||
        fs::exists(fs::path(kSceneSpriteDirectory) / (sceneId + ".json"));
}

std::string ResolveExistingSpritePath(const std::string& sceneId) {
    // 現行の接尾辞付きSprite配置を優先し、旧形式も参照可能にする。
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
    // 固定名で参照されるゲーム用シーンは、エディターからの削除候補にしない。
    static const char* kFixedRuntimeScenes[] = {
        "titleScene",
        "stageSelect",
        "gameOverScene",
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

    // ステージ定義に採用されたJSONも、固定名以外であっても保護する。
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
    // 旧シーン名を実行クラスへ対応させ、ステージ定義からもゲーム用シーンを判別する。
    if (sceneId == "titleScene")
        return "TITLE";
    if (sceneId == "stageSelect")
        return "SELECT";
    if (sceneId == "gameOverScene")
        return "GAMEOVER";
    if (sceneId == "sample")
        return "SCENE_EDITOR";
    if (sceneId == "tutorial")
        return "TUTORIAL";

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
    // 保存時にScene状態を取得するため、Editorの参照を借用します。
    editor_ = editor;
}

std::vector<SceneSerializer::SaveTarget> SceneSerializer::BuildSceneSaveTargets(const std::string& currentFilename, SaveMode mode) {
    std::vector<SaveTarget> targets;
    if (!editor_->GetSceneManager() || !editor_->GetSceneManager()->GetCurrentScene())
        return targets;

    // 同じベース名からカテゴリ別 JSON とメタデータのパスを組み立てる。
    std::string baseName = currentFilename;
    size_t extPos = baseName.find(".json");
    if (extPos != std::string::npos)
        baseName = baseName.substr(0, extPos);

    std::string basePath = "Resources/json/3Dobject/" + baseName;

    // カテゴリ別の箱
    json playerSceneData, enemySceneData, objectSceneData, cameraSceneData;
    playerSceneData["objects"] = json::array();
    enemySceneData["objects"] = json::array();
    objectSceneData["objects"] = json::array();
    cameraSceneData["objects"] = json::array();

    BaseScene* currentScene = editor_->GetSceneManager()->GetCurrentScene();

    // 保存対象間で GUID が衝突しないよう、シリアライズ前にシーン全体を検査する。
    currentScene->EnsureUniquePersistentObjectGuids();
    auto& allObjects = currentScene->GetObjects();

    // 編集用の仮Objectを除き、名前の付いた配置Objectだけを保存へ渡す。
    for (auto& obj : allObjects) {
        if (obj->GetName().empty())
            continue;
        if (obj->IsEditorInternal() || obj->GetClassName() == "EditorOnly" || obj->GetName().rfind("__Editor_", 0) == 0)
            continue;

        json d = SerializeObject(obj.get());

        // SaveCategoryを見て振り分け
        std::string cat = obj->GetSaveCategory();
        if (obj->IsCameraObject() || cat == "Camera")
            cameraSceneData["objects"].push_back(d);
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

std::vector<SceneSerializer::SaveTarget> SceneSerializer::BuildSingleObjectSaveTargets(engine::Object3d* object, const std::string& filename) {
    std::vector<SaveTarget> targets;
    if (!object)
        return targets;

    // 1. ファイル名の自動振り分け
    std::string baseName = filename;
    size_t extPos = baseName.find(".json");
    if (extPos != std::string::npos)
        baseName = baseName.substr(0, extPos);

    std::string cat = object->GetSaveCategory();
    std::string targetFilename;
    if (object->IsCameraObject() || cat == "Camera")
        targetFilename = "Resources/json/3Dobject/" + baseName + "_camera.json";
    else if (cat == "Player") targetFilename = "Resources/json/3Dobject/" + baseName + "_player.json";
    else if (cat == "Enemy") targetFilename = "Resources/json/3Dobject/" + baseName + "_enemy.json";
    else targetFilename = "Resources/json/3Dobject/" + baseName + "_object.json";

    // 2. 既存JSONの読み込み
    json sceneData;
    std::ifstream inFile(targetFilename);

    if (inFile.is_open()) {
        try {
            if (inFile.peek() != std::ifstream::traits_type::eof())
                inFile >> sceneData;
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

    // GUID を優先し、旧形式で GUID がない行だけ名前で照合する。
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

    // 表示用ラベルを返すだけで、個々の書き込み成否はこの関数では集計しない。
    for (const auto& target : targets) {
        SaveToFile(target.path, target.data);

        if (!target.label.empty()) {
            savedFilesMsg += target.label + ", ";
        }
    }

    if (!savedFilesMsg.empty()) {
        savedFilesMsg.pop_back();
        savedFilesMsg.pop_back();
    }

    return savedFilesMsg;
}

std::string SceneSerializer::SaveScene(const std::string& currentFilename, SaveMode mode) {
    // 保存モードから対象ファイルを構築し、実際の書込経路はSaveTargetsへ集約します。
    return SaveTargets(BuildSceneSaveTargets(currentFilename, mode));
}

nlohmann::json SceneSerializer::SerializeObject(engine::Object3d* obj) {
    json d;
    d["guid"] = obj->EnsurePersistentGuid();
    d["name"] = obj->GetName();
    const json components = obj->SerializeFeatureComponents();

    if (!components.empty()) {
        d["components"] = components;
    }

    // 1. 基本設定
    std::string className = obj->GetClassName();
    if (className.empty())
        className = "Model";
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
    const Transform* t = obj->GetTransform();
    d["position"] = { t->translate.x, t->translate.y, t->translate.z };
    d["rotation"] = { t->rotate.x, t->rotate.y, t->rotate.z };
    d["quaternion"] = { t->quaternion.x, t->quaternion.y, t->quaternion.z, t->quaternion.w };

    // Player/Enemyの大きさはタイプ共通設定から復元するため、配置側には保存しない。
    if (!isManagedCharacter) {
        d["scale"] = { t->scale.x, t->scale.y, t->scale.z };
    }

    // Cameraは移動と再生設定までを保存し、通常モデルの衝突・材質項目は書き出さない。
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
    engine::Object3d::ColliderConfig c = obj->GetColliderConfig();
    d["collider"]["type"] = (int)c.type;
    d["collider"]["center"] = { c.center.x, c.center.y, c.center.z };
    d["collider"]["size"] = { c.size.x, c.size.y, c.size.z };
    d["collider"]["rotation"] = { c.rotation.x, c.rotation.y, c.rotation.z };
    if (c.useModelSurface)
        d["collider"]["useModelSurface"] = true;
    if (c.useModelVolume)
        d["collider"]["useModelVolume"] = true;
    if (!c.footprint.empty()) {
        d["collider"]["footprint"] = nlohmann::json::array();

        for (const engine::Vector2& point : c.footprint) {
            d["collider"]["footprint"].push_back({ point.x, point.y });
        }
    }

    // 5. 衝突属性
    d["collisionAttribute"] = obj->GetCollisionAttribute();
    d["collisionMask"] = obj->GetCollisionMask();
    d["collisionAttributeIds"] = CollisionCategories::GetIds(obj->GetCollisionAttribute());
    d["collisionMaskIds"] = CollisionCategories::GetIds(obj->GetCollisionMask());
    d["collisionAttributeUnknownBits"] = obj->GetCollisionAttribute() & ~CollisionCategories::GetKnownMask();
    d["collisionMaskUnknownBits"] = obj->GetCollisionMask() & ~CollisionCategories::GetKnownMask();

    if (!obj->GetTerrainCollisionPath().empty()) {
        d["terrainCollisionPath"] = obj->GetTerrainCollisionPath();
    }

    // 6. イベント関連
    d["eventID"] = static_cast<int>(obj->GetEventType());
    d["targetID"] = obj->GetTargetID();
    d["myEventID"] = obj->GetEventID();

    // 保存と読込で同じ param 定義を使用します。
    const json parameter = SceneEntityParameterCodec::Serialize(*obj);
    if (!parameter.empty())
        d["param"] = parameter;

    // 8. グラフィックス
    engine::Vector4 color = obj->GetColor();
    d["color"] = { color.x, color.y, color.z, color.w };
    d["blendMode"] = static_cast<int>(obj->GetBlendMode());
    d["materialType"] = obj->GetMaterialType();
    if (!obj->GetShaderMaterialId().empty())
        d["shaderMaterialId"] = obj->GetShaderMaterialId();
    if (!obj->GetMaterialInstancePath().empty())
        d["materialInstance"] = obj->GetMaterialInstancePath();
    d["meshDrawIndex"] = obj->GetMeshDrawIndex();
    d["metallic"] = obj->GetMetallic();
    d["roughness"] = obj->GetRoughness();
    d["enableNormalMap"] = obj->GetEnableNormalMap();
    d["normalMapPath"] = obj->GetNormalMapPath();
    d["ormMapPath"] = obj->GetOrmMapPath();
    d["texturePath"] = obj->GetTexturePath();
    engine::Vector2 tiling = obj->GetTextureTiling();
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

    // 特殊材質または水の登録プロパティを持つObjectだけ、描画側の水パラメータも保存する。
    if ((MaterialRenderPolicy::IsSpecial(obj->GetMaterialType()) ||
         EditorPropertyRegistry::GetInstance()->HasSerializedProperties(*obj, "waterParam.")) && obj->GetMeshRenderer() && obj->GetMeshRenderer()->GetWaterParamData()) {
        auto* water = obj->GetMeshRenderer()->GetWaterParamData();
        d["waterParam"] = WaterParamJsonCodec::Write(*water);
    }

    return d;
}

// =========================================================
// 単体オブジェクトの更新保存 (UpdateObjectInSceneJSON)
// =========================================================
void SceneSerializer::UpdateObjectInSceneJSON(engine::Object3d* object, const std::string& filename) {
    // 単一Objectの差分だけを保存対象にし、Scene全体の再保存を避けます。
    if (!object)
        return;

    SaveTargets(BuildSingleObjectSaveTargets(object, filename));
    DebugConsole::GetInstance()->AddLog("Updated object JSON: " + object->GetName());
}

void SceneSerializer::SaveToFile(const std::string& path, const json& data) {
    // JSONを書けた場合だけDebug Consoleへ保存先を通知します。
    std::ofstream f(path);

    if (f.is_open()) {
        f << data.dump(4);
        DebugConsole::GetInstance()->AddLog("Saved JSON to " + path);
    }
}

std::vector<SceneSerializer::SceneAssetInfo> SceneSerializer::DiscoverSceneAssets() const {
    // Scene ObjectのJSONから利用可能なScene Asset情報を列挙します。
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
        // 明示メタデータを優先し、旧形式は実行クラスを推定できるSceneだけ一覧へ残す。
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

        // メタデータの有無とは別に、分割配置と現行・旧形式のSpriteファイルの実在を調べる。
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

    // ファイルを作る前に、実行クラスの登録とScene IDの重複を検査する。
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

        // 現在Sceneの配置を新IDの保存先へ組み立て、メタデータだけ新規作成時の指定へ差し替える。
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

        // Object 側の複製だけでは UI レイアウトを失うため、Sprite JSON も同じ Scene ID へ複製する。
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
        // 空テンプレートは複数ファイルで構成される。途中失敗時はこの呼び出しで作った分だけ戻す。
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
    // 新しいScene IDと出力先の重複を先に調べ、関連JSONを一式として複製します。
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

    // Scene本体だけでなくカテゴリ別JSONも複製し、途中失敗時は作成済みファイルだけを巻き戻す。
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
        // GUIDを複製したまま残すと元Sceneと同一Objectとして扱われるため、失敗時は複製を破棄する。
        for (const fs::path& path : createdPaths) {
            std::error_code removeError;
            fs::remove(path, removeError);
        }

        return false;
    }

    const std::string sourceSpritePath = ResolveExistingSpritePath(sourceId);
    const fs::path destinationSpritePath = fs::path(kSceneSpriteDirectory) / (newSceneId + "_sprite.json");

    if (!sourceSpritePath.empty()) {
        // 元SceneのSprite配置があれば引き継ぎ、なければ編集可能な空レイアウトを用意する。
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

    // 配置の複製後も実行クラスは引き継ぎ、識別子・表示名・Sprite参照を複製先へ揃える。
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

    // 表示名だけの変更は許し、IDを変える場合だけ出力先の衝突を検査する。
    if (sourceId != newSceneId && AnySceneDestinationExists(newSceneId)) {
        errorMessage = "同じScene IDのファイルが既に存在します。";
        return false;
    }

    // 固定パスやステージ定義の参照元はここでは書き換えないため、参照中のID変更を拒否する。
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

    // 移動済みの組を記録し、後続の名前変更が失敗したときに逆順で元へ戻す。
    struct MovedPath {
        fs::path source;
        fs::path destination;
    };

    std::vector<MovedPath> movedPaths;

    if (sourceId != newSceneId) {
        // 本体と存在するカテゴリ別JSONを同じIDへ移し、旧形式の単一ファイルにも対応する。
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

        // Spriteも同じ改名履歴へ加え、後続処理の失敗時にはObject側と一緒に名前を戻す。
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

    // ファイル名の変更後に内部IDとSprite参照を揃える。IDが同じ場合は表示名の更新もここで行う。
    const fs::path metadataPath = destinationPaths[0];
    json metadata = ReadJsonObject(metadataPath);
    const std::string runtimeScene = metadata.contains("_sceneAsset") && metadata["_sceneAsset"].is_object()
        ? metadata["_sceneAsset"].value("runtimeScene", std::string("SCENE_EDITOR"))
        : "SCENE_EDITOR";
    ApplySceneAssetMetadata(metadata, newSceneId, displayName, runtimeScene);

    // 保存失敗時に戻すのは移動したパス名。メタデータの書込内容を復元する処理ではない。
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
    // 実行側から参照されるSceneは削除を拒み、参照切れを防ぎます。
    errorMessage.clear();
    const std::string sceneId = GetSceneBaseName(filename);
    std::string referenceDescription;

    if (IsRuntimeReferencedScene(sceneId, referenceDescription)) {
        errorMessage = "このScene Assetは削除できません。" + referenceDescription;
        return false;
    }

    bool removedAny = false;

    // 存在する構成ファイルを順に削除する。途中失敗時に削除済みファイルを復元する処理はない。
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
    // 登録済み実行クラスだけを許し、Asset metadataの実行先を変更します。
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
    // 実行設定をScene metadataへまとめ、個々のAsset参照を同時に保存します。
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
    // 旧形式では実行クラスを推定し、特定できない場合は汎用Sceneとしてメタデータを補う。
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
    // Object配置と実行クラスなどの参照を検査し、開く前に問題を列挙します。
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

    // 単一・分割の配置を横断して必要Objectと名前の重複を集め、ファイル単位の見落としを防ぐ。
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

    // GAMEPLAY向けの必須配置と進行定義を確認し、汎用編集Sceneへゲーム固有の条件を課さない。
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
    // Scene IDに揃えたObject JSONの規約パスを返します。
    const std::string sceneId = GetSceneBaseName(filename);
    return (fs::path(kSceneObjectDirectory) / (sceneId + ".json")).generic_string();
}

std::string SceneSerializer::ResolveSceneAssetSpritePath(const std::string& filename) const {
    // 旧形式の配置も探す共通解決経路へ渡します。
    return ResolveExistingSpritePath(GetSceneBaseName(filename));
}
