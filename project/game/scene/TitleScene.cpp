#define NOMINMAX
#include "TitleScene.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include "AudioPlayer.h"
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "ParticleSystem.h"
#include "imgui.h"
#include "LightManager.h"
#include "SceneManager.h"
#include "DebugConsole.h"
#include "BulletManager.h"
#include "LevelLoader.h"
#include "GameRule.h"
#include "CameraEditor.h"
#include "LightEditor.h"

void TitleScene::Initialize() {
    // --- 1. システム基盤の取得 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    // --- 2. モデルのプリロード ---
    ModelManager::GetInstance()->LoadModel("player");
    ModelManager::GetInstance()->LoadModel("teapot");
    LOG("TitleScene Initialized!");

    bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

    // --- 3. マネージャ・共通クラスの初期化 ---
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);

    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/white.png");

    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

    // --- 4. サブシステムの生成 ---
    objectManager_ = std::make_unique<ObjectManager>();
    levelLoader_ = std::make_unique<LevelLoader>();
    gameRule_ = std::make_unique<GameRule>();
    gameRule_->Initialize(this);

    BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

    // --- 5. 固定スプライトの生成 (コードベースの生成) ---
    uint32_t monsterBallHandle = Sprite::LoadTexture("monsterBall.png");
    auto monsterBallSprite = std::make_unique<Sprite>();
    monsterBallSprite->Initialize(spriteCommon_.get(), monsterBallHandle);
    monsterBallSprite->SetPosition({ 200.0f, 360.0f });
    monsterBallSprite->SetSize({ 100.0f, 100.0f });
    monsterBallSprite->SetName("MonsterBall");
    sprites_.push_back(std::move(monsterBallSprite));

    // --- 6. レイアウトの読み込み (LevelLoaderへ委譲) ---
    levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/titleScene.json");
    levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/titleScene.json");

    LightManager::GetInstance()->LoadState("Resources/json/light/titleScene.json");
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile("title_camera.json");

    dxCommon_->FlushCommandQueue(false);
}

void TitleScene::Finalize() {
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();

    objectManager_.reset();
    sprites_.clear();
    particleSystem_.reset();
    particleCommon_.reset();
    spriteCommon_.reset();
    object3dCommon_.reset();
}

void TitleScene::Update(float deltaTime) {
    // 常に実行されるマネージャ更新
    LightEditor::GetInstance()->Update();
    CameraManager::GetInstance()->Update();
    CameraEditor::GetInstance()->Update(player_, false);

    // オブジェクト一括更新 (ObjectManagerに委譲)
    objectManager_->Update(deltaTime);
    particleSystem_->Update(deltaTime);

    for (auto& sprite : sprites_) {
        sprite->Update();
    }

    // 各種グローバル更新
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
}

void TitleScene::Draw() {
    // --- 一人称視点判定 ---
    bool isFirstPerson = false;
#ifndef _DEBUG
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
        isFirstPerson = true;
    }
#endif

    ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();
    object3dCommon_->SetGraphicsCommand();

    auto& objects = objectManager_->GetObjects();

    // --- 1. 不透明描画 ---
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_) continue;
        if (obj->GetMaterialType() == 1) continue; // 透明はスキップ
        obj->Draw(pointLightRes, spotLightRes);
    }

    // --- 2. 中間描画 (弾・デバッグ) ---
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    //if (debugEditor_) debugEditor_->DrawPreview(pointLightResource_.Get(), spotLightResource_.Get());
    LightEditor::GetInstance()->Draw3D();

    // --- 3. 透明描画 ---
    for (auto& obj : objects) {
        if (isFirstPerson && obj.get() == player_) continue;
        if (obj->GetMaterialType() == 1) { // 透明のみ描画
            obj->Draw(pointLightRes, spotLightRes);
        }
    }
    particleSystem_->Draw();
}

// ====================================================================
// UI描画専用の関数
// ====================================================================
void TitleScene::DrawUI() {
    // --- 4. 2D描画 (UIスプライト) ---
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    for (auto& sprite : sprites_) {
        sprite->Draw();
    }
}