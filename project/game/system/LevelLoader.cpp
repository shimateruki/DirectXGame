#include "LevelLoader.h"
#include "game/system/SceneEntityParameterCodec.h"
#include "BaseScene.h"
#include "game/system/SceneGameplayAccess.h"
#include "engine/system/scene/LoadingProfiler.h"
#include "json.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <utility>
#include <algorithm>
#include <limits>
#include <vector>
#include <Windows.h> // OutputDebugStringA用

// 生成するクラス群のインクルード
#include "Object3d.h"
#include "CollisionConfig.h"
#include "Sprite.h"
#include "Player.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "GameplayStatusManager.h"
#include "game/actor/enemy/spawn/EnemySpawner.h"
#include "BaseEnemy.h"
#include "GimmickFactory.h"
#include "BaseGimmick.h"
#include "ItemFactory.h"
#include "MoveStrategy3D.h"
#include "GhostRecorder.h"
// マネージャ系
#include "ModelManager.h"
#include "CollisionManager.h"
#include "InputManager.h"
#include "SpriteLayoutScaler.h"

using json = nlohmann::json;

namespace {
constexpr int kSlimeSoftMaterialType = 25;
constexpr int kPrismCrystalMaterialType = 27;

void ConfigureEnemyRuntimeReferencesInternal(BaseScene* scene) {
    // 全配置物の生成後に敵の参照を結び、読み込み順による未解決を防ぐ。
    if (!scene) {
        return;
    }

    auto& objects = scene->GetObjects();
    for (auto& object : objects) {
        auto* enemy = dynamic_cast<BaseEnemy*>(object.get());
        if (!enemy) {
            continue;
        }

        enemy->BindSceneSpawnContext(scene);
    }
}

bool IsSlimeEnemyType(const std::string& enemyType) {
    // スライム用の軟質材質を適用する種類だけを列挙する。
    return enemyType == "Slime" || enemyType == "Bomber" || enemyType == "FireSlime" || enemyType == "ThunderSlime" ||
           enemyType == "WindSlime" || enemyType == "GiantSlime" || enemyType == "PrismSlime" ||
           enemyType == "MagmaSlime";
}

bool IsSlimeModelName(std::string modelName) {
    // JSON の区切り文字差を吸収して、既存モデル名の分類を安定させる。
    std::replace(modelName.begin(), modelName.end(), '\\', '/');
    return modelName.find("Characters/slime") != std::string::npos;
}

void ApplySlimeSoftMaterialIfStandard(Object3d* object) {
    // エディターで明示した材質は尊重し、標準材質の時だけ柔らかい見た目へ変える。
    if (object && object->GetMaterialType() == 0) {
        object->SetMaterialType(kSlimeSoftMaterialType);
    }
}

void ApplySlimeMaterialDefault(Object3d* object) {
    if (!object) {
        return;
    }

    if (object->GetClassName() == "Player") {
        ApplySlimeSoftMaterialIfStandard(object);
        return;
    }

    const std::string enemyType = object->GetEnemyType();
    if (!IsSlimeEnemyType(enemyType)) {
        return;
    }

    // マグマスライムはモデル内の溶岩・黒曜石PBR材質を維持します。
    if (enemyType == "MagmaSlime") {
        return;
    }

    if (enemyType == "PrismSlime" &&
        (object->GetMaterialType() == 0 || object->GetMaterialType() == kSlimeSoftMaterialType)) {
        object->SetMaterialType(kPrismCrystalMaterialType);
        return;
    }

    ApplySlimeSoftMaterialIfStandard(object);
}

void ApplyLodConfig(Object3d* object, const json& objData) {
    // 個々の配置物の LOD 設定を読み、未設定なら既存の描画設定を保つ。
    if (!object || !objData.contains("lod") || !objData["lod"].is_object()) {
        return;
    }

    const auto& lodJson = objData["lod"];
    std::vector<Object3d::LodLevel> levels;
    if (lodJson.contains("levels") && lodJson["levels"].is_array()) {
        for (const auto& levelJson : lodJson["levels"]) {
            if (!levelJson.is_object()) {
                continue;
            }

            Object3d::LodLevel level;
            level.level = levelJson.value("level", 0);
            level.modelName = levelJson.value("modelName", "");
            level.distance = levelJson.value("distance", 0.0f);
            if (level.level > 0 && !level.modelName.empty()) {
                levels.push_back(level);
            }
        }
    }

    if (!levels.empty()) {
        object->SetLodLevels(levels);
    }
    object->SetLodEnabled(lodJson.value("enabled", true));
}

bool TryReadVector3(const json& value, float& x, float& y, float& z) {
    // 形式が異なる旧データでは出力引数を変更せず、呼び出し側へ失敗を返す。
    if (!value.is_array() || value.size() < 3 || !value[0].is_number() || !value[1].is_number() ||
        !value[2].is_number()) {
        return false;
    }

    x = value[0].get<float>();
    y = value[1].get<float>();
    z = value[2].get<float>();
    return true;
}

json ConvertBlenderVector(const json& value, bool negate) {
    // Blender 側の軸順をゲーム側へ揃え、回転だけ符号を反転する。
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!TryReadVector3(value, x, y, z)) {
        return value;
    }

    if (negate) {
        return json::array({-x, -z, -y});
    }
    return json::array({x, z, y});
}

