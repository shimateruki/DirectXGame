#define NOMINMAX
#include "GamePlayScene.h"
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
#include <EventManager.h>
#include "SceneManager.h"
#include "DebugConsole.h"
#include <cassert>
#include "BulletManager.h"
#include "MoveStrategy3D.h"
#include "MoveStrategy2D.h"
#include "LevelLoader.h"
#include "LockOnSystem.h"
#include "GameRule.h"
#include "ObjectManager.h" // 追加

#ifdef _DEBUG
#include "ParticleEditor.h"
#endif

// --- JSON (保存機能) ---
#include <fstream>
#include <string>
#include "json.hpp"
#include <numbers>
#include <CameraEditor.h>
#include <BaseEnemy.h>
#include <EnemyFactory.h>
#include <EnemySpawner.h>
#include <LightEditor.h>
#include <ParticleManager.h>

GamePlayScene::GamePlayScene() {}
GamePlayScene::~GamePlayScene() {}

void GamePlayScene::Initialize() {
	using json = nlohmann::json;

	// --- 1. エンジン基盤・リソース初期化 ---
	dxCommon_ = DirectXCommon::GetInstance();
	inputManager_ = InputManager::GetInstance();
	audioPlayer_ = AudioPlayer::GetInstance();

	LOG("Game Initialized!");

	ModelManager::GetInstance()->LoadModel("player");
	ModelManager::GetInstance()->LoadModel("teapot");
	ModelManager::GetInstance()->LoadModel("multiMaterial");
	ModelManager::GetInstance()->LoadModel("sampleBlock.gltf");
	ModelManager::GetInstance()->LoadModel("saka");
	ModelManager::GetInstance()->LoadModel("zimen.gltf");
	ModelManager::GetInstance()->LoadModel("a.gltf");
	ModelManager::GetInstance()->LoadModel("sphere.gltf");
	ModelManager::GetInstance()->LoadModel("skydome");
	ModelManager::GetInstance()->LoadModel("sample");
	ModelManager::GetInstance()->LoadModel("terrain");
	ModelManager::GetInstance()->LoadModel("sampleRun.gltf");

	bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

	// --- 2. 各種マネージャ初期化 ---
	CameraManager::GetInstance()->Initialize();
	CameraManager::GetInstance()->SetInputManager(inputManager_);

	spriteCommon_ = std::make_unique<SpriteCommon>();
	spriteCommon_->Initialize(dxCommon_);

	object3dCommon_ = std::make_unique<Object3dCommon>();
	object3dCommon_->Initialize(dxCommon_);

	particleCommon_ = std::make_unique<ParticleCommon>();
	particleCommon_->Initialize(dxCommon_);

	particleSystem_ = std::make_unique<ParticleSystem>();
	particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/white.png");

	ParticleManager::GetInstance()->Initialize(particleSystem_.get());

	gameRule_ = std::make_unique<GameRule>();
	gameRule_->Initialize(this);

	LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

	// --- 3. サブシステム初期化 ---
	objectManager_ = std::make_unique<ObjectManager>();

	lockOnSystem_ = std::make_unique<LockOnSystem>();
	lockOnSystem_->Initialize(inputManager_);

	BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());



	// Enemy
	std::unique_ptr<BaseEnemy> newEnemy = EnemyFactory::GetInstance()->CreateEnemy("Slime", object3dCommon_.get());
	if (newEnemy) {
		newEnemy->SetTranslate({ 10.0f, 0.0f, 10.0f });
		newEnemy->SetTarget(player_);
		objectManager_->AddObject(std::move(newEnemy));
	}

	// --- 5. レベルデータ読み込み (JSON) ---
	levelLoader_ = std::make_unique<LevelLoader>();
	levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/scene_layout.json");
	levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/sprite_layout.json");

	LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
	CameraEditor::GetInstance()->Initialize();
	CameraEditor::GetInstance()->LoadFile("game_camera.json");



	dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Finalize() {
	CollisionManager::GetInstance()->ClearObjects();
	BulletManager::GetInstance()->Finalize();
	particleSystem_.reset();
	particleCommon_.reset();
	sprites_.clear();
	spriteCommon_.reset();
	object3dCommon_.reset();
	objectManager_.reset();
	lockOnSystem_.reset();
}

void GamePlayScene::Update(float deltaTime) {
	static Math math;
	LightEditor::GetInstance()->Update();

	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

	// --- ロックオン & カメラ制御 ---
	lockOnSystem_->Update(objectManager_->GetObjects(), camera, player_);
	CameraEditor::GetInstance()->Update(player_, lockOnSystem_->IsLockingOn());

	// 自由カメラモード以外の操作
	if (!CameraEditor::GetInstance()->IsEditorMode()) {
		Camera::FollowMode currentMode = camera->GetFollowMode();

		// 右クリック回転
		if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
			if (inputManager_->IsMouseButtonPressed(1)) {
				Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
				if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
					camera->AddRotation(mouseDelta);
				}
			}
		}
	}

	// --- 全体更新 ---
	CameraManager::GetInstance()->Update();
	particleSystem_->Update(deltaTime);
	objectManager_->Update(deltaTime); // オブジェクト一括更新

	for (auto& sprite : sprites_) {
		sprite->Update();
	}

	BulletManager::GetInstance()->Update(deltaTime);
	CollisionManager::GetInstance()->Update();
}


void GamePlayScene::Draw() {
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
	if (debugEditor_) debugEditor_->DrawPreview(pointLightResource_.Get(), spotLightResource_.Get());
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
void GamePlayScene::DrawUI() {
	// --- 4. 2D描画 (UIスプライト) ---
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
	for (auto& sprite : sprites_) {
		sprite->Draw();
	}
}