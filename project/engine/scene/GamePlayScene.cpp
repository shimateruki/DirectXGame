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
#include "engine/3d/CollisionManager.h" 
#include "externals/imgui/imgui.h"
#include "Player.h"

void GamePlayScene::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    bgmHandle_ = audioPlayer_->LoadSoundFile("resouces/bgm/Alarm02.mp3");

    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    // --- オブジェクトの生成 ---
    objects_.emplace_back(std::make_unique<Object3d>()); // Plane
    objects_[0]->Initialize(object3dCommon_.get());
    objects_[0]->SetModel("plane");

    // Player (Teapot) の生成
    auto player = std::make_unique<Player>();
    player->Initialize(object3dCommon_.get());
    player->SetModel("teapot");
    player->SetTranslate({ 2.0f, 0.0f, 0.0f });
    objects_.emplace_back(std::move(player));

    // Enemy (Bunny) の生成
    auto enemy = std::make_unique<Object3d>();
    enemy->Initialize(object3dCommon_.get());
    enemy->SetModel("bunny");
    enemy->SetTranslate({ -2.0f, 0.0f, 0.0f });
    objects_.emplace_back(std::move(enemy));

    objects_.emplace_back(std::make_unique<Object3d>()); // fence
    objects_[3]->Initialize(object3dCommon_.get());
    objects_[3]->SetModel("fence");
    objects_[3]->SetTranslate({ 0.0f, 0.0f, 5.0f });

    // --- カメラの追尾設定 ---
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    Object3d::Transform* targetTransform = objects_[1]->GetTransform(); // Playerを追尾
    camera->SetTarget(&targetTransform->translate);

    // --- 衝突判定の設定 ---
    CollisionManager::GetInstance()->ClearObjects();
    // プレイヤーを登録
    objects_[1]->SetCollisionAttribute(kPlayer);
    objects_[1]->SetCollisionMask(~kPlayer); // プレイヤー自身とは当たらない
    objects_[1]->SetCollisionRadius(1.0f);
    CollisionManager::GetInstance()->AddObject(objects_[1].get());
    // 敵を登録
    objects_[2]->SetCollisionAttribute(kEnemy);
    objects_[2]->SetCollisionMask(~kEnemy); // 敵自身とは当たらない
    objects_[2]->SetCollisionRadius(1.0f);
    CollisionManager::GetInstance()->AddObject(objects_[2].get());

    // --- スプライトの生成 ---
    // (以前のままなので省略)
    uint32_t monsterBallHandle = Sprite::LoadTexture("monsterBall");
    auto monsterBallSprite = std::make_unique<Sprite>();
    monsterBallSprite->Initialize(spriteCommon_.get(), monsterBallHandle);
    monsterBallSprite->SetPosition({ 200.0f, 360.0f });
    monsterBallSprite->SetSize({ 100.0f, 100.0f });
    sprites_.push_back(std::move(monsterBallSprite));
    uint32_t flameHandle = Sprite::LoadTexture("sample");
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

    dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Update() {
    CameraManager::GetInstance()->Update();
    CollisionManager::GetInstance()->Update(); // ★ 衝突判定の更新を追加

    if (inputManager_->IsKeyTriggered(DIK_P)) {
        audioPlayer_->Play(bgmHandle_, true);
    }
    if (inputManager_->IsKeyTriggered(DIK_S)) {
        audioPlayer_->Stop(bgmHandle_);
    }

    // --- ImGui ---
    ImGui::Begin("Object Settings");
    static int selected = 1; // デフォルトでPlayer(Teapot)を選択
    const char* items[] = { "Plane", "Player (Teapot)", "Enemy (Bunny)","fence"};
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

    ImGui::Begin("Scene Control");
    ImGui::Checkbox("Draw Particles", &isDrawParticles_);
    if (isDrawParticles_ && inputManager_->IsMouseButtonTriggered(0)) {
        particleSystem_->SpawnParticles({ 0.0f, 0.1f, 0.0f }, 10);
    }
    ImGui::End();

    if (isDrawParticles_) {
        particleSystem_->Update();
    } else {
        // オブジェクトとスプライトの更新
        for (auto& obj : objects_) { obj->Update(); }
        for (auto& sprite : sprites_) { sprite->Update(); }
    }
}

// Draw() と Finalize() は以前の複数スプライト対応版から変更ありません
void GamePlayScene::Finalize() {
    objects_.clear();
    sprites_.clear();
    object3dCommon_.reset();
    spriteCommon_.reset();
}

void GamePlayScene::Draw() {
    if (isDrawParticles_) {
        particleSystem_->Draw();
    } else {
        object3dCommon_->SetGraphicsCommand();
        for (auto& obj : objects_) {
            obj->Draw();
        }
        spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
        for (auto& sprite : sprites_) {
            sprite->Draw();
        }
    }
}