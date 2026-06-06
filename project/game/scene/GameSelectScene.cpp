#define NOMINMAX
#include "GameSelectScene.h"
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
#include "ObjectManager.h" 
#include "BossCore.h"
#include"MeshEffectManager.h"
#include"WinApp.h"
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
#include <GPUParticleManager.h>
#include <SrvManager.h>
#include <PostEffect.h>
#include "StageManager.h"
#include "GameDataManager.h"
#include "GimmickStageGate.h"
#include "CollisionConfig.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace {
constexpr float kUnlockPresentationDuration = 2.6f;
constexpr uint32_t kStageSelectSolidMask = 0xFFFFFFFFu;

Vector4 GetStageIslandColor(int stageIndex, bool unlocked, bool selected, bool unlocking, float pulse) {
	if (!unlocked && !unlocking) {
		return { 0.16f, 0.17f, 0.20f, 0.78f };
	}
	if (unlocking) {
		return { 0.88f, 0.66f + pulse * 0.18f, 0.28f, 1.0f };
	}
	if (selected) {
		switch (stageIndex) {
		case 0:
			return { 0.56f, 0.70f, 0.50f, 1.0f };
		case 1:
			return { 0.48f, 0.58f, 0.72f, 1.0f };
		case 2:
			return { 0.62f, 0.52f, 0.72f, 1.0f };
		default:
			return { 0.54f, 0.64f, 0.58f, 1.0f };
		}
	}
	switch (stageIndex) {
	case 0:
		return { 0.46f, 0.60f, 0.44f, 1.0f };
	case 1:
		return { 0.38f, 0.48f, 0.62f, 1.0f };
	case 2:
		return { 0.52f, 0.44f, 0.64f, 1.0f };
	default:
		return { 0.40f, 0.52f, 0.48f, 1.0f };
	}
}

float GetStageIslandEmissive(bool unlocked, bool selected, bool unlocking, float pulse) {
	if (unlocking) {
		return 1.8f + pulse * 1.2f;
	}
	if (!unlocked) {
		return 0.18f;
	}
	return selected ? 1.05f + pulse * 0.45f : 0.72f;
}
}

GameSelectScene::GameSelectScene() {}
GameSelectScene::~GameSelectScene() {}

