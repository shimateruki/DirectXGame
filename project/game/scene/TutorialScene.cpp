#define NOMINMAX
#include "TutorialScene.h"
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
#include "ProfilerManager.h"
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
#include "IconsFontAwesome5.h"
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
#include "GameSettingsManager.h"

TutorialScene::TutorialScene() {}
TutorialScene::~TutorialScene() {}

void TutorialScene::SetIsGoal(bool isGoal) {
	if (isGoal_ == isGoal) {
		return;
	}

	isGoal_ = isGoal;
	if (!isGoal_) {
		goalSavePerformed_ = false;
	}
}

void TutorialScene::Initialize() {
	using json = nlohmann::json;

	// --- 1. エンジン基盤・リソース初期化 ---
	dxCommon_ = DirectXCommon::GetInstance();
	inputManager_ = InputManager::GetInstance();
	audioPlayer_ = AudioPlayer::GetInstance();


	LOG("Tutorial Scene Initialized!");

	// チュートリアル用の設定 (StageManagerがチュートリアル(-1)に対応している前提)
	const StageData& currentStage = StageManager::GetInstance()->GetCurrentStage();
	bgmHandle_ = audioPlayer_->LoadSoundFile(currentStage.bgmPath);

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
	particleSystem_->Initialize(particleCommon_.get(), "Resources/sprite/common/circle2.png");

	ParticleManager::GetInstance()->Initialize(particleSystem_.get());

	gameRule_ = std::make_unique<GameRule>();
	gameRule_->Initialize(this);

	LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

	// --- 3. サブシステム初期化 ---
	objectManager_ = std::make_unique<ObjectManager>();

	lockOnSystem_ = std::make_unique<LockOnSystem>();
	lockOnSystem_->Initialize(inputManager_);
	uint32_t lockOnTex = TextureManager::GetInstance()->Load("Resources/sprite/ui/hud/lockOn.png"); 
	lockOnSprite_ = std::make_unique<Sprite>();
	lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
	lockOnSprite_->SetAnchorPoint({ 0.5f, 0.5f }); // 画像の中心を基準にする
	lockOnSprite_->SetSize({ 64.0f, 64.0f });      // アイコンのサイズ（適宜調整！）
	BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

	GPUParticleManager::GetInstance()->Initialize(dxCommon_);
	GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
	gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");

	// 1. キューブマップ（DDS）の読み込み
	skyboxTextureHandle_ = TextureManager::GetInstance()->Load("Resources/output_skybox.dds");

	// 2. スカイボックスの生成と初期化
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);

	// --- 5. レベルデータ読み込み (JSON) ---
	levelLoader_ = std::make_unique<LevelLoader>();
	
	// チュートリアル用のパスがあればそちらを、なければ現在の設定に従う
	std::string objectPath = "Resources/json/3Dobject/tutorial.json";
	std::string spritePath = "Resources/json/sprite/tutorialScene.json";

	// ファイルが存在しない場合は代替パス (GamePlayと同じものなど) を検討
	levelLoader_->LoadObjectLayout(this, objectPath);
	levelLoader_->LoadSpriteLayout(this, spritePath);

	saveIndicatorOverlay_ = std::make_unique<SaveIndicatorOverlay>();
	saveIndicatorOverlay_->Initialize(spriteCommon_.get());

	LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
	CameraEditor::GetInstance()->Initialize();
	CameraEditor::GetInstance()->LoadFile("game_camera.json");

	// 課題用アニメーションモデルの生成
	animatedCube_ = std::make_unique<Object3d>();
	animatedCube_->Initialize(object3dCommon_.get());
	animatedCube_->SetModel("Samples/walk"); 
	
	if (animatedCube_->GetModel() && !animatedCube_->GetModel()->GetModelData().animations.empty()) {
		animatedCube_->animName_ = animatedCube_->GetModel()->GetModelData().animations[0].name;
	}
	
	animatedCube_->isAnimLoop_ = true;
	animatedCube_->SetTranslate({0.0f, 0.0f, 0.0f});
	animatedCube_->SetScale({2.0f, 2.0f, 2.0f});

	dxCommon_->FlushCommandQueue(false);
}