json NormalizeLevelObject(const json& source, const std::string& inheritedParentName) {
    // 旧形式の Transform と親子名を現行の配置 JSON へ移し、元の未知項目は残す。
    json normalized = source;
    const bool hasLegacyTransform = source.contains("transform") && source["transform"].is_object();

    if (hasLegacyTransform) {
        const json& transform = source["transform"];
        if (!normalized.contains("position") && transform.contains("translation")) {
            normalized["position"] = ConvertBlenderVector(transform["translation"], false);
        }
        if (!normalized.contains("rotation") && transform.contains("rotation")) {
            normalized["rotation"] = ConvertBlenderVector(transform["rotation"], true);
        }
        if (!normalized.contains("scale") && transform.contains("scaling")) {
            normalized["scale"] = ConvertBlenderVector(transform["scaling"], false);
        }

        if (normalized.contains("collider") && normalized["collider"].is_object()) {
            auto& collider = normalized["collider"];
            if (collider.contains("center")) {
                collider["center"] = ConvertBlenderVector(collider["center"], false);
            }
            if (collider.contains("rotation")) {
                collider["rotation"] = ConvertBlenderVector(collider["rotation"], true);
            }
            if (collider.contains("size")) {
                collider["size"] = ConvertBlenderVector(collider["size"], false);
            }
        }
    }

    if (!normalized.contains("modelName") && source.contains("file_name") && source["file_name"].is_string()) {
        normalized["modelName"] = source["file_name"];
    }

    const bool isPlayerSpawn = source.value("spawn", false);
    const bool isEnemySpawn = source.value("enemy", false);
    const std::string sourceType = source.value("type", "Model");
    if (isPlayerSpawn) {
        normalized["type"] = "Player";
        normalized["saveCategory"] = "Player";
    } else if (isEnemySpawn) {
        normalized["type"] = "Enemy";
        normalized["saveCategory"] = "Enemy";
        std::string enemyType = source.value("enemyType", "");
        if (enemyType.empty()) {
            enemyType = source.value("enemy_type", "Slime");
        }
        normalized["enemyType"] = enemyType.empty() ? "Slime" : enemyType;
    } else if (sourceType == "MESH" || sourceType == "EMPTY" || sourceType == "CURVE" || sourceType == "SURFACE" ||
               sourceType == "META" || sourceType == "FONT") {
        normalized["type"] = "Model";
        if (!normalized.contains("saveCategory")) {
            normalized["saveCategory"] = "Object";
        }
    }

    if (!inheritedParentName.empty() && (!normalized.contains("parentName") || !normalized["parentName"].is_string() ||
                                         normalized["parentName"].get<std::string>().empty())) {
        normalized["parentName"] = inheritedParentName;
    }

    normalized.erase("transform");
    normalized.erase("children");
    normalized.erase("file_name");
    normalized.erase("spawn");
    normalized.erase("enemy");
    normalized.erase("enemy_type");
    return normalized;
}

void AppendNormalizedLevelObjects(const json& sourceObjects, json& destination,
                                  const std::string& inheritedParentName = "") {
    if (!sourceObjects.is_array()) {
        return;
    }

    for (const auto& source : sourceObjects) {
        if (!source.is_object()) {
            continue;
        }

        json normalized = NormalizeLevelObject(source, inheritedParentName);
        const std::string childParentName = normalized.value("name", inheritedParentName);
        destination.push_back(std::move(normalized));

        if (source.contains("children")) {
            AppendNormalizedLevelObjects(source["children"], destination, childParentName);
        }
    }
}

bool RequiresLevelObjectNormalization(const json& sourceObjects) {
    // 旧形式の印がある時だけ変換し、現行の配置値を二重変換しない。
    if (!sourceObjects.is_array()) {
        return false;
    }

    for (const auto& source : sourceObjects) {
        if (!source.is_object()) {
            continue;
        }

        if (source.contains("transform") || source.contains("file_name") || source.contains("spawn") ||
            source.contains("enemy")) {
            return true;
        }

        const std::string sourceType = source.value("type", "Model");
        if (sourceType == "MESH" || sourceType == "EMPTY" || sourceType == "CURVE" || sourceType == "SURFACE" ||
            sourceType == "META" || sourceType == "FONT") {
            return true;
        }

        if (source.contains("children") && source["children"].is_array() && !source["children"].empty()) {
            return true;
        }
    }
    return false;
}
} // namespace

// シーンJSONから配置物を生成し、参照を接続します。生成と紐付けを分け、読み込み順に依存する敵・ギミックを扱います。

void LevelLoader::ConfigureEnemyRuntimeReferences(BaseScene* scene) {
    // 読み込み側とシーン再構築側から同じ参照解決処理を使う。
    ConfigureEnemyRuntimeReferencesInternal(scene);
}

void LevelLoader::LoadObjectLayout(BaseScene* scene, const std::string& filename, bool resolveSceneAssetPath) {
    BeginObjectLayoutLoad(scene, filename, resolveSceneAssetPath);
    while (!ContinueObjectLayoutLoad(scene, (std::numeric_limits<size_t>::max)())) {
    }
}