void GameSelectScene::Initialize() {
	using json = nlohmann::json;

	// --- 1. エンジン基盤・リソース初期化 ---
	dxCommon_ = DirectXCommon::GetInstance();
	inputManager_ = InputManager::GetInstance();
	audioPlayer_ = AudioPlayer::GetInstance();


	LOG("Game Select Initialized!");

	StageManager::GetInstance()->Initialize();
	selectedStageIndex_ = StageManager::GetInstance()->GetCurrentStageIndex();
	if (selectedStageIndex_ < 0 || selectedStageIndex_ >= static_cast<int>(StageManager::GetInstance()->GetStages().size())) {
		selectedStageIndex_ = 0;
	}
	if (GameDataManager::GetInstance()->IsStageCleared(selectedStageIndex_) &&
		IsStageUnlocked(selectedStageIndex_ + 1)) {
		selectedStageIndex_++;
	}
	previousSelectedStageIndex_ = -1;
	stageDecisionCooldown_ = 0.0f;
	isChangingStage_ = false;
	unlockingStageIndex_ = -1;
	unlockPresentationTimer_ = 0.0f;
	unlockParticleTimer_ = 0.0f;
	stageSelectTime_ = 0.0f;

	bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

	// --- 2. 各種マネージャ初期化 ---
	EventManager::GetInstance()->ClearAllListeners();
	CameraManager::GetInstance()->Initialize();
	CameraManager::GetInstance()->SetInputManager(inputManager_);

	spriteCommon_ = std::make_unique<SpriteCommon>();
	spriteCommon_->Initialize(dxCommon_);

	object3dCommon_ = std::make_unique<Object3dCommon>();
	object3dCommon_->Initialize(dxCommon_);

	particleCommon_ = std::make_unique<ParticleCommon>();
	particleCommon_->Initialize(dxCommon_);

	particleSystem_ = std::make_unique<ParticleSystem>();
	particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/circle2.png");

	ParticleManager::GetInstance()->Initialize(particleSystem_.get());

	gameRule_ = std::make_unique<GameRule>();
	gameRule_->Initialize(this);

	LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

	// --- 3. サブシステム初期化 ---
	objectManager_ = std::make_unique<ObjectManager>();

	lockOnSystem_ = std::make_unique<LockOnSystem>();
	lockOnSystem_->Initialize(inputManager_);
	uint32_t lockOnTex = TextureManager::GetInstance()->Load("Resources/sprite/lockOn.png"); 
	lockOnSprite_ = std::make_unique<Sprite>();
	lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
	lockOnSprite_->SetAnchorPoint({ 0.5f, 0.5f }); // 画像の中心を基準にする
	lockOnSprite_->SetSize({ 64.0f, 64.0f });      // アイコンのサイズ（適宜調整！）
	BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

	GPUParticleManager::GetInstance()->Initialize(dxCommon_);
	GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
	// パーティクルで使う画像を読み込み、ハンドル(番号)を保存しておく
	gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/white.png");

	// 1. キューブマップ（DDS）の読み込み
	skyboxTextureHandle_ = TextureManager::GetInstance()->Load("Resources/output_skybox.dds");

	// 2. スカイボックスの生成と初期化
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);

	// --- 5. レベルデータ読み込み (JSON) ---
	levelLoader_ = std::make_unique<LevelLoader>();
	// セレクトシーン用のレイアウトがあればそれを読み込むが、一旦 bossStage.json で代用
	levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/stageSelect.json");
	levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/stageSelect_sprite.json");
	LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
	CameraEditor::GetInstance()->Initialize();
	CameraEditor::GetInstance()->LoadFile("game_camera.json");

	GameDataManager::GetInstance()->MarkStageUnlockSeen(0);
	unlockingStageIndex_ = FindPendingUnlockStage();
	if (unlockingStageIndex_ >= 0) {
		selectedStageIndex_ = unlockingStageIndex_;
		DebugConsole::GetInstance()->AddLog("Stage Select: crown unlock presentation start.");
	}
	ApplyStageGateStates();
	UpdateStageSelectDecorations(0.0f);
    
	dxCommon_->FlushCommandQueue(false);
}