void TutorialScene::Finalize() {
	CollisionManager::GetInstance()->ClearObjects();
	BulletManager::GetInstance()->Finalize();
	saveIndicatorOverlay_.reset();
	particleSystem_.reset();
	particleCommon_.reset();
	sprites_.clear();
	spriteCommon_.reset();
	object3dCommon_.reset();
	objectManager_.reset();
	lockOnSystem_.reset();
}

void TutorialScene::Update(float deltaTime) {
	if (isGoal_) {
		if (!goalSavePerformed_) {
			// チュートリアルクリア状況を保存
			GameDataManager::GetInstance()->MarkStageCleared(-1);
			if (saveIndicatorOverlay_) {
				saveIndicatorOverlay_->Play(1.35f);
			}
			DebugConsole::GetInstance()->AddLog("Saving tutorial clear data...");
			goalSavePerformed_ = true;
		}

		if (saveIndicatorOverlay_) {
			saveIndicatorOverlay_->Update(deltaTime);
		}

		deltaTime = 0.0f; // 時を止める

		if (inputManager_->IsKeyTriggered(DIK_SPACE) && (!saveIndicatorOverlay_ || !saveIndicatorOverlay_->IsPlaying())) {
			SceneManager::GetInstance()->ChangeScene("SELECT");
			return;
		}
	}

	// --- ポストエフェクト更新 ---
	PostEffect::GetInstance()->Update(deltaTime);
	Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
	if (activeCamera) {
		PostEffect::GetInstance()->GetParams()->projectionInverse = Math::Inverse(activeCamera->GetProjectionMatrix());
	}

	static Math math;
	LightEditor::GetInstance()->Update();

	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

	lockOnSystem_->Update(objectManager_->GetObjects(), camera, player_);
	CameraEditor::GetInstance()->Update(player_, lockOnSystem_->IsLockingOn());

	Object3d* target = lockOnSystem_->GetTarget();

	if (target && lockOnSystem_->IsLockingOn()) {
		isDrawLockOn_ = true;
		AABB aabb = target->GetAABB();

		Vector3 targetCenter;
		targetCenter.x = (aabb.min.x + aabb.max.x) * 0.5f;
		targetCenter.y = (aabb.min.y + aabb.max.y) * 0.5f;
		targetCenter.z = (aabb.min.z + aabb.max.z) * 0.5f;

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

			float finalSize = baseSize * distanceScale;
			finalSize = std::max(32.0f, std::min(finalSize, 256.0f));

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

	if (!CameraEditor::GetInstance()->IsEditorMode()) {
		Camera::FollowMode currentMode = camera->GetFollowMode();

		if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
			camera->SetRotationSensitivity(GameSettingsManager::GetInstance()->GetCameraSensitivity());
			Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
			if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
				camera->AddRotation(mouseDelta);
			}
		}
	}

	ProfilerManager::GetInstance()->SetObjectList(&objectManager_->GetObjects());
	CameraManager::GetInstance()->Update();
	particleSystem_->Update(deltaTime);
	objectManager_->Update(deltaTime);
	GPUParticleManager::GetInstance()->Update(deltaTime);
	for (auto& sprite : sprites_) {
		sprite->Update();
	}
	if (saveIndicatorOverlay_) {
		saveIndicatorOverlay_->Update(deltaTime);
	}
	BulletManager::GetInstance()->Update(deltaTime);
	CollisionManager::GetInstance()->Update();
	UpdateUI();

	if (animatedCube_) {
		animatedCube_->Update(deltaTime);
	}
}

