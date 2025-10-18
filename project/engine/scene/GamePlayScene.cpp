#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef min
#undef max

#include <fstream>
#include <string>
#include "externals/nlohmann/json.hpp"
using json = nlohmann::json;

#include "GamePlayScene.h"
#include "engine/base/DirectXCommon.h"
#include "engine/io/InputManager.h"
#include "engine/audio/AudioPlayer.h"
#include "engine/3d/Object3dCommon.h"
#include "engine/2d/SpriteCommon.h"
#include "engine/3d/Object3d.h"
#include "engine/2d/Sprite.h"
#include "engine/3d/ModelManager.h"
#include "engine/3d/TextureManager.h"
#include "engine/3d/CameraManager.h"
#include "engine/3d/CollisionManager.h" // ★ 追加
#include "engine/3d/ParticleSystem.h" // ★ 追加
#include "externals/imgui/imgui.h"

// ▼▼▼ ゲーム側のオブジェクトをインクルード ▼▼▼
#include "Player.h"





void GamePlayScene::Initialize() {


    // --- 基盤クラスのポインタを保持 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    // --- 各種初期化 ---
    bgmHandle_ = audioPlayer_->LoadSoundFile("resouces/bgm/Alarm02.mp3");
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);
    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    // --- オブジェクトの生成 ---
    // Plane
    auto plane = std::make_unique<Object3d>();
    plane->Initialize(object3dCommon_.get());
    plane->SetModel("plane");
    // plane->SetCollisionAttribute(kGround); // 地面にも判定を持たせる場合はコメントアウト解除
    // plane->SetColliderType(ColliderType::kAABB);
    // plane->SetCollisionSize({10.0f, 0.1f, 10.0f}); // 薄い箱
    objects_.emplace_back(std::move(plane));

    // Player (Teapot)
    auto player = std::make_unique<Player>();
    player->Initialize(object3dCommon_.get()); // Player独自のInitializeが呼ばれる
    player->SetModel("teapot");
    player->SetTranslate({ 2.0f, 0.0f, 0.0f });
    player->SetName("Player");
    objects_.emplace_back(std::move(player));

 

    // Enemy (Bunny)
    auto enemy = std::make_unique<Object3d>();
    enemy->Initialize(object3dCommon_.get());
    enemy->SetModel("bunny");
    enemy->SetTranslate({ -2.0f, 0.0f, 0.0f });
    objects_.emplace_back(std::move(enemy));

    // Block (fence)
    for (int i = 0; i < 5; ++i) {
        auto block = std::make_unique<Object3d>();
        block->Initialize(object3dCommon_.get());
        block->SetModel("block");
        block->SetTranslate({ -4.0f, 0.0f, (float)i * 1.8f - 4.0f });
        objects_.emplace_back(std::move(block));
    }

    // --- カメラの追尾設定 ---
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    camera->SetTarget(&objects_[1]->GetTransform()->translate); // Playerを追尾

    // --- 衝突判定の設定 ---
    CollisionManager::GetInstance()->ClearObjects();

    // Player (Index 1)
    objects_[1]->SetCollisionAttribute(kPlayer);
    objects_[1]->SetCollisionMask(~kPlayer);
    // (形状とサイズは Player::Initialize で設定済み)
    CollisionManager::GetInstance()->AddObject(objects_[1].get());

    // Enemy (Index 2)
    objects_[2]->SetCollisionAttribute(kEnemy);
    objects_[2]->SetCollisionMask(~kEnemy);
    objects_[2]->SetColliderType(ColliderType::kSphere); // 敵は球判定にしてみる
    objects_[2]->SetCollisionRadius(1.0f);
    CollisionManager::GetInstance()->AddObject(objects_[2].get());

    // Blocks (Index 3 から)
    for (size_t i = 3; i < objects_.size(); ++i) {
        objects_[i]->SetCollisionAttribute(kGround);
        objects_[i]->SetCollisionMask(~kGround);
        objects_[i]->SetColliderType(ColliderType::kAABB); // ★ 地形はAABB
        objects_[i]->SetCollisionSize({ 1.0f, 1.0f, 1.0f }); // (フェンスの大きさに合わせる)
        CollisionManager::GetInstance()->AddObject(objects_[i].get());
    }

    // --- スプライトの生成 ---
    // (複数スプライト対応版のコード)
    uint32_t monsterBallHandle = Sprite::LoadTexture("monsterBall");
    auto monsterBallSprite = std::make_unique<Sprite>();
    monsterBallSprite->Initialize(spriteCommon_.get(), monsterBallHandle);
    monsterBallSprite->SetPosition({ 200.0f, 360.0f });
    monsterBallSprite->SetSize({ 100.0f, 100.0f });
    sprites_.push_back(std::move(monsterBallSprite));
    uint32_t flameHandle = Sprite::LoadTexture("flame");
    auto flameSprite = std::make_unique<Sprite>();
    flameSprite->Initialize(spriteCommon_.get(), flameHandle);
    flameSprite->SetAnimation(4, 0.15f, true);
    flameSprite->Play();
    flameSprite->SetPosition({ 640.0f, 360.0f });
    sprites_.push_back(std::move(flameSprite));

    // --- パーティクルの初期化 ---
    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);
    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get());


    // デバッグエディタ用に保存されたシーンレイアウトを読み込む
    std::ifstream file("scene_layout.json");
    if (file.is_open()) {
        json sceneData;
        try {
            sceneData = json::parse(file); // JSONファイルをパース

            // "objects" 配列が存在するかチェック
            if (sceneData.contains("objects") && sceneData["objects"].is_array()) {

                // JSON内の全オブジェクトデータをループ
                for (const auto& objData : sceneData["objects"]) {

                    // 1. オブジェクト名をJSONから取得
                    std::string name = objData["name"];

                    // 2. objects_ リストから同じ名前のオブジェクトを検索
                    Object3d* targetObject = nullptr;
                    for (auto& obj : objects_) {
                        if (obj->GetName() == name) {
                            targetObject = obj.get();
                            break;
                        }
                    }

                    // 3. オブジェクトが見つかったら、Transformを上書き
                    if (targetObject) {
                        Object3d::Transform* transform = targetObject->GetTransform();

                        transform->translate.x = objData["position"][0];
                        transform->translate.y = objData["position"][1];
                        transform->translate.z = objData["position"][2];

                        transform->rotate.x = objData["rotation"][0];
                        transform->rotate.y = objData["rotation"][1];
                        transform->rotate.z = objData["rotation"][2];

                        transform->scale.x = objData["scale"][0];
                        transform->scale.y = objData["scale"][1];
                        transform->scale.z = objData["scale"][2];
                    }
                }
            }
        }
        catch (json::parse_error& e) {
            // JSONのパースに失敗した場合（ファイルが壊れているなど）
            OutputDebugStringA("Failed to parse scene_layout.json\n");
            OutputDebugStringA(e.what());
        }
        file.close();
    }





    dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Finalize() {
    // Gameクラスから移動してきた終了処理
    objects_.clear();
    sprites_.clear(); // ★ 複数スプライト対応
    object3dCommon_.reset();
    spriteCommon_.reset();
    particleCommon_.reset(); // ★ 追加
    particleSystem_.reset(); // ★ 追加
}