void GameSelectScene::Finalize() {
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

void GameSelectScene::Update(float deltaTime) {

	// --- ポストエフェクト更新 ---
	PostEffect::GetInstance()->Update(deltaTime);
	Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
	if (activeCamera) {
		PostEffect::GetInstance()->GetParams()->projectionInverse = Math::Inverse(activeCamera->GetProjectionMatrix());
	}

	static Math math;
	LightEditor::GetInstance()->Update();

	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

	// --- ロックオン & カメラ制御 ---
	lockOnSystem_->Update(objectManager_->GetObjects(), camera, player_);
	CameraEditor::GetInstance()->Update(player_, lockOnSystem_->IsLockingOn());

	// ロックオンアイコン更新
	Object3d* target = lockOnSystem_->GetTarget();
	if (target && lockOnSystem_->IsLockingOn()) {
		isDrawLockOn_ = true;
		AABB aabb = target->GetAABB();
		Vector3 targetCenter = (aabb.min + aabb.max) * 0.5f;
		Matrix4x4 viewProj = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
		float w = targetCenter.x * viewProj.m[0][3] + targetCenter.y * viewProj.m[1][3] + targetCenter.z * viewProj.m[2][3] + viewProj.m[3][3];

		if (w > 0.001f) {
			Vector3 ndc;
			ndc.x = (targetCenter.x * viewProj.m[0][0] + targetCenter.y * viewProj.m[1][0] + targetCenter.z * viewProj.m[2][0] + viewProj.m[3][0]) / w;
			ndc.y = (targetCenter.x * viewProj.m[0][1] + targetCenter.y * viewProj.m[1][1] + targetCenter.z * viewProj.m[2][1] + viewProj.m[3][1]) / w;

			float screenWidth = static_cast<float>(::WinApp::kClientWidth);
			float screenHeight = static_cast<float>(::WinApp::kClientHeight);
			float screenX = (ndc.x + 1.0f) * 0.5f * screenWidth;
			float screenY = (1.0f - ndc.y) * 0.5f * screenHeight;

			lockOnSprite_->SetPosition({ screenX, screenY });

			float objSizeX = aabb.max.x - aabb.min.x;
			float objSizeY = aabb.max.y - aabb.min.y;
			float objSizeZ = aabb.max.z - aabb.min.z;
			float maxObjSize = std::max({ objSizeX, objSizeY, objSizeZ });

			float baseSize = maxObjSize * 25.0f;
			float distanceScale = 20.0f / w;
			float finalSize = std::max(32.0f, std::min(baseSize * distanceScale, 256.0f));

			lockOnSprite_->SetSize({ finalSize, finalSize });
			lockOnSprite_->SetRotation(lockOnSprite_->GetRotation() + 2.0f * deltaTime);
			lockOnSprite_->Update();
		}
		else {
			isDrawLockOn_ = false;
		}
	}
	else {
		isDrawLockOn_ = false;
	}

	// カメラ操作
	if (!CameraEditor::GetInstance()->IsEditorMode()) {
		Camera::FollowMode currentMode = camera->GetFollowMode();
		if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
			Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
#ifdef USE_IMGUI
			if (inputManager_->IsMouseButtonPressed(1)) {
				if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) camera->AddRotation(mouseDelta);
			}
#else
			if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) camera->AddRotation(mouseDelta);
#endif
		}
	}

	// --- 全体更新 ---
	CameraManager::GetInstance()->Update();
	particleSystem_->Update(deltaTime);
	UpdateStageGateSelection(deltaTime);
	objectManager_->Update(deltaTime);
	GPUParticleManager::GetInstance()->Update(deltaTime);
	for (auto& sprite : sprites_) sprite->Update();
	BulletManager::GetInstance()->Update(deltaTime);
	CollisionManager::GetInstance()->Update();
	UpdateUI();

	if (animatedCube_) animatedCube_->Update(deltaTime);
}

