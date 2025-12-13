#include "TitleScene.h"
#include "SceneManager.h"    
#include"CameraManager.h"
#include <cassert>
#include <fstream>          
#include "json.hpp"         
#include "ModelManager.h"   
#include "Object3d.h"       
#include "Object3dCommon.h" 
#include <CollisionManager.h>

void TitleScene::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();

    // スプライト共通基盤の初期化
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    // 3Dオブジェクト共通基盤の初期化
    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_); 
    
    // (TitleScene 用のカメラ位置を設定)
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
  

    // (テスト用) 3Dオブジェクトを1つ生成
    auto plane = std::make_unique<Object3d>();
    plane->Initialize(object3dCommon_.get());
    plane->SetModel("block");
    plane->SetName("PlaneDebau");
    plane->SetCollisionSize({ 1.0f,1.0f,1.0f });
    objects_.emplace_back(std::move(plane));

    // タイトルロゴのスプライトを生成
    titleLogoHandle_ = Sprite::LoadTexture("monsterBall.png");
    auto titleLogo = std::make_unique<Sprite>();
    titleLogo->Initialize(spriteCommon_.get(), titleLogoHandle_);
    titleLogo->SetPosition({ 640.0f, 360.0f });
    titleLogo->SetSize({ 200.0f, 200.0f });
    titleLogo->SetAnchorPoint({ 0.5f, 0.5f });
    titleLogo->SetName("TitleLogo");

    sprites_.push_back(std::move(titleLogo));

    // レイアウト読み込み
    LoadObjectLayout("scene_layout.json");
    LoadSpriteLayout("sprite_layout.json");

    //コマンドリストが安全に閉じるためのやつないとバグる
    dxCommon_->FlushCommandQueue(false);
}

void TitleScene::Finalize() {
    sprites_.clear();
    spriteCommon_.reset();

    // 3Dオブジェクトの解放
    objects_.clear();
    object3dCommon_.reset();
}

void TitleScene::Update(float deltaTime) {
    (void)deltaTime;
    CameraManager::GetInstance()->Update();
    // 3Dオブジェクトの更新
    for (auto& obj : objects_) {
        obj->Update(deltaTime);
    }
    for (auto& obj : objects_) {
        obj->UpdateLocalMatrix();
    }

    
    for (auto& obj : objects_) {
        obj->UpdateWorldMatrix();
    }
    // スプライトの更新
    for (auto& sprite : sprites_) {
        sprite->Update();
    }

    // Enterキーが押されたら GamePlayScene に切り替え
    if (inputManager_->IsKeyTriggered(DIK_RETURN)) {
        sceneManager_->ChangeScene("GAMEPLAY");
    }
    ProcessRemovals();
}

void TitleScene::Draw() {

    // 3Dオブジェクトの描画
    object3dCommon_->SetGraphicsCommand();
 /*   for (auto& obj : objects_) {
        obj->Draw();
    }*/

    // スプライトの描画
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    for (auto& sprite : sprites_) {
        sprite->Draw();
    }
}

#pragma region Editor Functions
void TitleScene::LoadObjectLayout(const std::string& filename) {
    using json = nlohmann::json;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::string warnMsg = "Warning: Could not open " + filename + " for Object layout.\n";
        OutputDebugStringA(warnMsg.c_str());
        return;
    }

    json sceneData;
    try {
        sceneData = json::parse(file);
        if (sceneData.contains("objects") && sceneData["objects"].is_array()) {
            for (const auto& objData : sceneData["objects"]) {
                if (!objData.contains("name") || !objData["name"].is_string()) continue;
                std::string name = objData["name"].get<std::string>();

                Object3d* targetObject = nullptr;
                for (auto& obj : objects_) {
                    if (obj && !obj->GetName().empty() && obj->GetName() == name) {
                        targetObject = obj.get();
                        break;
                    }
                }

                if (targetObject) {
                    Object3d::Transform* transform = targetObject->GetTransform();
                    if (objData.contains("position") && objData["position"].is_array() && objData["position"].size() == 3) {
                        transform->translate.x = objData["position"][0].get<float>();
                        transform->translate.y = objData["position"][1].get<float>();
                        transform->translate.z = objData["position"][2].get<float>();
                    }
                    if (objData.contains("rotation") && objData["rotation"].is_array() && objData["rotation"].size() == 3) {
                        transform->rotate.x = objData["rotation"][0].get<float>();
                        transform->rotate.y = objData["rotation"][1].get<float>();
                        transform->rotate.z = objData["rotation"][2].get<float>();
                    }
                    if (objData.contains("scale") && objData["scale"].is_array() && objData["scale"].size() == 3) {
                        transform->scale.x = objData["scale"][0].get<float>();
                        transform->scale.y = objData["scale"][1].get<float>();
                        transform->scale.z = objData["scale"][2].get<float>();
                    }
                 
                }
            }
        }
    }
    catch (json::parse_error& e) {
        OutputDebugStringA(("Failed to parse " + filename + "\n").c_str());
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
    }

    file.close();
}