void LevelLoader::BeginObjectLayoutLoad(BaseScene* scene, const std::string& filename, bool resolveSceneAssetPath) {
    const std::string resolvedFilename =
        scene && resolveSceneAssetPath ? scene->ResolvePrimaryObjectLayoutPath(filename) : filename;
    std::string justName = resolvedFilename;
    size_t slashPos = justName.find_last_of("/\\");
    if (slashPos != std::string::npos) {
        justName = justName.substr(slashPos + 1);
    }
    if (scene && resolveSceneAssetPath) {
        scene->SetLoadedFilename(justName);
    }
    std::string baseFilename = resolvedFilename;

    size_t extPos = baseFilename.find(".json");
    if (extPos != std::string::npos) {
        baseFilename = baseFilename.substr(0, extPos);
    }

    // 親子リンクは全ファイルの生成が終わるまで保留します。
    parentPendingList_.clear();
    pendingObjectFiles_.clear();
    pendingFileIndex_ = 0;
    pendingObjectIndex_ = 0;
    pendingObjects_ = json::array();
    pendingFileLoaded_ = false;
    objectLayoutLoading_ = true;
    preparationStep_ = 0;

    // 分割形式のファイル名を同じベース名から導出します。
    std::string playerFile = baseFilename + "_player.json";
    std::string enemyFile = baseFilename + "_enemy.json";
    std::string objectFile = baseFilename + "_object.json";
    std::string cameraFile = baseFilename + "_camera.json";

    // どれか一つでも存在すれば分割形式を優先します。
    bool useSplitFiles = false;
    std::ifstream pFile(playerFile);
    std::ifstream eFile(enemyFile);
    std::ifstream oFile(objectFile);
    std::ifstream cFile(cameraFile);

    if (pFile.is_open() || eFile.is_open() || oFile.is_open() || cFile.is_open()) {
        useSplitFiles = true;
    }

    // 実際の読み込みは LoadSingleJson に任せるため、存在確認用のストリームを閉じます。
    if (pFile.is_open())
        pFile.close();
    if (eFile.is_open())
        eFile.close();
    if (oFile.is_open())
        oFile.close();
    if (cFile.is_open())
        cFile.close();

    // 新しい分割形式を優先し、旧形式の単一JSONも読み込み可能にします。
    if (useSplitFiles) {
        pendingObjectFiles_ = {playerFile, enemyFile, objectFile, cameraFile};
    } else {
        pendingObjectFiles_ = {resolvedFilename};
    }
}