void GamePlayScene::Update() {
    // --- 0. 先に更新が必要なもの ---
    CameraManager::GetInstance()->Update();
    if (inputManager_->IsKeyTriggered(DIK_P)) {
        audioPlayer_->Play(bgmHandle_, true);
    }
    if (inputManager_->IsKeyTriggered(DIK_S)) {
        audioPlayer_->Stop(bgmHandle_);
    }

    // --- ImGui ---
    ImGui::Begin("Object Settings");
    static int selected = 1;
    const char* items[] = { "Plane", "Player (Teapot)", "Enemy (Bunny)","fence" };
    ImGui::Combo("View Select", &selected, items, IM_ARRAYSIZE(items));
    if (selected < objects_.size()) {
        Object3d* currentObject = objects_[selected].get();
        Object3d::Transform* transform = currentObject->GetTransform();
        Model::Material* material = currentObject->GetMaterial();
        Object3d::DirectionalLight* light = currentObject->GetDirectionalLight();
        if (ImGui::CollapsingHeader("Object Transform")) {
            ImGui::DragFloat3("Translate", &transform->translate.x, 0.01f);
            ImGui::DragFloat3("Rotate", &transform->rotate.x, 0.01f);
            ImGui::DragFloat3("Scale", &transform->scale.x, 0.01f);
        }
        if (ImGui::CollapsingHeader("Lighting")) {
            ImGui::ColorEdit4("Light Color", &light->color.x);
            ImGui::DragFloat3("Light Direction", &light->direction.x, 0.01f);
            ImGui::DragFloat("Light Intensity", &light->intensity, 0.01f);
        }
        if (material && ImGui::CollapsingHeader("Material")) {
            ImGui::ColorEdit4("Color", &material->color.x);
            const char* lightingTypes[] = { "None", "Lambertian", "Half Lambert" };
            ImGui::Combo("Lighting Type", &material->selectedLighting, lightingTypes, IM_ARRAYSIZE(lightingTypes));
        }
    }
    ImGui::End();

    ImGui::Begin("Scene Control");
    ImGui::Checkbox("Draw Particles", &isDrawParticles_);
    if (isDrawParticles_) {
        if (inputManager_->IsMouseButtonTriggered(0)) {
            OutputDebugStringA("Spawn Particles Triggered!\n");
            particleSystem_->SpawnParticles({ 0.0f, 0.1f, 0.0f }, 10);
        }
    }
    ImGui::End();

    if (isDrawParticles_) {
        particleSystem_->Update();
    }

    // ▼▼▼ ★★★ 処理順序を根本的に修正 ★★★ ▼▼▼

    if (isDrawParticles_) {
        particleSystem_->Update();
    } else {
        // --- 1. ゲームロジックの更新 (移動) ---
        // (Playerはここで壁にめり込む)
        for (auto& obj : objects_) {
            obj->Update(); // ★ obj->Update() から変更
        }
        for (auto& sprite : sprites_) {
            sprite->Update();
        }
    }

    // --- 2. 物理演算の更新 (衝突判定と押し戻し) ---
    // (めり込んだPlayerが正しい位置に戻される)
    CollisionManager::GetInstance()->Update();

    // --- 3. 行列の更新 (描画準備) ---
    // (押し戻された「後」の正しい座標で、全オブジェクトの行列を計算し直す)
    if (!isDrawParticles_) {
        for (auto& obj : objects_) {
            obj->UpdateMatrix(); // ★ このループを丸ごと追加
        }
    }

}

void GamePlayScene::Draw() {

    if (isDrawParticles_) {
        // --- パーティクル描画モード ---
        particleSystem_->Draw();

    } else {
        // --- 通常描画モード ---

        // 3Dオブジェクトの描画
        object3dCommon_->SetGraphicsCommand();
        for (auto& obj : objects_) {
            obj->Draw();
        }

        // スプライトの描画
        spriteCommon_->SetPipeline(dxCommon_->GetCommandList()); // ★ 複数スプライト対応
        for (auto& sprite : sprites_) { // ★ 複数スプライト対応
            sprite->Draw();
        }
    }
}