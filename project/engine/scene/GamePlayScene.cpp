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

#include "externals/imgui/imgui.h"

void GamePlayScene::Initialize() {
    // 基盤クラスのポインタを保持
    // ★★★ 各マネージャのインスタンスをシングルトン経由で取得 ★★★
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    // 以下、Gameクラスから移動してきた初期化処理
    bgmHandle_ = audioPlayer_->LoadSoundFile("resouces/Alarm02.mp3");

    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    // --- モデルの読み込み ---
    ModelManager::GetInstance()->LoadModel("resouces/plane.obj");
    ModelManager::GetInstance()->LoadModel("resouces/teapot.obj");
    ModelManager::GetInstance()->LoadModel("resouces/bunny.obj");

    // --- オブジェクトの生成 ---
    objects_.emplace_back(std::make_unique<Object3d>()); // Plane
    objects_.emplace_back(std::make_unique<Object3d>()); // Teapot
    objects_.emplace_back(std::make_unique<Object3d>()); // Bunny

    objects_[0]->Initialize(object3dCommon_.get());
    objects_[0]->SetModel("resouces/plane.obj");

    objects_[1]->Initialize(object3dCommon_.get());
    objects_[1]->SetModel("resouces/teapot.obj");
    objects_[1]->SetTranslate({ 2.0f, 0.0f, 0.0f });

    objects_[2]->Initialize(object3dCommon_.get());
    objects_[2]->SetModel("resouces/bunny.obj");
    objects_[2]->SetTranslate({ -2.0f, 0.0f, 0.0f });

    // --- スプライトの生成 ---
    sprite_ = std::make_unique<Sprite>();
    uint32_t spriteTexHandle = TextureManager::GetInstance()->Load("resouces/monsterBall.png");
    sprite_->Initialize(spriteCommon_.get(), spriteTexHandle);
    sprite_->SetPosition({ 200.0f, 360.0f });
    sprite_->SetSize({ 100.0f, 100.0f });

    dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Finalize() {
    // Gameクラスから移動してきた終了処理
    objects_.clear();
    sprite_.reset();
    object3dCommon_.reset();
    spriteCommon_.reset();
}

void GamePlayScene::Update() {
    CameraManager::GetInstance()->Update();

    if (inputManager_->IsKeyTriggered(DIK_P)) {
        audioPlayer_->Play(bgmHandle_, true);
    }
    if (inputManager_->IsKeyTriggered(DIK_S)) {
        audioPlayer_->Stop(bgmHandle_);
    }

    // --- ImGui ---
    ImGui::Begin("Object Settings");
    static int selected = 0;
    const char* items[] = { "Plane", "Teapot", "Bunny" };
    ImGui::Combo("View Select", &selected, items, IM_ARRAYSIZE(items));
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
    ImGui::End();

    // --- オブジェクト更新 ---
    for (auto& obj : objects_) {
        // CameraManagerの導入により、Updateの引数は不要になっています
        obj->Update();
    }
    sprite_->Update();
}

void GamePlayScene::Draw() {
    // 3Dオブジェクトの描画
    object3dCommon_->SetGraphicsCommand();
    for (auto& obj : objects_) {
        // ★★★ 引数が不要に ★★★
        obj->Draw();
    }

    // スプライトの描画
    sprite_->Draw();
}