void GameSelectScene::Draw() {
	bool isFirstPerson = false;
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

	// 一人称判定
	if (player_ && camera) {
		Vector3 pPos = player_->GetWorldPosition();
		pPos.y += 1.0f;
		Vector3 cPos = camera->GetEye();
		Vector3 toCam = { cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z };
		float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);
		if (dist < 3.0f) isFirstPerson = true;
	}

	ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
	ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();
	object3dCommon_->SetGraphicsCommand();

	auto& objects = objectManager_->GetObjects();

	// --- 1. 不透明描画 ---
	for (auto& obj : objects) {
		if (!obj->GetIsVisible()) continue;
		bool isPlayerPart = false;
		if (isFirstPerson) {
			Object3d* current = obj.get();
			while (current) {
				if (current == player_) { isPlayerPart = true; break; }
				current = current->GetParent();
			}
		}
		if (isPlayerPart) continue;
		if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || obj->GetMaterialType() >= 8) continue;
		obj->Draw(pointLightRes, spotLightRes);
	}

	if (animatedCube_) animatedCube_->Draw(pointLightRes, spotLightRes);

	// --- 2. 中間描画 ---
	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	LightEditor::GetInstance()->Draw3D();
	if (skybox_) skybox_->Draw(camera->GetConstantBuffer());

	// --- 3. 透明描画 ---
	for (auto& obj : objects) {
		if (!obj->GetIsVisible()) continue;
		bool isPlayerPart = false;
		if (isFirstPerson) {
			Object3d* current = obj.get();
			while (current) {
				if (current == player_) { isPlayerPart = true; break; }
				current = current->GetParent();
			}
		}
		if (isPlayerPart) continue;
		if (obj->GetMaterialType() == 1) obj->Draw(pointLightRes, spotLightRes);
	}
	particleSystem_->Draw();

	// 4. ローカルフォグ
	bool hasFog = false;
	for (auto& obj : objects) if (obj->GetIsVisible() && obj->GetMaterialType() == 7) hasFog = true;
	if (hasFog) {
		dxCommon_->PreDrawLocalFog();
		for (auto& obj : objects) if (obj->GetIsVisible() && obj->GetMaterialType() == 7) obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
		dxCommon_->PostDrawLocalFog();
	}

	// 5. 流体描画
	bool hasFluid = false;
	for (auto& obj : objects) if (obj->GetIsVisible() && obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 15) hasFluid = true;
	if (hasFluid) {
		dxCommon_->UpdateGrabTexture();
		for (auto& obj : objects) {
			if (!obj->GetIsVisible()) continue;
			int matType = obj->GetMaterialType();
			if (matType == 8) obj->DrawWater(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			else if (matType == 9) obj->DrawMagma(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			else if (matType == 10) obj->DrawIce(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			else if (matType == 11) obj->DrawFire(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			else if (matType == 12) obj->DrawLaser(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			else if (matType == 13) obj->DrawSlimeGel(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			else if (matType == 14) obj->DrawShockwave(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			else if (matType == 15) obj->DrawLiquidContact(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
		}
	}

	// 6. GPUパーティクル
	dxCommon_->UpdateGrabTexture();
	dxCommon_->PreDrawLocalFog();
	GPUParticleManager::GetInstance()->Draw(dxCommon_->GetCommandList(), camera->GetViewMatrix(), camera->GetProjectionMatrix(), gpuParticleTexHandle_, dxCommon_->GetDepthSrvHandle());
	dxCommon_->PostDrawLocalFog();
}

void GameSelectScene::DrawUI() {
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
	for (auto& sprite : sprites_) sprite->Draw();
	if (isDrawLockOn_ && lockOnSprite_) lockOnSprite_->Draw();
}

void GameSelectScene::DrawShadow() {
	if (objectManager_) objectManager_->DrawShadow();
}

void GameSelectScene::UpdateUI() {}

void GameSelectScene::UpdateStageGateSelection(float deltaTime) {
	stageSelectTime_ += deltaTime;

	if (stageDecisionCooldown_ > 0.0f) {
		stageDecisionCooldown_ -= deltaTime;
		if (stageDecisionCooldown_ < 0.0f) stageDecisionCooldown_ = 0.0f;
	}

	UpdateUnlockPresentation(deltaTime);

	if (unlockingStageIndex_ >= 0) {
		ApplyStageGateStates();
		UpdateStageSelectDecorations(deltaTime);
		return;
	}

	float nearestDistance = std::numeric_limits<float>::max();
	GimmickStageGate* nearestGate = FindNearestStageGate(&nearestDistance);
	if (nearestGate && nearestDistance <= gateSelectRadius_) {
		int gateStageIndex = nearestGate->GetStageIndex();
		if (gateStageIndex >= 0 && gateStageIndex < static_cast<int>(StageManager::GetInstance()->GetStages().size())) {
			selectedStageIndex_ = gateStageIndex;
		}
	}

	ApplyStageGateStates();
	UpdateStageSelectDecorations(deltaTime);

	if (selectedStageIndex_ != previousSelectedStageIndex_) {
		const auto& stages = StageManager::GetInstance()->GetStages();
		if (selectedStageIndex_ >= 0 && selectedStageIndex_ < static_cast<int>(stages.size())) {
			const StageData& stage = stages[selectedStageIndex_];
			DebugConsole::GetInstance()->AddLog("Stage Select: " + stage.name);
		}
		previousSelectedStageIndex_ = selectedStageIndex_;
	}

	bool decide =
		inputManager_->IsKeyTriggered(DIK_SPACE) ||
		inputManager_->IsKeyTriggered(DIK_RETURN) ||
		inputManager_->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A);

	if (decide) {
		EnterSelectedStage();
	}
}

void GameSelectScene::ApplyStageGateStates() {
	if (!objectManager_) {
		return;
	}

	for (auto& object : objectManager_->GetObjects()) {
		auto* gate = dynamic_cast<GimmickStageGate*>(object.get());
		if (!gate) {
			continue;
		}

		int stageIndex = gate->GetStageIndex();
		bool unlocked = IsStageUnlocked(stageIndex);
		bool cleared = GameDataManager::GetInstance()->IsStageCleared(stageIndex);
		bool unlocking = stageIndex == unlockingStageIndex_;
		gate->SetGateState(stageIndex == selectedStageIndex_, unlocked, cleared, unlocking);
	}
}

bool GameSelectScene::IsStageUnlocked(int stageIndex) const {
	const auto& stages = StageManager::GetInstance()->GetStages();
	if (stageIndex < 0 || stageIndex >= static_cast<int>(stages.size())) {
		return false;
	}

	const StageData& stage = stages[stageIndex];
	if (stage.defaultUnlocked || stageIndex == 0) {
		return true;
	}
	if (stage.unlockStageIndex < 0) {
		return false;
	}
	return GameDataManager::GetInstance()->IsStageCleared(stage.unlockStageIndex);
}

int GameSelectScene::FindPendingUnlockStage() const {
	const auto& stages = StageManager::GetInstance()->GetStages();
	auto* save = GameDataManager::GetInstance();

	for (int stageIndex = 1; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
		if (IsStageUnlocked(stageIndex) && !save->IsStageUnlockSeen(stageIndex)) {
			return stageIndex;
		}
	}
	return -1;
}

void GameSelectScene::UpdateUnlockPresentation(float deltaTime) {
	if (unlockingStageIndex_ < 0) {
		return;
	}

	unlockPresentationTimer_ += deltaTime;
	unlockParticleTimer_ -= deltaTime;
	selectedStageIndex_ = unlockingStageIndex_;

	Object3d* crown = FindObjectByName("StageSelect_CrownCore");
	if (crown) {
		Transform* transform = crown->GetTransform();
		transform->rotate.y += deltaTime * 1.8f;
		transform->isQuaternionMaster = false;
		crown->SetColor({ 1.0f, 0.78f, 0.18f, 1.0f });
		crown->SetEmissive(3.5f + std::sin(stageSelectTime_ * 9.0f) * 0.8f);
	}

	if (unlockParticleTimer_ <= 0.0f && particleSystem_) {
		unlockParticleTimer_ = 0.08f;
		Vector3 up = { 0.0f, 1.0f, 0.0f };
		Vector3 nodePos = GetStageNodePosition(unlockingStageIndex_);
		if (crown) {
			particleSystem_->SpawnParticles(
				crown->GetWorldPosition() + Vector3{ 0.0f, 1.6f, 0.0f },
				6,
				5.5f,
				&up,
				1.1f,
				{ 1.0f, 0.86f, 0.22f, 1.0f },
				{ 1.0f, 0.95f, 0.55f, 0.0f },
				0.35f,
				0.75f,
				0.55f,
				0.05f
			);
		}
		particleSystem_->SpawnParticles(
			nodePos + Vector3{ 0.0f, 1.4f, 0.0f },
			5,
			4.2f,
			&up,
			1.0f,
			{ 0.65f, 0.9f, 1.0f, 1.0f },
			{ 1.0f, 0.85f, 0.2f, 0.0f },
			0.3f,
			0.65f,
			0.45f,
			0.04f
		);
	}

	if (unlockPresentationTimer_ >= kUnlockPresentationDuration) {
		GameDataManager::GetInstance()->MarkStageUnlockSeen(unlockingStageIndex_);
		DebugConsole::GetInstance()->AddLog("Stage Select: stage unlocked.");
		unlockingStageIndex_ = -1;
		unlockPresentationTimer_ = 0.0f;
		unlockParticleTimer_ = 0.0f;
	}
}

void GameSelectScene::UpdateStageSelectDecorations(float deltaTime) {
	const auto& stages = StageManager::GetInstance()->GetStages();
	float pulse = (std::sin(stageSelectTime_ * 5.0f) + 1.0f) * 0.5f;

	Object3d* crown = FindObjectByName("StageSelect_CrownCore");
	if (crown && unlockingStageIndex_ < 0) {
		Transform* transform = crown->GetTransform();
		transform->rotate.y += deltaTime * 0.55f;
		transform->isQuaternionMaster = false;
		crown->SetColor({ 1.0f, 0.72f, 0.18f, 1.0f });
		crown->SetEmissive(1.5f + pulse * 0.45f);
	}

	for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
		bool unlocked = IsStageUnlocked(stageIndex);
		bool unlocking = stageIndex == unlockingStageIndex_;
		bool selected = stageIndex == selectedStageIndex_;

		Object3d* island = FindObjectByName("StageIsland_" + std::to_string(stageIndex));
		if (island) {
			island->SetIsVisible(true);
			island->SetCollisionAttribute(unlocked ? kGround : 0);
			island->SetCollisionMask(unlocked ? kStageSelectSolidMask : 0);
			island->SetColor(GetStageIslandColor(stageIndex, unlocked, selected, unlocking, pulse));
			island->SetEmissive(GetStageIslandEmissive(unlocked, selected, unlocking, pulse));
		}

		Object3d* markerPad = FindObjectByName("StageMarkerPad_" + std::to_string(stageIndex));
		if (markerPad) {
			markerPad->SetIsVisible(true);
			if (unlocking) {
				markerPad->SetColor({ 1.0f, 0.82f + pulse * 0.12f, 0.28f, 1.0f });
				markerPad->SetEmissive(2.4f + pulse * 1.1f);
			} else if (unlocked) {
				markerPad->SetColor(selected ? Vector4{ 0.86f, 0.92f, 1.0f, 1.0f } : Vector4{ 0.45f, 0.66f, 0.88f, 1.0f });
				markerPad->SetEmissive(selected ? 1.8f + pulse * 0.45f : 0.95f);
			} else {
				markerPad->SetColor({ 0.22f, 0.24f, 0.30f, 1.0f });
				markerPad->SetEmissive(0.18f);
			}
		}

		Object3d* flag = FindObjectByName("StageFlag_" + std::to_string(stageIndex));
		if (flag) {
			flag->SetIsVisible(true);
			if (unlocking) {
				flag->SetColor({ 1.0f, 0.88f, 0.36f, 1.0f });
				flag->SetEmissive(2.0f + pulse * 1.0f);
			} else if (unlocked) {
				flag->SetColor(selected ? Vector4{ 0.95f, 1.0f, 1.0f, 1.0f } : Vector4{ 0.74f, 0.90f, 1.0f, 1.0f });
				flag->SetEmissive(selected ? 1.65f + pulse * 0.35f : 1.05f);
			} else {
				flag->SetColor({ 0.32f, 0.36f, 0.46f, 1.0f });
				flag->SetEmissive(0.2f);
			}
		}

		UpdatePathDisplay(stageIndex, unlocked, unlocking, pulse);
	}

	UpdateStarCoinDisplays(deltaTime);
}

void GameSelectScene::UpdatePathDisplay(int stageIndex, bool active, bool unlocking, float pulse) {
	for (int segmentIndex = 0; segmentIndex < 12; ++segmentIndex) {
		std::ostringstream name;
		name << "StagePath_" << stageIndex << "_" << segmentIndex;
		Object3d* path = FindObjectByName(name.str());
		if (!path) {
			continue;
		}

		bool visible = active || unlocking;
		path->SetIsVisible(visible);
		path->SetCollisionAttribute(active ? kGround : 0);
		path->SetCollisionMask(active ? kStageSelectSolidMask : 0);

		if (unlocking) {
			path->SetColor({ 1.0f, 0.78f + pulse * 0.16f, 0.28f, 1.0f });
			path->SetEmissive(2.2f + pulse * 1.4f);
		} else if (active) {
			path->SetColor({ 0.66f, 0.84f, 1.0f, 1.0f });
			path->SetEmissive(1.05f);
		} else {
			path->SetColor({ 0.12f, 0.14f, 0.18f, 0.35f });
			path->SetEmissive(0.1f);
		}
	}
}

void GameSelectScene::UpdateStarCoinDisplays(float deltaTime) {
	const auto& stages = StageManager::GetInstance()->GetStages();
	auto* save = GameDataManager::GetInstance();

	for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
		bool unlocked = IsStageUnlocked(stageIndex);
		for (int coinIndex = 0; coinIndex < 3; ++coinIndex) {
			std::ostringstream name;
			name << "StageCoin_" << stageIndex << "_" << coinIndex;
			Object3d* coin = FindObjectByName(name.str());
			if (!coin) {
				continue;
			}

			coin->SetIsVisible(unlocked);
			coin->SetCollisionAttribute(0);
			coin->SetCollisionMask(0);
			if (!unlocked) {
				continue;
			}

			Transform* transform = coin->GetTransform();
			transform->rotate.y += deltaTime * 1.8f;
			transform->isQuaternionMaster = false;

			if (save->IsStarCoinCollected(stageIndex, coinIndex)) {
				coin->SetColor({ 1.0f, 0.85f, 0.12f, 1.0f });
				coin->SetEmissive(2.2f);
			} else {
				coin->SetColor({ 0.32f, 0.34f, 0.38f, 0.78f });
				coin->SetEmissive(0.35f);
			}
		}
	}
}

Object3d* GameSelectScene::FindObjectByName(const std::string& name) const {
	if (!objectManager_) {
		return nullptr;
	}

	for (const auto& object : objectManager_->GetObjects()) {
		if (object && object->GetName() == name) {
			return object.get();
		}
	}
	return nullptr;
}

Vector3 GameSelectScene::GetStageNodePosition(int stageIndex) const {
	if (objectManager_) {
		for (const auto& object : objectManager_->GetObjects()) {
			auto* gate = dynamic_cast<GimmickStageGate*>(object.get());
			if (gate && gate->GetStageIndex() == stageIndex) {
				return gate->GetWorldPosition();
			}
		}
	}

	static const Vector3 fallbackPositions[] = {
		{ -8.0f, 1.4f, 3.0f },
		{ 0.0f, 1.4f, -4.2f },
		{ 8.0f, 1.4f, 3.0f },
	};
	constexpr int fallbackCount = static_cast<int>(sizeof(fallbackPositions) / sizeof(fallbackPositions[0]));
	if (stageIndex >= 0 && stageIndex < fallbackCount) {
		return fallbackPositions[stageIndex];
	}
	return { 0.0f, 1.4f, 0.0f };
}

void GameSelectScene::EnterSelectedStage() {
	if (isChangingStage_ || stageDecisionCooldown_ > 0.0f) {
		return;
	}

	const auto& stages = StageManager::GetInstance()->GetStages();
	if (selectedStageIndex_ < 0 || selectedStageIndex_ >= static_cast<int>(stages.size())) {
		return;
	}

	const StageData& stage = stages[selectedStageIndex_];
	if (!IsStageUnlocked(selectedStageIndex_)) {
		DebugConsole::GetInstance()->AddLog("Stage Select: " + stage.name + " is locked.");
		stageDecisionCooldown_ = 0.35f;
		return;
	}

	StageManager::GetInstance()->SetCurrentStage(selectedStageIndex_);
	DebugConsole::GetInstance()->AddLog("Stage Select: enter " + stage.name);
	isChangingStage_ = true;
	SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
}

GimmickStageGate* GameSelectScene::FindNearestStageGate(float* outDistance) const {
	if (outDistance) {
		*outDistance = std::numeric_limits<float>::max();
	}
	if (!objectManager_ || !player_) {
		return nullptr;
	}

	const Vector3 playerPos = player_->GetWorldPosition();
	GimmickStageGate* nearestGate = nullptr;
	float nearestDistanceSq = std::numeric_limits<float>::max();

	for (const auto& object : objectManager_->GetObjects()) {
		auto* gate = dynamic_cast<GimmickStageGate*>(object.get());
		if (!gate) {
			continue;
		}

		const Vector3 gatePos = gate->GetWorldPosition();
		float dx = gatePos.x - playerPos.x;
		float dz = gatePos.z - playerPos.z;
		float distanceSq = dx * dx + dz * dz;
		if (distanceSq < nearestDistanceSq) {
			nearestDistanceSq = distanceSq;
			nearestGate = gate;
		}
	}

	if (outDistance && nearestGate) {
		*outDistance = std::sqrt(nearestDistanceSq);
	}
	return nearestGate;
}
