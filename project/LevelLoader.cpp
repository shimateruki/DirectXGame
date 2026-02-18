#include "LevelLoader.h"
#include "BaseScene.h" 
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <Windows.h> // OutputDebugStringA用

// 生成するクラス群のインクルード
#include "Object3d.h"
#include "Sprite.h"
#include "Player.h"
#include "EnemyFactory.h"
#include "EnemySpawner.h"
#include "BaseEnemy.h"
#include "MoveStrategy3D.h"
#include "GhostRecorder.h"
// マネージャ系
#include "ModelManager.h"
#include "CollisionManager.h"
#include "InputManager.h"

using json = nlohmann::json;

void LevelLoader::LoadObjectLayout(BaseScene* scene, const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::string warnMsg = "Warning: Could not open " + filename + " for Object layout.\n";
        OutputDebugStringA(warnMsg.c_str());
        return;
    }

    json sceneData;
    parentPendingList_.clear();

    // シーンからリストへの参照を取得
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

                    // --- 生成分岐 ---

                    // パターンA: 敵 (Factory)
                    if (!enemyType.empty()) {
                        auto enemy = EnemyFactory::GetInstance()->CreateEnemy(enemyType, object3dCommon);
                        if (enemy) {
                            enemy->SetEnemyType(enemyType);
                            if (auto base = dynamic_cast<BaseEnemy*>(enemy.get())) {
                                base->SetTarget(scene->GetPlayer());
                            }
                            newObj = std::move(enemy);
                        }
                    }
                    // パターンB: プレイヤー
                    else if (type == "Player" || name == "Player") {
                        auto player = std::make_unique<Player>();
                        player->Initialize(object3dCommon, InputManager::GetInstance(), scene->GetParticleSystem());
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
                                // 直接シーンに追加
                                CollisionManager::GetInstance()->AddObject(newEnemy.get());
                                scene->GetObjects().push_back(std::move(newEnemy));
                            }
                            });
                        newObj = std::move(spawner);
                    }
                    // パターンD: 通常オブジェクト
                    else {
                        newObj = std::make_unique<Object3d>();
                        newObj->Initialize(object3dCommon);
                    }

                    if (newObj) {
                        newObj->SetName(name);
                        std::string currentClass = newObj->GetClassName();
                        if (currentClass != "Enemy" && currentClass != "Player" && currentClass != "Spawner") {
                            newObj->SetClassName(type);
                        }
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
                    targetObject->SetIsVisible(false);
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
                if (objData.contains("rotation")) { transform->rotate.x = objData["rotation"][0]; transform->rotate.y = objData["rotation"][1]; transform->rotate.z = objData["rotation"][2]; }
                if (objData.contains("scale")) { transform->scale.x = objData["scale"][0]; transform->scale.y = objData["scale"][1]; transform->scale.z = objData["scale"][2]; }

                // ロード直後に行列更新
                targetObject->UpdateLocalMatrix();
                targetObject->UpdateWorldMatrix();

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
                    if (p.contains("interval")) param.interval = p["interval"];
                    if (p.contains("maxCount")) param.maxCount = p["maxCount"];
                }

                // 7. Animation
                if (objData.contains("animName")) targetObject->animName_ = objData["animName"];
                if (objData.contains("isAnimLoop")) targetObject->isAnimLoop_ = objData["isAnimLoop"];
                if (objData.contains("isAnimRelative")) targetObject->isAnimRelative_ = objData["isAnimRelative"];

                targetObject->InitializeRecorder(nullptr);
                bool isCinematic = (targetObject->GetClassName() == "CinematicCamera");

                targetObject->recorder_->Play(
                    targetObject->animName_,
                    targetObject->isAnimLoop_,
                    targetObject->isAnimRelative_,
                    isCinematic 
                );

                // 8. 親子関係保留
                if (objData.contains("parentName") && objData["parentName"].is_string()) {
                    std::string pName = objData["parentName"].get<std::string>();
                    if (!pName.empty()) parentPendingList_[targetObject] = pName;
                }
            }
        }

        // 9. 親子解決
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
    }
    catch (const json::parse_error& e) {
        std::string message = "Failed to parse " + filename + " : " + e.what() + "\n";
        OutputDebugStringA(message.c_str());
    }
    file.close();
}

void LevelLoader::LoadSpriteLayout(BaseScene* scene, const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;

    auto& sprites = scene->GetSprites();
    SpriteCommon* spriteCommon = scene->GetSpriteCommon();

    json layoutData;
    try {
        layoutData = json::parse(file);
        if (layoutData.contains("sprites") && layoutData["sprites"].is_array()) {
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
                    targetSprite = newSprite.get();
                    sprites.push_back(std::move(newSprite));
                }

                if (targetSprite) {
                    if (spriteData.contains("position")) targetSprite->SetPosition({ spriteData["position"][0], spriteData["position"][1] });
                    if (spriteData.contains("size")) targetSprite->SetSize({ spriteData["size"][0], spriteData["size"][1] });
                    if (spriteData.contains("anchor")) targetSprite->SetAnchorPoint({ spriteData["anchor"][0], spriteData["anchor"][1] });
                    targetSprite->Update();
                }
            }
        }
    }
    catch (...) {}
    file.close();
}