void TutorialScene::Draw() {
	bool isFirstPerson = false;
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

	if (player_ && camera) {
		Vector3 pPos = player_->GetWorldPosition();
		pPos.y += 1.0f;
		Vector3 cPos = camera->GetEye();
		Vector3 toCam = { cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z };
		float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);

		if (dist < 3.0f) {
			isFirstPerson = true;
		}
	}

	ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
	ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();

	auto& objects = objectManager_->GetObjects();

	if (skybox_ && camera) {
		skybox_->Draw(camera->GetConstantBuffer());
	}

	object3dCommon_->SetGraphicsCommand();
	object3dCommon_->SetPipelineState(BlendMode::kNone);

	for (auto& obj : objects) {
		if (!IsVisible(obj.get())) continue;

		bool isPlayerPart = false;
		if (isFirstPerson) {
			Object3d* current = obj.get();
			while (current) {
				if (current == player_) { isPlayerPart = true; break; }
				current = current->GetParent();
			}
		}

		if (isPlayerPart) continue;
		if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || (obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 22)) continue;

		obj->Draw(pointLightRes, spotLightRes);
	}
	
	if (player_ && player_->GetHookMarker()) {
		player_->GetHookMarker()->Draw(pointLightRes, spotLightRes);
	}

	if (animatedCube_) {
		animatedCube_->Draw(pointLightRes, spotLightRes);
	}

	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	LightEditor::GetInstance()->Draw3D();

	for (auto& obj : objects) {
		bool isPlayerPart = false;
		if (isFirstPerson) {
			Object3d* current = obj.get();
			while (current) {
				if (current == player_) { isPlayerPart = true; break; }
				current = current->GetParent();
			}
		}
		if (isPlayerPart) continue;

		if (obj->GetMaterialType() == 1) {
			obj->Draw(pointLightRes, spotLightRes);
		}
	}
	particleSystem_->Draw();

	bool hasFog = false;
	for (auto& obj : objects) if (obj->GetMaterialType() == 7) { hasFog = true; break; }

	if (hasFog) {
		dxCommon_->PreDrawLocalFog();
		for (auto& obj : objects) if (obj->GetMaterialType() == 7) obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
		dxCommon_->PostDrawLocalFog();
	}

	bool hasFluid = BulletManager::GetInstance()->HasSpecialMaterialBullets();
	for (auto& obj : objects) if (obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 22) { hasFluid = true; break; }

	bool hasGPUParticles = !GPUParticleManager::GetInstance()->IsEmpty();
	if (hasFluid || hasGPUParticles) {
		dxCommon_->UpdateGrabTexture();

		if (hasFluid) {
			for (auto& obj : objects) {
				int matType = obj->GetMaterialType();
				if (matType == 8) obj->DrawWater(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 9) obj->DrawMagma(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 10) obj->DrawIce(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 11) obj->DrawFire(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 12) obj->DrawLaser(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 13) obj->DrawSlimeGel(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 14) obj->DrawShockwave(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 15) obj->DrawLiquidContact(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 16) obj->DrawDamageCrack(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 17) obj->DrawUpdraft(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 18) obj->DrawStunBind(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 19) obj->DrawCrownUnlock(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 20) obj->DrawPoisonSpore(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 21) obj->DrawCloud(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				else if (matType == 22) obj->DrawGatePortal(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			}
		}
		BulletManager::GetInstance()->DrawSpecial(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());

		if (hasGPUParticles) {
			GPUParticleManager::GetInstance()->Draw(
				dxCommon_->GetCommandList(),
				camera->GetViewMatrix(),
				camera->GetProjectionMatrix(),
				gpuParticleTexHandle_,
				dxCommon_->GetDepthSrvHandle()
			);
		}
	}
}

void TutorialScene::DrawUI() {
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
	for (auto& sprite : sprites_) sprite->Draw();
	if (isDrawLockOn_ && lockOnSprite_) lockOnSprite_->Draw();
	if (player_) player_->DrawUI();
	if (saveIndicatorOverlay_ && saveIndicatorOverlay_->IsActive()) saveIndicatorOverlay_->Draw();
}

void TutorialScene::DrawShadow() {
	if (objectManager_) objectManager_->DrawShadow();
}

void TutorialScene::UpdateUI() {}

void TutorialScene::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_INFO_CIRCLE " Scene: Tutorial");
    ImGui::Separator();
    if (ImGui::Button("Back to SELECT")) {
        SceneManager::GetInstance()->ChangeScene("SELECT");
    }
    if (saveIndicatorOverlay_ && ImGui::CollapsingHeader("Save Indicator", ImGuiTreeNodeFlags_DefaultOpen)) {
        saveIndicatorOverlay_->DrawImGui();
    }
    ImGui::Separator();
    ImGui::TextDisabled("※この項目は TutorialScene::DrawImGui() で編集可能です");
#endif
}

void TutorialScene::StartBridgeDropMovie() {}

bool TutorialScene::IsVisible(Object3d* obj) {
    if (!obj) return false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return true;
    AABB worldAabb = obj->GetModelWorldAABB();
    return Math::IntersectFrustumAABB(camera->GetFrustum(), worldAabb.min, worldAabb.max);
}
