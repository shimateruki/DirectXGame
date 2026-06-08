#include "LevelLoader.h"
#include "BaseScene.h" 
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <utility>
#include <Windows.h> // OutputDebugStringA用

// 生成するクラス群のインクルード
#include "Object3d.h"
#include "Sprite.h"
#include "Player.h"
#include "EnemyFactory.h"
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

using json = nlohmann::json;

namespace {
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
}


// ========================================================================
// 1. 自動で3つのファイルを読み込み、無ければ旧仕様で読み込み、最後に親子関係を解決する
// ========================================================================
void LevelLoader::LoadObjectLayout(BaseScene* scene, const std::string& filename) {
    // ロード前にプリセットを最新の状態にする
    PresetManager::GetInstance()->Initialize();

    std::string justName = filename;
    size_t slashPos = justName.find_last_of("/\\");
    if (slashPos != std::string::npos) {
        justName = justName.substr(slashPos + 1);
    }
    scene->SetLoadedFilename(justName);
    // filename は "Resources/json/3Dobject/scene_layout.json" のようになっている
    std::string baseFilename = filename;


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

    // 分割ファイルの存在チェック
    bool useSplitFiles = false;
    std::ifstream pFile(playerFile);
    std::ifstream eFile(enemyFile);
    std::ifstream oFile(objectFile);

    if (pFile.is_open() || eFile.is_open() || oFile.is_open()) {
        useSplitFiles = true;
    }

    // チェック用に開いたファイルを閉じる（この後 LoadSingleJson で開くため）
    if (pFile.is_open()) pFile.close();
    if (eFile.is_open()) eFile.close();
    if (oFile.is_open()) oFile.close();

    // 分岐処理：分割ファイルがあればそれを、無ければ単一ファイルを読み込む
    // ========================================================
    if (useSplitFiles) {
        // 新仕様：分割ファイルを読み込む
        LoadSingleJson(scene, playerFile);
        LoadSingleJson(scene, enemyFile);
        LoadSingleJson(scene, objectFile);
    } else {
        // 旧仕様：過去の単一ファイル (scene_layout.json など) をそのまま読み込む
        LoadSingleJson(scene, filename);
    }

    // 全ファイルを読み込み後、親子関係を解決
    auto& objects = scene->GetObjects();
    for (auto const& [childObj, parentName] : parentPendingList_) {
        Object3d* parentObj = nullptr;
        for (auto& obj : objects) {
            if (obj && obj->GetName() == parentName) {
                parentObj = obj.get();
                break;
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
    std::ifstream file(filename);
    if (!file.is_open()) {
        return;
    }

    json sceneData;
    auto& objects = scene->GetObjects();
    Object3dCommon* object3dCommon = scene->GetObject3dCommon();

    try {
        sceneData = json::parse(file);

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
                            // プリセットによるパラメータ上書き
                            PresetManager::GetInstance()->ApplyPresetToObject(enemyType, enemy.get());

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
                        if (objData.contains("saveCategory") && objData["saveCategory"].is_string()) {
                            newObj->SetSaveCategory(objData["saveCategory"].get<std::string>());
                        } else {
                            // 過去の互換性用: categoryが無ければクラス名(Type)から推測する
                            if (currentClass == "Player" || type == "Player") {
                                newObj->SetSaveCategory("Player");
                            } else if (currentClass == "Enemy" || currentClass == "Spawner" || type == "Spawner") {
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

                // 3. モデル・表示設定
                std::string type = targetObject->GetClassName();
                if (type == "InvisibleBox" || type == "Spawner") {
                    targetObject->SetModel(nullptr);
                    targetObject->SetIsVisible(true);
                } else {
                    targetObject->SetIsVisible(true);
                    if (objData.contains("modelName") && objData["modelName"].is_string()) {
                        std::string modelName = objData["modelName"].get<std::string>();
                        // モデルロードはManagerに任せる
                        ModelManager::GetInstance()->LoadModel(modelName);
                        targetObject->SetModel(modelName);
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
                if (objData.contains("enableEnvMap")) targetObject->SetEnableEnvMap(objData["enableEnvMap"].get<bool>());
                if (objData.contains("envIntensity")) targetObject->SetEnvIntensity(objData["envIntensity"].get<float>());
                if (objData.contains("emissive")) targetObject->SetEmissive(objData["emissive"].get<float>());
                if (objData.contains("particleName")) targetObject->SetParticleName(objData["particleName"].get<std::string>());
                if (objData.contains("gpuParticleName")) targetObject->SetGPUParticleName(objData["gpuParticleName"].get<std::string>());
                if (objData.contains("meshEffect1")) targetObject->SetMeshEffect1Name(objData["meshEffect1"].get<std::string>());
                if (objData.contains("meshEffect2")) targetObject->SetMeshEffect2Name(objData["meshEffect2"].get<std::string>());
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
                if (objData.contains("waterParam") && targetObject->GetMaterialType() >= 8 && targetObject->GetMaterialType() <= 20) {
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
                        if (jw.contains("effectSoftness")) water->effectSoftness = jw["effectSoftness"];
                        if (jw.contains("effectIntensity")) water->effectIntensity = jw["effectIntensity"];
                        if (jw.contains("billboardScale")) water->billboardScale = jw["billboardScale"];
                    }
                }
                // 5. Collider
                if (objData.contains("collider")) {
                    json colData = objData["collider"];
                    Object3d::ColliderConfig config = targetObject->GetColliderConfig();
                    if (colData.contains("type")) config.type = (ColliderType)colData["type"].get<int>();
                    if (colData.contains("center")) { config.center.x = colData["center"][0]; config.center.y = colData["center"][1]; config.center.z = colData["center"][2]; }
                    if (colData.contains("size")) { config.size.x = colData["size"][0]; config.size.y = colData["size"][1]; config.size.z = colData["size"][2]; }
                    if (colData.contains("rotation")) { config.rotation.x = colData["rotation"][0]; config.rotation.y = colData["rotation"][1]; config.rotation.z = colData["rotation"][2]; }
                    targetObject->SetColliderConfig(config);
                }

                if (objData.contains("collisionAttribute")) targetObject->SetCollisionAttribute(objData["collisionAttribute"]);
                if (objData.contains("collisionMask")) targetObject->SetCollisionMask(objData["collisionMask"]);

                // 6. Events & Params
                if (objData.contains("eventID")) targetObject->SetEventType(static_cast<EventType>(objData["eventID"]));
                if (objData.contains("targetID")) targetObject->SetTargetID(objData["targetID"]);
                if (objData.contains("myEventID")) targetObject->SetEventID(objData["myEventID"]);
                if (objData.contains("itemType") && objData["itemType"].is_string()) {
                    targetObject->SetItemType(objData["itemType"].get<std::string>());
                }

                if (objData.contains("param") && objData["param"].is_object()) {
                    if (!targetObject->param_.has_value()) targetObject->param_.emplace();
                    json p = objData["param"];
                    auto& param = targetObject->param_.value();
                    if (p.contains("hp")) param.hp = p["hp"];
                    if (p.contains("maxHp")) param.maxHp = p["maxHp"];
                    if (p.contains("speed")) param.speed = p["speed"];
                    if (p.contains("gravity")) param.gravity = p["gravity"];
                    if (p.contains("jumpPower")) param.jumpPower = p["jumpPower"];
                    if (p.contains("maxFallSpeed")) param.maxFallSpeed = p["maxFallSpeed"];
                    if (p.contains("enemyType")) param.enemyType = p["enemyType"];
                    if (p.contains("gimmickType")) param.gimmickType = p["gimmickType"];
                    if (p.contains("itemType")) param.itemType = p["itemType"];
                    if (p.contains("healAmount")) param.healAmount = p["healAmount"];
                    if (p.contains("interval")) param.interval = p["interval"];
                    if (p.contains("maxCount")) param.maxCount = p["maxCount"];
                    if (p.contains("shakeDuration")) param.shakeDuration = p["shakeDuration"];
                    if (p.contains("fallDuration")) param.fallDuration = p["fallDuration"];
                    if (p.contains("colorType")) param.colorType = p["colorType"];
                    if (p.contains("detectionRange")) param.detectionRange = p["detectionRange"];
                    if (p.contains("switchMode")) param.switchMode = p["switchMode"];
                    if (p.contains("actionMode")) param.actionMode = p["actionMode"];
                    if (p.contains("moveAmount")) param.moveAmount = p["moveAmount"];
                    if (p.contains("moveSpeed")) param.moveSpeed = p["moveSpeed"];
                    if (p.contains("startActive")) param.startActive = p["startActive"];
                    if (p.contains("returnOnOff")) param.returnOnOff = p["returnOnOff"];
                }

                // ==========================================
                // 7. Animation & Recorder
                // ==========================================

                // 古いフラットな形式の読み込み（後方互換性用）
                if (objData.contains("animName")) targetObject->animName_ = objData["animName"];
                if (objData.contains("isAnimLoop")) targetObject->isAnimLoop_ = objData["isAnimLoop"];
                if (objData.contains("isAnimRelative")) targetObject->isRecordRelative_ = objData["isAnimRelative"];

                // ネストされた形式の読み込み（最新仕様）
                if (objData.contains("animation")) {
                    const auto& anim = objData["animation"];
                    if (anim.contains("animName")) targetObject->animName_ = anim["animName"];
                    if (anim.contains("isAnimLoop")) targetObject->isAnimLoop_ = anim["isAnimLoop"];
                }
                if (objData.contains("recorder")) {
                    const auto& rec = objData["recorder"];
                    if (rec.contains("recordPathName")) targetObject->recordPathName_ = rec["recordPathName"];
                    if (rec.contains("isRecordLoop")) targetObject->isRecordLoop_ = rec["isRecordLoop"];
                    if (rec.contains("isRecordRelative")) targetObject->isRecordRelative_ = rec["isRecordRelative"];
                }

                targetObject->InitializeRecorder(nullptr);
                bool isCinematic = (targetObject->GetClassName() == "CinematicCamera");

                // パスデータが存在する場合に再生を開始
                if (!targetObject->recordPathName_.empty() && targetObject->recorder_) {
                    targetObject->recorder_->Play(
                        targetObject->recordPathName_,
                        targetObject->isRecordLoop_,
                        targetObject->isRecordRelative_,
                        isCinematic
                    );
                }

                // 8. 親子関係保留 (一括解決のためにメンバ変数のリストに追加しておく)
                if (objData.contains("parentName") && objData["parentName"].is_string()) {
                    std::string pName = objData["parentName"].get<std::string>();
                    if (!pName.empty()) parentPendingList_[targetObject] = pName;
                }
            }
        }
    }
    catch (const json::parse_error& e) {
        std::string message = "Failed to parse " + filename + " : " + e.what() + "\n";
        OutputDebugStringA(message.c_str());
    }
    file.close();
}




void LevelLoader::LoadSpriteLayout(BaseScene* scene, const std::string& filename) {
    std::string justName = filename;
    size_t slashPos = justName.find_last_of("/\\");
    if (slashPos != std::string::npos) {
        justName = justName.substr(slashPos + 1);
    }
    scene->SetLoadedSpriteFilename(justName);
    std::ifstream file(filename);
    if (!file.is_open()) return;

    auto& sprites = scene->GetSprites();
    SpriteCommon* spriteCommon = scene->GetSpriteCommon();

    json layoutData;
    try {
        layoutData = json::parse(file);
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
                    if (spriteData.contains("position")) targetSprite->SetPosition({ spriteData["position"][0], spriteData["position"][1] });
                    if (spriteData.contains("size")) targetSprite->SetSize({ spriteData["size"][0], spriteData["size"][1] });
                    if (spriteData.contains("anchor")) targetSprite->SetAnchorPoint({ spriteData["anchor"][0], spriteData["anchor"][1] });
                    if (spriteData.contains("color")) {
                        Vector4 currentColor = targetSprite->GetColor();
                        targetSprite->SetColor({
                            spriteData["color"][0], // R
                            spriteData["color"][1], // G
                            spriteData["color"][2], // B
                            currentColor.w          // A (現在の透明度を維持)
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
    file.close();
}