void TitleScene::LoadSpriteLayout(const std::string& filename) {
    using json = nlohmann::json;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::string warnMsg = "Warning: Could not open " + filename + "\n";
        OutputDebugStringA(warnMsg.c_str());
        return;
    }

    json layoutData;
    try {
        layoutData = json::parse(file);

        if (layoutData.contains("sprites") && layoutData["sprites"].is_array()) {
            for (const auto& spriteData : layoutData["sprites"]) {

                if (!spriteData.contains("name") || !spriteData["name"].is_string()) {
                    continue;
                }
                std::string name = spriteData["name"];

                Sprite* targetSprite = nullptr;
                for (auto& sprite : sprites_) {
                    if (sprite && !sprite->GetName().empty() && sprite->GetName() == name) {
                        targetSprite = sprite.get();
                        break;
                    }
                }

                if (targetSprite) {
                    if (spriteData.contains("position") && spriteData["position"].is_array() && spriteData["position"].size() == 2) {
                        targetSprite->SetPosition({
                            spriteData["position"][0].get<float>(),
                            spriteData["position"][1].get<float>()
                            });
                    }
                    if (spriteData.contains("size") && spriteData["size"].is_array() && spriteData["size"].size() == 2) {
                        targetSprite->SetSize({
                            spriteData["size"][0].get<float>(),
                            spriteData["size"][1].get<float>()
                            });
                    }
                    if (spriteData.contains("anchor") && spriteData["anchor"].is_array() && spriteData["anchor"].size() == 2) {
                        targetSprite->SetAnchorPoint({
                            spriteData["anchor"][0].get<float>(),
                            spriteData["anchor"][1].get<float>()
                            });
                    }
                    if (spriteData.contains("color") && spriteData["color"].is_array() && spriteData["color"].size() == 4) {
                        targetSprite->SetColor({
                            spriteData["color"][0].get<float>(),
                            spriteData["color"][1].get<float>(),
                            spriteData["color"][2].get<float>(),
                            spriteData["color"][3].get<float>()
                            });
                    }
                    targetSprite->Update();
                }
            }
        }

    }
    catch (json::parse_error& e) {
        OutputDebugStringA("Failed to parse sprite_layout.json\n");
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
    }

    file.close();
}

void TitleScene::RequestRemoveObject(Object3d* object) {
    if (object) {
        removalList_.push_back(object);
    }
}

void TitleScene::ProcessRemovals() {
    if (removalList_.empty()) {
        return;
    }

    CollisionManager* colManager = CollisionManager::GetInstance();

    // 1. CollisionManager から削除
    for (Object3d* obj : removalList_) {
        colManager->RemoveObject(obj);
    }

    // 2. objects_ (unique_ptr) リストから削除
    auto it = std::remove_if(objects_.begin(), objects_.end(),
        [this](const std::unique_ptr<Object3d>& p) {
            for (Object3d* removalObj : removalList_) {
                if (p.get() == removalObj) {
                    return true; // 削除対象
                }
            }
            return false;
        }
    );
    objects_.erase(it, objects_.end());

    // 3. 予約リストをクリア
    removalList_.clear();
}
#pragma endregion