bool LevelLoader::ContinueObjectLayoutLoad(BaseScene* scene, size_t maxObjects) {
    if (!objectLayoutLoading_) {
        return true;
    }
    // 配置物生成前の管理データも別フレームで読み、JSON解析と重ならないようにする。
    if (preparationStep_ == 0) {
        const auto start = std::chrono::steady_clock::now();
        runtimePresets_.Load();
        LogSlowLoadingOperation("LevelLoader preset initialization",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        ++preparationStep_;
        return false;
    }
    if (preparationStep_ == 1) {
        const auto start = std::chrono::steady_clock::now();
        GameplayStatusManager::GetInstance()->Initialize();
        LogSlowLoadingOperation("LevelLoader status initialization",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        ++preparationStep_;
        return false;
    }
    // 少なくとも一件は進める。残り件数をファイル間で共有し、分割ファイルも一フレームに集中させない。
    size_t remainingObjects = (std::max)(size_t{1}, maxObjects);
    while (pendingFileIndex_ < pendingObjectFiles_.size() && remainingObjects > 0) {
        if (!LoadSingleJson(scene, pendingObjectFiles_[pendingFileIndex_], remainingObjects)) {
            return false;
        }
        ++pendingFileIndex_;
    }
    if (pendingFileIndex_ < pendingObjectFiles_.size()) {
        return false;
    }

    // GUIDを優先して親を探し、古いシーンだけ名前による参照へフォールバックします。
    auto& objects = scene->GetObjects();
    scene->EnsureUniquePersistentObjectGuids();
    for (auto const& [childObj, parentReference] : parentPendingList_) {
        Object3d* parentObj = scene->FindObjectByPersistentGuid(parentReference.guid);
        if (!parentObj && !parentReference.legacyName.empty()) {
            for (auto& obj : objects) {
                if (obj && obj->GetName() == parentReference.legacyName) {
                    parentObj = obj.get();
                    break;
                }
            }
        }
        if (parentObj) {
            childObj->SetParent(parentObj);
        }
    }

    ConfigureEnemyRuntimeReferences(scene);
    objectLayoutLoading_ = false;
    pendingObjectFiles_.clear();
    return true;
}

float LevelLoader::GetObjectLayoutLoadProgress() const {
    if (!objectLayoutLoading_ || pendingObjectFiles_.empty()) {
        return 1.0f;
    }
    const float fileProgress = pendingFileLoaded_ && !pendingObjects_.empty()
        ? static_cast<float>(pendingObjectIndex_) / static_cast<float>(pendingObjects_.size())
        : 0.0f;
    return (static_cast<float>(pendingFileIndex_) + fileProgress) /
           static_cast<float>(pendingObjectFiles_.size());
}

// JSON一件ごとに種類別のFactoryで生成し、保存値の復元後にランタイム参照をつなぎます。
bool LevelLoader::LoadSingleJson(BaseScene* scene, const std::string& filename, size_t& remainingObjects) {
    auto& objects = scene->GetObjects();
    Object3dCommon* object3dCommon = scene->GetObject3dCommon();

    try {
        if (!pendingFileLoaded_) {
            json sceneData;
            if (!scene->TakePreparedJson(filename, sceneData)) {
                std::ifstream file(filename);
                if (!file.is_open()) {
                    return true;
                }
                sceneData = json::parse(file);
            }
            pendingObjects_ = json::array();
            if (sceneData.contains("objects") && sceneData["objects"].is_array()) {
                if (RequiresLevelObjectNormalization(sceneData["objects"])) {
                    AppendNormalizedLevelObjects(sceneData["objects"], pendingObjects_);
                } else {
                    pendingObjects_ = std::move(sceneData["objects"]);
                }
            }
            pendingFileLoaded_ = true;
            pendingObjectIndex_ = 0;
        }

        if (pendingObjects_.is_array()) {
            while (pendingObjectIndex_ < pendingObjects_.size() && remainingObjects > 0) {
                const auto& objData = pendingObjects_[pendingObjectIndex_++];
                --remainingObjects;
                if (!objData.contains("name") || !objData["name"].is_string())
                    continue;
                std::string name = objData["name"].get<std::string>();

                // 1. 既存チェック
                Object3d* targetObject = nullptr;
                for (auto& obj : objects) {
                    if (obj && !obj->GetName().empty() && obj->GetName() == name) {
                        targetObject = obj.get();
                        break;
                    }
                }

                // 2. 新規生成
                if (!targetObject && object3dCommon) {
                    std::unique_ptr<Object3d> newObj = nullptr;

                    std::string enemyType = "";
                    if (objData.contains("enemyType") && objData["enemyType"].is_string()) {
                        enemyType = objData["enemyType"].get<std::string>();
                    }

                    std::string type = "Model";
                    if (objData.contains("type") && objData["type"].is_string()) {
                        type = objData["type"].get<std::string>();
                    }

                    std::string gimmickType = "";
                    if (objData.contains("gimmickType") && objData["gimmickType"].is_string()) {
                        gimmickType = objData["gimmickType"].get<std::string>();
                    }

                    std::string itemType = "";
                    if (objData.contains("itemType") && objData["itemType"].is_string()) {
                        itemType = objData["itemType"].get<std::string>();
                    }

                    // --- 生成分岐 ---

                    // パターンA: 敵 (Factory)
                    if (!enemyType.empty()) {
                        auto enemy = EnemyFactory::GetInstance()->CreateEnemy(enemyType, object3dCommon);
                        if (enemy) {
                            enemy->SetEnemyType(enemyType);

                            enemy->BindSceneSpawnContext(scene);
                            newObj = std::move(enemy);
                        }
                    }
                    // パターンB: プレイヤー
                    else if (type == "Player" || name == "Player") {
                        auto player = std::make_unique<Player>();
                        player->Initialize(object3dCommon, InputManager::GetInstance(), scene->GetParticleSystem(),
                                           scene->GetSpriteCommon());
                        player->SetMoveStrategy(std::make_unique<MoveStrategy3D>());

                        // シーンにプレイヤーを登録
                        SetScenePlayer(scene, player.get());

                        newObj = std::move(player);
                    }
                    // パターンC: スポーナー
                    else if (type == "Spawner") {
                        auto spawner = std::make_unique<EnemySpawner>();
                        std::string spawnEnemyType = "Goblin";
                        float interval = 3.0f;
                        int maxCount = 5;

                        if (objData.contains("param") && objData["param"].is_object()) {
                            auto& p = objData["param"];
                            if (p.contains("enemyType") && p["enemyType"].is_string())
                                spawnEnemyType = p["enemyType"];
                            if (p.contains("interval") && p["interval"].is_number())
                                interval = p["interval"];
                            if (p.contains("maxCount") && p["maxCount"].is_number())
                                maxCount = p["maxCount"];
                        }

                        spawner->Initialize(object3dCommon, spawnEnemyType, interval, maxCount);
                        spawner->SetClassName("Spawner");
                        spawner->SetModel(nullptr);
                        spawner->SetIsVisible(false);

                        // スポーン時のコールバック
                        spawner->SetOnSpawnCallback([scene, spawnEnemyType](const Vector3& spawnPos) {
                            auto newEnemy =
                                EnemyFactory::GetInstance()->CreateEnemy(spawnEnemyType, scene->GetObject3dCommon());
                            if (newEnemy) {
                                newEnemy->SetTranslate(spawnPos);
                                newEnemy->UpdateLocalMatrix();
                                newEnemy->UpdateWorldMatrix();
                                newEnemy->SetEnemyType(spawnEnemyType);
                                newEnemy->BindSceneSpawnContext(scene);
                                // 直接シーンに追加
                                scene->AddObjectImmediate(std::move(newEnemy));
                            }
                        });
                        newObj = std::move(spawner);
                    }
                    // パターンD: アイテム (Factory)
                    else if (!itemType.empty() || type == "Item") {
                        std::string iType = itemType.empty() ? "Heal" : itemType;
                        auto item = ItemFactory::GetInstance()->CreateItem(iType, object3dCommon);
                        if (item) {
                            item->SetItemType(iType);
                            newObj = std::move(item);
                        }
                    }
                    // パターンE: ギミック (Factory)
                    else if (!gimmickType.empty() || type == "Gimmick") {
                        // type == "Gimmick" だが gimmickType が空の場合はデフォルト名で作成
                        std::string gType = gimmickType.empty() ? "DefaultGimmick" : gimmickType;
                        auto gimmick = GimmickFactory::GetInstance()->CreateGimmick(gType, object3dCommon);
                        if (gimmick) {
                            gimmick->SetGimmickType(gType);
                            // プリセットによるパラメータ上書き
                            runtimePresets_.ApplyToObject(gType, gimmick.get());

                            newObj = std::move(gimmick);
                        }
                    }
                    // パターンF: 通常オブジェクト
                    else {
                        newObj = std::make_unique<Object3d>();
                        newObj->Initialize(object3dCommon);
                    }

                    if (newObj) {
                        newObj->SetName(name);
                        std::string currentClass = newObj->GetClassName();
                        if (currentClass != "Enemy" && currentClass != "Player" && currentClass != "Spawner" &&
                            currentClass != "Gimmick" && currentClass != "Item") {
                            newObj->SetClassName(type);
                        }

                        // 保存カテゴリの復元処理
                        // ==========================================
                        if (type == "Camera" || type == "CinematicCamera") {
                            newObj->SetClassName("Camera");
                            newObj->SetSaveCategory("Camera");
                        } else if (currentClass == "Player" || type == "Player") {
                            newObj->SetSaveCategory("Player");
                        } else if (objData.contains("saveCategory") && objData["saveCategory"].is_string()) {
                            newObj->SetSaveCategory(objData["saveCategory"].get<std::string>());
                        } else {
                            // 過去の互換性用: categoryが無ければクラス名(Type)から推測する
                            if (currentClass == "Enemy" || currentClass == "Spawner" || type == "Spawner") {
                                newObj->SetSaveCategory("Enemy");
                            } else {
                                newObj->SetSaveCategory("Object");
                            }
                        }
                        // ==========================================

                        targetObject = newObj.get();

                        // 生成したら即座にCollisionManagerとシーンリストへ登録
                        scene->AddObjectImmediate(std::move(newObj));
                    }
                }

                if (!targetObject)
                    continue;

                if (objData.contains("guid") && objData["guid"].is_string()) {
                    targetObject->SetPersistentGuid(objData["guid"].get<std::string>());
                }
                targetObject->EnsurePersistentGuid();
                targetObject->DeserializeFeatureComponents(objData);

                if (objData.contains("prefabInstance") && objData["prefabInstance"].is_object()) {
                    const auto& prefab = objData["prefabInstance"];
                    Object3d::PrefabInstanceInfo info;
                    info.assetId = prefab.value("assetId", "");
                    info.prefabName = prefab.value("prefabName", "");
                    info.instanceId = prefab.value("instanceId", "");
                    info.sourceObjectId = prefab.value("sourceObjectId", "");
                    info.isRoot = prefab.value("isRoot", false);
                    targetObject->SetPrefabInstanceInfo(info);
                }

                if (targetObject->IsCameraObject()) {
                    targetObject->SetClassName("Camera");
                    targetObject->SetSaveCategory("Camera");
                    targetObject->SetModel("Editor/camera_gizmo");
                    targetObject->SetColor({0.25f, 0.75f, 1.0f, 1.0f});
                    targetObject->SetCastShadow(false);
                    Object3d::ColliderConfig cameraCollider;
                    cameraCollider.type = ColliderType::kNone;
                    targetObject->SetColliderConfig(cameraCollider);
                    if (objData.contains("camera")) {
                        SceneCameraSettings settings = targetObject->GetSceneCameraSettings();
                        DeserializeSceneCameraSettings(objData["camera"], settings);
                        targetObject->SetSceneCameraSettings(settings);
                    }
                }

                // 3. モデル・表示設定
                std::string type = targetObject->GetClassName();
                std::string loadedModelName;
                if (type == "InvisibleBox" || type == "Spawner") {
                    targetObject->SetModel(nullptr);
                    targetObject->SetIsVisible(true);
                } else {
                    targetObject->SetIsVisible(true);
                    if (objData.contains("modelName") && objData["modelName"].is_string()) {
                        std::string modelName = objData["modelName"].get<std::string>();
                        loadedModelName = modelName;
                        // モデルロードはManagerに任せる
                        if (!modelName.empty()) {
                            ModelManager::GetInstance()->LoadModel(modelName);
                            targetObject->SetModel(modelName);
                        } else {
                            targetObject->SetModel(nullptr);
                        }
                    }
                }

                // 4. Transform
                Transform transformValue = *targetObject->GetTransform();
                Transform* transform = &transformValue;
                if (objData.contains("position")) {
                    transform->translate.x = objData["position"][0];
                    transform->translate.y = objData["position"][1];
                    transform->translate.z = objData["position"][2];
                }
                if (objData.contains("quaternion")) {
                    transform->quaternion.x = objData["quaternion"][0];
                    transform->quaternion.y = objData["quaternion"][1];
                    transform->quaternion.z = objData["quaternion"][2];
                    transform->quaternion.w = objData["quaternion"][3];
                    transform->isQuaternionMaster = true; // クォータニオン優先モード

                    // インスペクター表示用などにオイラー角も読んでおく
                    if (objData.contains("rotation")) {
                        transform->rotate.x = objData["rotation"][0];
                        transform->rotate.y = objData["rotation"][1];
                        transform->rotate.z = objData["rotation"][2];
                    }
                }
                // 古いセーブデータ（オイラー角しかない場合）の互換性対応
                else if (objData.contains("rotation")) {
                    transform->rotate.x = objData["rotation"][0];
                    transform->rotate.y = objData["rotation"][1];
                    transform->rotate.z = objData["rotation"][2];
                    transform->isQuaternionMaster = false; // UpdateMatrix時にクォータニオンを作らせる
                }
                if (objData.contains("scale")) {
                    transform->scale.x = objData["scale"][0];
                    transform->scale.y = objData["scale"][1];
                    transform->scale.z = objData["scale"][2];
                }

                targetObject->SetLocalTransform(transformValue);
                // ロード直後に行列更新
                targetObject->UpdateLocalMatrix();
                targetObject->UpdateWorldMatrix();
                if (objData.contains("blendMode"))
                    targetObject->SetBlendMode(static_cast<BlendMode>(objData["blendMode"].get<int>()));
                if (objData.contains("materialType"))
                    targetObject->SetMaterialType(objData["materialType"].get<int>());
                if (objData.contains("meshDrawIndex"))
                    targetObject->SetMeshDrawIndex(objData["meshDrawIndex"].get<int>());

                if (objData.contains("color")) {
                    targetObject->SetColor(
                        {objData["color"][0], objData["color"][1], objData["color"][2], objData["color"][3]});
                }
                // マテリアル / グラフィック関連
                if (objData.contains("metallic"))
                    targetObject->SetMetallic(objData["metallic"].get<float>());
                if (objData.contains("roughness"))
                    targetObject->SetRoughness(objData["roughness"].get<float>());
                if (objData.contains("enableNormalMap"))
                    targetObject->SetEnableNormalMap(objData["enableNormalMap"].get<bool>());
                if (objData.contains("normalMapPath"))
                    targetObject->SetNormalMap(objData["normalMapPath"].get<std::string>());
                if (objData.contains("ormMapPath"))
                    targetObject->SetOrmMap(objData["ormMapPath"].get<std::string>());
                if (objData.contains("texturePath"))
                    targetObject->SetTexture(objData["texturePath"].get<std::string>());
                if (targetObject->GetMaterialType() == 0 &&
                    (IsSlimeEnemyType(targetObject->GetEnemyType()) || IsSlimeModelName(loadedModelName))) {
                    targetObject->SetMaterialType(kSlimeSoftMaterialType);
                }
                if (objData.contains("textureTiling") && objData["textureTiling"].is_array() &&
                    objData["textureTiling"].size() >= 2) {
                    targetObject->SetTextureTiling(
                        {objData["textureTiling"][0].get<float>(), objData["textureTiling"][1].get<float>()});
                }
                if (objData.contains("autoTextureTiling"))
                    targetObject->SetAutoTextureTiling(objData["autoTextureTiling"].get<bool>());
                if (objData.contains("enableLighting"))
                    targetObject->SetEnableLighting(objData["enableLighting"].get<bool>());
                if (objData.contains("enableEnvMap"))
                    targetObject->SetEnableEnvMap(objData["enableEnvMap"].get<bool>());
                if (objData.contains("envIntensity"))
                    targetObject->SetEnvIntensity(objData["envIntensity"].get<float>());
                if (objData.contains("emissive"))
                    targetObject->SetEmissive(objData["emissive"].get<float>());
                if (objData.contains("castShadow"))
                    targetObject->SetCastShadow(objData["castShadow"].get<bool>());
                if (objData.contains("localFog")) {
                    if (auto* fogData = targetObject->GetLocalFogData()) {
                        auto& f = objData["localFog"];
                        if (f.contains("color")) {
                            fogData->fogColor = {f["color"][0], f["color"][1], f["color"][2], f["color"][3]};
                        }
                        if (f.contains("density"))
                            fogData->fogDensity = f["density"];
                        if (f.contains("edgeFade"))
                            fogData->edgeFade = f["edgeFade"];
                        if (f.contains("noiseSpeed"))
                            fogData->noiseSpeed = f["noiseSpeed"];
                        if (f.contains("noiseScale"))
                            fogData->noiseScale = f["noiseScale"];
                        if (f.contains("scatteringG"))
                            fogData->scatteringG = f["scatteringG"];
                        if (f.contains("scatteringIntensity"))
                            fogData->scatteringIntensity = f["scatteringIntensity"];
                    }
                }
                if (objData.contains("waterParam") &&
                    ((targetObject->GetMaterialType() >= 8 && targetObject->GetMaterialType() <= 22) ||
                     targetObject->GetMaterialType() == 26)) {
                    if (targetObject->GetMeshRenderer() && targetObject->GetMeshRenderer()->GetWaterParamData()) {
                        auto* water = targetObject->GetMeshRenderer()->GetWaterParamData();
                        const auto& jw = objData["waterParam"];

                        if (jw.contains("waveSpeed"))
                            water->waveSpeed = jw["waveSpeed"];
                        if (jw.contains("waveHeight"))
                            water->waveHeight = jw["waveHeight"];
                        if (jw.contains("waveFrequency"))
                            water->waveFrequency = jw["waveFrequency"];
                        if (jw.contains("flowSpeedX"))
                            water->flowSpeedX = jw["flowSpeedX"];
                        if (jw.contains("flowSpeedY"))
                            water->flowSpeedY = jw["flowSpeedY"];
                        if (jw.contains("effectType"))
                            water->effectType = jw["effectType"];
                        if (jw.contains("effectScale"))
                            water->effectScale = jw["effectScale"];
                        if (jw.contains("effectScaleX"))
                            water->effectScaleX = jw["effectScaleX"];
                        if (jw.contains("effectScaleY"))
                            water->effectScaleY = jw["effectScaleY"];
                        if (jw.contains("effectScaleZ"))
                            water->effectScaleZ = jw["effectScaleZ"];
                        if (jw.contains("topDownDetail"))
                            water->topDownDetail = jw["topDownDetail"];
                        if (jw.contains("effectSoftness"))
                            water->effectSoftness = jw["effectSoftness"];
                        if (jw.contains("effectIntensity"))
                            water->effectIntensity = jw["effectIntensity"];
                        if (jw.contains("billboardScale"))
                            water->billboardScale = jw["billboardScale"];
                    }
                }
                // 5. Collider
                if (!targetObject->IsCameraObject() && objData.contains("collider")) {
                    json colData = objData["collider"];
                    Object3d::ColliderConfig config = targetObject->GetColliderConfig();
                    if (colData.contains("type"))
                        config.type = (ColliderType)colData["type"].get<int>();
                    if (colData.contains("center")) {
                        config.center.x = colData["center"][0];
                        config.center.y = colData["center"][1];
                        config.center.z = colData["center"][2];
                    }
                    if (colData.contains("size")) {
                        config.size.x = colData["size"][0];
                        config.size.y = colData["size"][1];
                        config.size.z = colData["size"][2];
                    }
                    if (colData.contains("rotation")) {
                        config.rotation.x = colData["rotation"][0];
                        config.rotation.y = colData["rotation"][1];
                        config.rotation.z = colData["rotation"][2];
                    }
                    config.footprint.clear();
                    if (colData.contains("footprint") && colData["footprint"].is_array()) {
                        for (const auto& point : colData["footprint"]) {
                            if (point.is_array() && point.size() == 2 &&
                                point[0].is_number() && point[1].is_number()) {
                                config.footprint.push_back({ point[0].get<float>(), point[1].get<float>() });
                            }
                        }
                    }
                    targetObject->SetColliderConfig(config);
                }

                if (!targetObject->IsCameraObject()) {
                    auto loadCategoryMask = [&objData](const char* idsKey, const char* legacyKey, uint32_t fallback) {
                        if (objData.contains(idsKey) && objData[idsKey].is_array()) {
                            std::vector<std::string> ids;
                            for (const auto& id : objData[idsKey]) {
                                if (id.is_string()) ids.push_back(id.get<std::string>());
                            }
                            const std::string unknownKey = std::string(idsKey) == "collisionAttributeIds"
                                ? "collisionAttributeUnknownBits" : "collisionMaskUnknownBits";
                            const uint32_t unknown = objData.value(unknownKey, uint32_t{0});
                            return CollisionCategories::FromIds(ids) | unknown;
                        }
                        if (objData.contains(legacyKey) && objData[legacyKey].is_number_unsigned()) {
                            return CollisionCategories::FromLegacyMask(objData[legacyKey].get<uint32_t>());
                        }
                        if (objData.contains(legacyKey) && objData[legacyKey].is_number_integer()) {
                            return CollisionCategories::FromLegacyMask(static_cast<uint32_t>(objData[legacyKey].get<int>()));
                        }
                        return fallback;
                    };
                    targetObject->SetCollisionAttribute(loadCategoryMask("collisionAttributeIds", "collisionAttribute", targetObject->GetCollisionAttribute()));
                    targetObject->SetCollisionMask(loadCategoryMask("collisionMaskIds", "collisionMask", targetObject->GetCollisionMask()));
                }
                if (!targetObject->IsCameraObject() && objData.contains("isStatic"))
                    targetObject->SetStatic(objData["isStatic"].get<bool>());
                if (!targetObject->IsCameraObject() && objData.contains("terrainCollisionPath") &&
                    objData["terrainCollisionPath"].is_string()) {
                    targetObject->LoadTerrainCollisionFromFile(objData["terrainCollisionPath"].get<std::string>());
                }

                // 6. Events & Params
                if (objData.contains("eventID"))
                    targetObject->SetEventType(static_cast<EventType>(objData["eventID"]));
                if (objData.contains("itemType") && objData["itemType"].is_string()) {
                    targetObject->SetItemType(objData["itemType"].get<std::string>());
                }

                if (objData.contains("param")) {
                    SceneEntityParameterCodec::Apply(*targetObject, objData["param"]);
                }

                ApplySlimeMaterialDefault(targetObject);
                GameplayStatusManager::GetInstance()->ApplyManagedStatus(targetObject, true);
                ApplyLodConfig(targetObject, objData);
                targetObject->UpdateLocalMatrix();
                targetObject->UpdateWorldMatrix();

                // ==========================================
                // 7. Animation & Recorder
                // ==========================================

                // 古いフラットな形式の読み込み（後方互換性用）
                if (objData.contains("animName"))
                    targetObject->animName_ = objData["animName"];
                if (objData.contains("isAnimLoop"))
                    targetObject->isAnimLoop_ = objData["isAnimLoop"];

                // ネストされた形式の読み込み（最新仕様）
                if (objData.contains("animation")) {
                    const auto& anim = objData["animation"];
                    if (anim.contains("animName"))
                        targetObject->animName_ = anim["animName"];
                    if (anim.contains("isAnimLoop"))
                        targetObject->isAnimLoop_ = anim["isAnimLoop"];
                    if (anim.contains("animatorController")) {
                        targetObject->SetAnimatorController(anim.value("animatorController", ""));
                    }
                }
                targetObject->InitializeRecorder(nullptr);
                bool isCinematic = targetObject->IsCameraObject();

                // パスデータが存在する場合に再生を開始
                if (!targetObject->GetRecordPathName().empty() && targetObject->recorder_) {
                    targetObject->recorder_->Play(targetObject->GetRecordPathName(), targetObject->IsRecordLoop(),
                                                  targetObject->IsRecordRelative(), isCinematic);
                }

                // 他のカテゴリ JSON に親がある場合も解決できるよう、全件生成まで親子付けを保留する。
                PendingParentReference parentReference;
                if (objData.contains("parentGuid") && objData["parentGuid"].is_string()) {
                    parentReference.guid = objData["parentGuid"].get<std::string>();
                }
                if (objData.contains("parentName") && objData["parentName"].is_string()) {
                    parentReference.legacyName = objData["parentName"].get<std::string>();
                }
                if (!parentReference.guid.empty() || !parentReference.legacyName.empty()) {
                    parentPendingList_[targetObject] = std::move(parentReference);
                }
            }
        }
        if (pendingObjectIndex_ < pendingObjects_.size()) {
            return false;
        }
        pendingObjects_ = json::array();
        pendingObjectIndex_ = 0;
        pendingFileLoaded_ = false;
    } catch (const json::parse_error& e) {
        std::string message = "Failed to parse " + filename + " : " + e.what() + "\n";
        OutputDebugStringA(message.c_str());
        pendingObjects_ = json::array();
        pendingObjectIndex_ = 0;
        pendingFileLoaded_ = false;
    }
    return true;
}

void LevelLoader::LoadSpriteLayout(BaseScene* scene, const std::string& filename) {
    const std::string resolvedFilename = scene ? scene->ResolvePrimarySpriteLayoutPath(filename) : filename;
    std::string justName = resolvedFilename;
    size_t slashPos = justName.find_last_of("/\\");
    if (slashPos != std::string::npos) {
        justName = justName.substr(slashPos + 1);
    }
    scene->SetLoadedSpriteFilename(justName);
    auto& sprites = scene->GetSprites();
    SpriteCommon* spriteCommon = scene->GetSpriteCommon();

    json layoutData;
    try {
        if (!scene->TakePreparedJson(resolvedFilename, layoutData)) {
            std::ifstream file(resolvedFilename);
            if (!file.is_open()) {
                return;
            }
            layoutData = json::parse(file);
        }
        const auto layoutScale = SpriteLayoutScaler::Make(layoutData);
        if (layoutData.contains("sprites") && layoutData["sprites"].is_array()) {
            std::vector<std::pair<Sprite*, std::string>> spriteParentPending;
            for (const auto& spriteData : layoutData["sprites"]) {
                if (!spriteData.contains("name"))
                    continue;
                std::string name = spriteData["name"];

                Sprite* targetSprite = nullptr;
                for (auto& sprite : sprites) {
                    if (sprite && sprite->GetName() == name) {
                        targetSprite = sprite.get();
                        break;
                    }
                }

                if (!targetSprite && spriteCommon) {
                    std::string textureFile = "";
                    if (spriteData.contains("texture"))
                        textureFile = spriteData["texture"];
                    uint32_t handle = textureFile.empty() ? 0 : Sprite::LoadTexture(textureFile);

                    auto newSprite = std::make_unique<Sprite>();
                    newSprite->Initialize(spriteCommon, handle);
                    newSprite->SetName(name);
                    newSprite->SetTextureName(textureFile);
                    targetSprite = newSprite.get();
                    scene->AddSprite(std::move(newSprite));
                }

                if (targetSprite) {
                    if (spriteData.contains("position")) {
                        targetSprite->SetPosition(SpriteLayoutScaler::ScalePosition(
                            SpriteLayoutScaler::ReadVector2(spriteData["position"], targetSprite->GetPosition()),
                            layoutScale));
                    }
                    if (spriteData.contains("size")) {
                        targetSprite->SetSize(SpriteLayoutScaler::ScaleSize(
                            SpriteLayoutScaler::ReadVector2(spriteData["size"], targetSprite->GetSize()), layoutScale));
                    }
                    if (spriteData.contains("anchor"))
                        targetSprite->SetAnchorPoint({spriteData["anchor"][0], spriteData["anchor"][1]});
                    if (spriteData.contains("color")) {
                        Vector4 currentColor = targetSprite->GetColor();
                        float alpha = currentColor.w;
                        if (spriteData["color"].is_array() && spriteData["color"].size() >= 4) {
                            alpha = spriteData["color"][3];
                        }
                        targetSprite->SetColor({spriteData["color"][0], // R
                                                spriteData["color"][1], // G
                                                spriteData["color"][2], // B
                                                alpha});
                    }
                    if (spriteData.contains("emissive")) {
                        targetSprite->SetEmissive(spriteData["emissive"].get<float>());
                    }
                    if (spriteData.contains("parentName") && spriteData["parentName"].is_string()) {
                        spriteParentPending.emplace_back(targetSprite, spriteData["parentName"].get<std::string>());
                    } else {
                        targetSprite->SetParent(nullptr);
                    }
                    targetSprite->Update();
                }
            }
            for (const auto& [sprite, parentName] : spriteParentPending) {
                if (!sprite)
                    continue;

                Sprite* parentSprite = nullptr;
                if (!parentName.empty()) {
                    for (auto& candidate : sprites) {
                        if (candidate && candidate.get() != sprite && candidate->GetName() == parentName) {
                            parentSprite = candidate.get();
                            break;
                        }
                    }
                }
                sprite->SetParent(parentSprite);
                sprite->Update();
            }
        }
    } catch (...) {
    }
}
