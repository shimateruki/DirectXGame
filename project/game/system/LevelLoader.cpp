#include "LevelLoader.h"
#include "BaseScene.h" 
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <utility>
#include <algorithm>
#include <vector>
#include <Windows.h> // OutputDebugStringA用

// 生成するクラス群のインクルード
#include "Object3d.h"
#include "Sprite.h"
#include "Player.h"
#include "EnemyFactory.h"
#include "GameplayStatusManager.h"
#include "EnemySpawner.h"
#include "BaseEnemy.h"
#include "EnemyBomber.h"
#include "GimmickFactory.h"
#include "BaseGimmick.h"
#include "ItemFactory.h"
#include "MoveStrategy3D.h"
#include "GhostRecorder.h"
// マネージャ系
#include "ModelManager.h"
#include "CollisionManager.h"
#include "InputManager.h"
#include "PresetManager.h"
#include "SpriteLayoutScaler.h"

using json = nlohmann::json;

namespace {
constexpr int kSlimeSoftMaterialType = 25;

void ConfigureBomberSpawnCallback(BaseScene* scene, EnemyBomber* bomber) {
    if (!scene || !bomber) {
        return;
    }

    bomber->SetSpawnCallback([scene](std::unique_ptr<BaseEnemy> spawned) {
        if (!spawned) {
            return;
        }

        spawned->SetTarget(scene->GetPlayer());
        scene->AddObject(std::move(spawned));
    });
}

void ConfigureEnemyRuntimeReferences(BaseScene* scene) {
    if (!scene) {
        return;
    }

    Player* player = scene->GetPlayer();
    auto& objects = scene->GetObjects();
    for (auto& object : objects) {
        auto* enemy = dynamic_cast<BaseEnemy*>(object.get());
        if (!enemy) {
            continue;
        }

        enemy->SetTarget(player);
        if (auto* bomber = dynamic_cast<EnemyBomber*>(enemy)) {
            ConfigureBomberSpawnCallback(scene, bomber);
        }
    }
}

bool IsSlimeEnemyType(const std::string& enemyType) {
    return enemyType == "Slime" ||
        enemyType == "Bomber" ||
        enemyType == "FireSlime" ||
        enemyType == "ThunderSlime" ||
        enemyType == "GiantSlime";
}

bool IsSlimeModelName(std::string modelName) {
    std::replace(modelName.begin(), modelName.end(), '\\', '/');
    return modelName.find("Characters/slime") != std::string::npos;
}

void ApplySlimeSoftMaterialIfStandard(Object3d* object) {
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

    ApplySlimeSoftMaterialIfStandard(object);
}

void ApplyLodConfig(Object3d* object, const json& objData) {
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
}


// ========================================================================
// 1. 自動で3つのファイルを読み込み、無ければ旧仕様で読み込み、最後に親子関係を解決する
// ========================================================================
void LevelLoader::LoadObjectLayout(BaseScene* scene, const std::string& filename) {
    // ロード前にプリセットを最新の状態にする
    PresetManager::GetInstance()->Initialize();
    GameplayStatusManager::GetInstance()->Initialize();

    const std::string resolvedFilename = scene
        ? scene->ResolvePrimaryObjectLayoutPath(filename)
        : filename;
    std::string justName = resolvedFilename;
    size_t slashPos = justName.find_last_of("/\\");
    if (slashPos != std::string::npos) {
        justName = justName.substr(slashPos + 1);
    }
    scene->SetLoadedFilename(justName);
    // filename は "Resources/json/3Dobject/scene_layout.json" のようになっている
    std::string baseFilename = resolvedFilename;


    size_t extPos = baseFilename.find(".json");
    if (extPos != std::string::npos) {
        baseFilename = baseFilename.substr(0, extPos);
    }

    // 以前のリストをクリア
    parentPendingList_.clear();

    // 確認用のファイルパスを作成
    std::string playerFile = baseFilename + "_player.json";
    std::string enemyFile = baseFilename + "_enemy.json";
    std::string objectFile = baseFilename + "_object.json";
    std::string cameraFile = baseFilename + "_camera.json";

    // 分割ファイルの存在チェック
    bool useSplitFiles = false;
    std::ifstream pFile(playerFile);
    std::ifstream eFile(enemyFile);
    std::ifstream oFile(objectFile);
    std::ifstream cFile(cameraFile);

    if (pFile.is_open() || eFile.is_open() || oFile.is_open() || cFile.is_open()) {
        useSplitFiles = true;
    }

    // チェック用に開いたファイルを閉じる（この後 LoadSingleJson で開くため）
    if (pFile.is_open()) pFile.close();
    if (eFile.is_open()) eFile.close();
    if (oFile.is_open()) oFile.close();
    if (cFile.is_open()) cFile.close();

    // 分岐処理：分割ファイルがあればそれを、無ければ単一ファイルを読み込む
    // ========================================================
    if (useSplitFiles) {
        // 新仕様：分割ファイルを読み込む
        LoadSingleJson(scene, playerFile);
        LoadSingleJson(scene, enemyFile);
        LoadSingleJson(scene, objectFile);
        LoadSingleJson(scene, cameraFile);
    } else {
        // 旧仕様：過去の単一ファイル (scene_layout.json など) をそのまま読み込む
        LoadSingleJson(scene, resolvedFilename);
    }

    // 全ファイルを読み込み後、親子関係を解決
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
}
// ========================================================================
// 2. 1ファイル分の読み込み処理 (実際の生成とパラメータ設定)
// ========================================================================

void LevelLoader::LoadSingleJson(BaseScene* scene, const std::string& filename) {
    json sceneData;
    auto& objects = scene->GetObjects();
    Object3dCommon* object3dCommon = scene->GetObject3dCommon();

    try {
        if (!scene->TakePreparedJson(filename, sceneData)) {
            std::ifstream file(filename);
            if (!file.is_open()) {
                return;
            }
            sceneData = json::parse(file);
        }

        if (sceneData.contains("objects") && sceneData["objects"].is_array()) {

            for (const auto& objData : sceneData["objects"]) {
                if (!objData.contains("name") || !objData["name"].is_string()) continue;
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

                            if (auto base = dynamic_cast<BaseEnemy*>(enemy.get())) {
                                base->SetTarget(scene->GetPlayer());
                            }
                            if (auto bomber = dynamic_cast<EnemyBomber*>(enemy.get())) {
                                ConfigureBomberSpawnCallback(scene, bomber);
                            }
                            newObj = std::move(enemy);
                        }
                    }
                    // パターンB: プレイヤー
                    else if (type == "Player" || name == "Player") {
                        auto player = std::make_unique<Player>();
                        player->Initialize(object3dCommon, InputManager::GetInstance(), scene->GetParticleSystem(), scene->GetSpriteCommon());
                        player->SetMoveStrategy(std::make_unique<MoveStrategy3D>());

                        // シーンにプレイヤーを登録
                        scene->SetPlayer(player.get());

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
                            if (p.contains("enemyType") && p["enemyType"].is_string()) spawnEnemyType = p["enemyType"];
                            if (p.contains("interval") && p["interval"].is_number()) interval = p["interval"];
                            if (p.contains("maxCount") && p["maxCount"].is_number()) maxCount = p["maxCount"];
                        }

                        spawner->Initialize(object3dCommon, spawnEnemyType, interval, maxCount);
                        spawner->SetClassName("Spawner");
                        spawner->SetModel(nullptr);
                        spawner->SetIsVisible(false);

                        // スポーン時のコールバック
                        spawner->SetOnSpawnCallback([scene, spawnEnemyType](const Vector3& spawnPos) {
                            auto newEnemy = EnemyFactory::GetInstance()->CreateEnemy(spawnEnemyType, scene->GetObject3dCommon());
                            if (newEnemy) {
                                newEnemy->GetTransform()->translate = spawnPos;
                                newEnemy->UpdateLocalMatrix();
                                newEnemy->UpdateWorldMatrix();
                                newEnemy->SetEnemyType(spawnEnemyType);
                                if (auto base = dynamic_cast<BaseEnemy*>(newEnemy.get())) {
                                    base->SetTarget(scene->GetPlayer());
                                }
                                if (auto bomber = dynamic_cast<EnemyBomber*>(newEnemy.get())) {
                                    ConfigureBomberSpawnCallback(scene, bomber);
                                }
                                // 直接シーンに追加
                                CollisionManager::GetInstance()->AddObject(newEnemy.get());
                                scene->GetObjects().push_back(std::move(newEnemy));
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
                            PresetManager::GetInstance()->ApplyPresetToObject(gType, gimmick.get());

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
                        if (currentClass != "Enemy" && currentClass != "Player" && currentClass != "Spawner" && currentClass != "Gimmick" && currentClass != "Item") {
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
                        CollisionManager::GetInstance()->AddObject(newObj.get());
                        objects.push_back(std::move(newObj));
                    }
                }

                if (!targetObject) continue;

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
                    targetObject->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
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
                Transform* transform = targetObject->GetTransform();
                if (objData.contains("position")) { transform->translate.x = objData["position"][0]; transform->translate.y = objData["position"][1]; transform->translate.z = objData["position"][2]; }
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
                if (objData.contains("scale")) { transform->scale.x = objData["scale"][0]; transform->scale.y = objData["scale"][1]; transform->scale.z = objData["scale"][2]; }

                // ロード直後に行列更新
                targetObject->UpdateLocalMatrix();
                targetObject->UpdateWorldMatrix();
                if (objData.contains("blendMode")) targetObject->SetBlendMode(static_cast<BlendMode>(objData["blendMode"].get<int>()));
                if (objData.contains("materialType")) targetObject->SetMaterialType(objData["materialType"].get<int>());
                if (objData.contains("meshDrawIndex")) targetObject->SetMeshDrawIndex(objData["meshDrawIndex"].get<int>());

                if (objData.contains("color")) {
                    targetObject->SetColor({
                        objData["color"][0],
                        objData["color"][1],
                        objData["color"][2],
                        objData["color"][3]
                        });
                }
                // マテリアル / グラフィック関連
                if (objData.contains("metallic")) targetObject->SetMetallic(objData["metallic"].get<float>());
                if (objData.contains("roughness")) targetObject->SetRoughness(objData["roughness"].get<float>());
                if (objData.contains("enableNormalMap")) targetObject->SetEnableNormalMap(objData["enableNormalMap"].get<bool>());
                if (objData.contains("normalMapPath")) targetObject->SetNormalMap(objData["normalMapPath"].get<std::string>());
                if (objData.contains("ormMapPath")) targetObject->SetOrmMap(objData["ormMapPath"].get<std::string>());
                if (objData.contains("texturePath")) targetObject->SetTexture(objData["texturePath"].get<std::string>());
                if (targetObject->GetMaterialType() == 0 &&
                    (IsSlimeEnemyType(targetObject->GetEnemyType()) || IsSlimeModelName(loadedModelName))) {
                    targetObject->SetMaterialType(kSlimeSoftMaterialType);
                }
                if (objData.contains("textureTiling") && objData["textureTiling"].is_array() && objData["textureTiling"].size() >= 2) {
                    targetObject->SetTextureTiling({
                        objData["textureTiling"][0].get<float>(),
                        objData["textureTiling"][1].get<float>()
                    });
                }
                if (objData.contains("autoTextureTiling")) targetObject->SetAutoTextureTiling(objData["autoTextureTiling"].get<bool>());
                if (objData.contains("enableLighting")) targetObject->SetEnableLighting(objData["enableLighting"].get<bool>());
                if (objData.contains("enableEnvMap")) targetObject->SetEnableEnvMap(objData["enableEnvMap"].get<bool>());
                if (objData.contains("envIntensity")) targetObject->SetEnvIntensity(objData["envIntensity"].get<float>());
                if (objData.contains("emissive")) targetObject->SetEmissive(objData["emissive"].get<float>());
                if (objData.contains("castShadow")) targetObject->SetCastShadow(objData["castShadow"].get<bool>());
                if (objData.contains("localFog")) {
                    if (auto* fogData = targetObject->GetLocalFogData()) {
                        auto& f = objData["localFog"];
                        if (f.contains("color")) {
                            fogData->fogColor = { f["color"][0], f["color"][1], f["color"][2], f["color"][3] };
                        }
                        if (f.contains("density")) fogData->fogDensity = f["density"];
                        if (f.contains("edgeFade")) fogData->edgeFade = f["edgeFade"];
                        if (f.contains("noiseSpeed")) fogData->noiseSpeed = f["noiseSpeed"];
                        if (f.contains("noiseScale")) fogData->noiseScale = f["noiseScale"];
                        if (f.contains("scatteringG")) fogData->scatteringG = f["scatteringG"];
                        if (f.contains("scatteringIntensity")) fogData->scatteringIntensity = f["scatteringIntensity"];
                    }
                }
                if (objData.contains("waterParam") && targetObject->GetMaterialType() >= 8 && targetObject->GetMaterialType() <= 22) {
                    if (targetObject->GetMeshRenderer() && targetObject->GetMeshRenderer()->GetWaterParamData()) {
                        auto* water = targetObject->GetMeshRenderer()->GetWaterParamData();
                        const auto& jw = objData["waterParam"];

                        if (jw.contains("waveSpeed"))     water->waveSpeed = jw["waveSpeed"];
                        if (jw.contains("waveHeight"))    water->waveHeight = jw["waveHeight"];
                        if (jw.contains("waveFrequency")) water->waveFrequency = jw["waveFrequency"];
                        if (jw.contains("flowSpeedX"))    water->flowSpeedX = jw["flowSpeedX"];
                        if (jw.contains("flowSpeedY"))    water->flowSpeedY = jw["flowSpeedY"];
                        if (jw.contains("effectType"))    water->effectType = jw["effectType"];
                        if (jw.contains("effectScale"))   water->effectScale = jw["effectScale"];
                        if (jw.contains("effectScaleX"))  water->effectScaleX = jw["effectScaleX"];
                        if (jw.contains("effectScaleY"))  water->effectScaleY = jw["effectScaleY"];
                        if (jw.contains("effectScaleZ"))  water->effectScaleZ = jw["effectScaleZ"];
                        if (jw.contains("effectSoftness")) water->effectSoftness = jw["effectSoftness"];
                        if (jw.contains("effectIntensity")) water->effectIntensity = jw["effectIntensity"];
                        if (jw.contains("billboardScale")) water->billboardScale = jw["billboardScale"];
                    }
                }
                // 5. Collider
                if (!targetObject->IsCameraObject() && objData.contains("collider")) {
                    json colData = objData["collider"];
                    Object3d::ColliderConfig config = targetObject->GetColliderConfig();
                    if (colData.contains("type")) config.type = (ColliderType)colData["type"].get<int>();
                    if (colData.contains("center")) { config.center.x = colData["center"][0]; config.center.y = colData["center"][1]; config.center.z = colData["center"][2]; }
                    if (colData.contains("size")) { config.size.x = colData["size"][0]; config.size.y = colData["size"][1]; config.size.z = colData["size"][2]; }
                    if (colData.contains("rotation")) { config.rotation.x = colData["rotation"][0]; config.rotation.y = colData["rotation"][1]; config.rotation.z = colData["rotation"][2]; }
                    targetObject->SetColliderConfig(config);
                }

                if (!targetObject->IsCameraObject() && objData.contains("collisionAttribute")) targetObject->SetCollisionAttribute(objData["collisionAttribute"]);
                if (!targetObject->IsCameraObject() && objData.contains("collisionMask")) targetObject->SetCollisionMask(objData["collisionMask"]);
                if (!targetObject->IsCameraObject() && objData.contains("isStatic")) targetObject->SetStatic(objData["isStatic"].get<bool>());
                if (!targetObject->IsCameraObject() && objData.contains("terrainCollisionPath") && objData["terrainCollisionPath"].is_string()) {
                    targetObject->LoadTerrainCollisionFromFile(objData["terrainCollisionPath"].get<std::string>());
                }

                // 6. Events & Params
                if (objData.contains("eventID")) targetObject->SetEventType(static_cast<EventType>(objData["eventID"]));
                if (objData.contains("itemType") && objData["itemType"].is_string()) {
                    targetObject->SetItemType(objData["itemType"].get<std::string>());
                }

                if (objData.contains("param") && objData["param"].is_object()) {
                    if (!targetObject->param_.has_value()) targetObject->param_.emplace();
                    json p = objData["param"];
                    auto& param = targetObject->param_.value();
                    const bool isManagedCharacter = GameplayStatusManager::IsManagedCharacter(targetObject);
                    const bool hasHp = !isManagedCharacter && p.contains("hp");
                    const bool hasMaxHp = !isManagedCharacter && p.contains("maxHp");
                    if (hasHp) param.hp = p["hp"];
                    if (hasMaxHp) param.maxHp = p["maxHp"];
                    if (!isManagedCharacter && p.contains("attackPower")) param.attackPower = p["attackPower"];
                    if (!isManagedCharacter && p.contains("speed")) param.speed = p["speed"];
                    if (!isManagedCharacter && p.contains("gravity")) param.gravity = p["gravity"];
                    if (!isManagedCharacter && p.contains("jumpPower")) param.jumpPower = p["jumpPower"];
                    if (!isManagedCharacter && p.contains("morphLimited")) param.morphLimited = p["morphLimited"];
                    if (!isManagedCharacter && p.contains("morphDuration")) param.morphDuration = p["morphDuration"];
                    if (!isManagedCharacter && p.contains("maxFallSpeed")) param.maxFallSpeed = p["maxFallSpeed"];
                    if (p.contains("enemyType")) param.enemyType = p["enemyType"];
                    if (p.contains("gimmickType")) param.gimmickType = p["gimmickType"];
                    if (p.contains("itemType")) param.itemType = p["itemType"];
                    if (p.contains("healAmount")) param.healAmount = p["healAmount"];
                    if (p.contains("interval")) param.interval = p["interval"];
                    if (p.contains("maxCount")) param.maxCount = p["maxCount"];
                    if (p.contains("shakeDuration")) param.shakeDuration = p["shakeDuration"];
                    if (p.contains("fallDuration")) param.fallDuration = p["fallDuration"];
                    if (p.contains("colorType")) param.colorType = p["colorType"];
                    if (!isManagedCharacter && p.contains("detectionRange")) param.detectionRange = p["detectionRange"];
                    if (p.contains("switchMode")) param.switchMode = p["switchMode"];
                    if (p.contains("actionMode")) param.actionMode = p["actionMode"];
                    if (p.contains("targetScene")) param.targetScene = p["targetScene"].get<std::string>();
                    if (p.contains("moveAmount")) param.moveAmount = p["moveAmount"];
                    if (p.contains("moveSpeed")) param.moveSpeed = p["moveSpeed"];
                    if (p.contains("startActive")) param.startActive = p["startActive"];
                    if (p.contains("returnOnOff")) param.returnOnOff = p["returnOnOff"];
                    if (!isManagedCharacter) {
                        param.maxHp = (std::max)(param.maxHp, 1.0f);
                        if (hasMaxHp && !hasHp) {
                            param.hp = param.maxHp;
                        }
                        param.hp = (std::max)(param.hp, 0.0f);
                        if (param.hp > param.maxHp) {
                            param.maxHp = param.hp;
                        }
                        param.attackPower = (std::max)(param.attackPower, 0.0f);
                    }
                    if (!isManagedCharacter && (targetObject->GetClassName() == "Enemy" || !targetObject->GetEnemyType().empty())) {
                        param.morphDuration = (std::max)(param.morphDuration, 0.1f);
                    }
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
                if (objData.contains("animName")) targetObject->animName_ = objData["animName"];
                if (objData.contains("isAnimLoop")) targetObject->isAnimLoop_ = objData["isAnimLoop"];

                // ネストされた形式の読み込み（最新仕様）
                if (objData.contains("animation")) {
                    const auto& anim = objData["animation"];
                    if (anim.contains("animName")) targetObject->animName_ = anim["animName"];
                    if (anim.contains("isAnimLoop")) targetObject->isAnimLoop_ = anim["isAnimLoop"];
                    if (anim.contains("animatorController")) {
                        targetObject->SetAnimatorController(anim.value("animatorController", ""));
                    }
                }
                targetObject->InitializeRecorder(nullptr);
                bool isCinematic = targetObject->IsCameraObject();

                // パスデータが存在する場合に再生を開始
                if (!targetObject->GetRecordPathName().empty() && targetObject->recorder_) {
                    targetObject->recorder_->Play(
                        targetObject->GetRecordPathName(),
                        targetObject->IsRecordLoop(),
                        targetObject->IsRecordRelative(),
                        isCinematic
                    );
                }

                // 8. 親子関係保留 (一括解決のためにメンバ変数のリストに追加しておく)
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
    }
    catch (const json::parse_error& e) {
        std::string message = "Failed to parse " + filename + " : " + e.what() + "\n";
        OutputDebugStringA(message.c_str());
    }
}




void LevelLoader::LoadSpriteLayout(BaseScene* scene, const std::string& filename) {
    const std::string resolvedFilename = scene
        ? scene->ResolvePrimarySpriteLayoutPath(filename)
        : filename;
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
                if (!spriteData.contains("name")) continue;
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
                    if (spriteData.contains("texture")) textureFile = spriteData["texture"];
                    uint32_t handle = textureFile.empty() ? 0 : Sprite::LoadTexture(textureFile);

                    auto newSprite = std::make_unique<Sprite>();
                    newSprite->Initialize(spriteCommon, handle);
                    newSprite->SetName(name);
                    newSprite->SetTextureName(textureFile);
                    targetSprite = newSprite.get();
                    sprites.push_back(std::move(newSprite));
                }

                if (targetSprite) {
                    if (spriteData.contains("position")) {
                        targetSprite->SetPosition(SpriteLayoutScaler::ScalePosition(
                            SpriteLayoutScaler::ReadVector2(spriteData["position"], targetSprite->GetPosition()),
                            layoutScale
                        ));
                    }
                    if (spriteData.contains("size")) {
                        targetSprite->SetSize(SpriteLayoutScaler::ScaleSize(
                            SpriteLayoutScaler::ReadVector2(spriteData["size"], targetSprite->GetSize()),
                            layoutScale
                        ));
                    }
                    if (spriteData.contains("anchor")) targetSprite->SetAnchorPoint({ spriteData["anchor"][0], spriteData["anchor"][1] });
                    if (spriteData.contains("color")) {
                        Vector4 currentColor = targetSprite->GetColor();
                        float alpha = currentColor.w;
                        if (spriteData["color"].is_array() && spriteData["color"].size() >= 4) {
                            alpha = spriteData["color"][3];
                        }
                        targetSprite->SetColor({
                            spriteData["color"][0], // R
                            spriteData["color"][1], // G
                            spriteData["color"][2], // B
                            alpha
                            });
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
                if (!sprite) continue;

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
    }
    catch (...) {}
}
