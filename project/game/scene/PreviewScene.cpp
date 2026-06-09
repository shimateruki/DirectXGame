#define NOMINMAX
#include "PreviewScene.h"
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
#include <fstream>
#include <string>
#include <algorithm>
#include <cmath>
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

PreviewScene::PreviewScene() {}
PreviewScene::~PreviewScene() {}

void PreviewScene::Initialize() {
	using json = nlohmann::json;

	// --- 1. エンジン基盤・リソース初期化 ---
	dxCommon_ = DirectXCommon::GetInstance();
	inputManager_ = InputManager::GetInstance();
	audioPlayer_ = AudioPlayer::GetInstance();

	LOG("Preview Scene Initialized!");

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
	lockOnSprite_->SetAnchorPoint({ 0.5f, 0.5f }); 
	lockOnSprite_->SetSize({ 64.0f, 64.0f });      
	BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

	GPUParticleManager::GetInstance()->Initialize(dxCommon_);
	GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
	gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");

	skyboxTextureHandle_ = TextureManager::GetInstance()->Load("Resources/output_skybox.dds");
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);

	// --- 5. レベルデータ読み込み (JSON) ---
	levelLoader_ = std::make_unique<LevelLoader>();
	levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/sample.json");
	levelLoader_->LoadSpriteLayout(this, currentStage.spritePath);
	LightManager::GetInstance()->LoadState("Resources/json/light/light_layout.json");
	CameraEditor::GetInstance()->Initialize();
	CameraEditor::GetInstance()->LoadFile("game_camera.json");

	animatedCube_ = std::make_unique<Object3d>();
	animatedCube_->Initialize(object3dCommon_.get());
	animatedCube_->SetModel("Samples/walk"); 
	
	if (animatedCube_->GetModel() && !animatedCube_->GetModel()->GetModelData().animations.empty()) {
		animatedCube_->animName_ = animatedCube_->GetModel()->GetModelData().animations[0].name;
	}
	
	animatedCube_->isAnimLoop_ = true;
	animatedCube_->SetTranslate({0.0f, 0.0f, 0.0f}); 
	animatedCube_->SetScale({2.0f, 2.0f, 2.0f});

	InitializePreviewHUD();

	dxCommon_->FlushCommandQueue(false);
}

void PreviewScene::Finalize() {
	CollisionManager::GetInstance()->ClearObjects();
	BulletManager::GetInstance()->Finalize();
	particleSystem_.reset();
	particleCommon_.reset();
	hudLifeMeter_.reset();
	hudLifeMeterDigit_.reset();
	hudLifeIcon_.reset();
	hudLifeXIcon_.reset();
	for (auto& digit : hudLifeDigits_) {
		digit.reset();
	}
	hudCoinIcon_.reset();
	hudCoinXIcon_.reset();
	for (auto& digit : hudCoinDigits_) {
		digit.reset();
	}
	sprites_.clear();
	spriteCommon_.reset();
	object3dCommon_.reset();
	objectManager_.reset();
	lockOnSystem_.reset();
}

void PreviewScene::Update(float deltaTime) {
	// --- ポストエフェクト更新 ---
	PostEffect::GetInstance()->Update(deltaTime);
	Camera* activeCamera = CameraManager::GetInstance()->GetActiveCamera();
	if (activeCamera) {
		PostEffect::GetInstance()->GetParams()->projectionInverse = Math::Inverse(activeCamera->GetProjectionMatrix());
	}

	static Math math;
	LightEditor::GetInstance()->Update();

	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

	// ロックオン & カメラ制御
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
			float currentRot = lockOnSprite_->GetRotation();
			lockOnSprite_->SetRotation(currentRot + 2.0f * deltaTime);
			lockOnSprite_->Update();
		}
		else {
			isDrawLockOn_ = false;
		}
	}
	else {
		isDrawLockOn_ = false;
	}

	// 自由カメラモード以外の操作
	if (!CameraEditor::GetInstance()->IsEditorMode()) {
		Camera::FollowMode currentMode = camera->GetFollowMode();
		if (currentMode == Camera::FollowMode::kAimable || currentMode == Camera::FollowMode::kFirstPerson) {
			Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
			if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
				camera->AddRotation(mouseDelta);
			}
		}
	}

	// --- 全体更新 ---
	ProfilerManager::GetInstance()->SetObjectList(&objectManager_->GetObjects());
	CameraManager::GetInstance()->Update();
	particleSystem_->Update(deltaTime);
	objectManager_->Update(deltaTime); 
	GPUParticleManager::GetInstance()->Update(deltaTime);
	for (auto& sprite : sprites_) {
		sprite->Update();
	}
	BulletManager::GetInstance()->Update(deltaTime);
	{
		PROFILE_SCOPE("衝突判定");
		CollisionManager::GetInstance()->Update();
	}
	UpdateUI(deltaTime);

	if (animatedCube_) {
		animatedCube_->Update(deltaTime);
	}
}

void PreviewScene::Draw() {
	bool isFirstPerson = false;
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();

	if (!isFirstPerson && player_ && camera) {
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

	// 1. 背景描画
	if (skybox_ && camera) {
		skybox_->Draw(camera->GetConstantBuffer());
	}

	// 2. 不透明モデル描画
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
		if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || obj->GetMaterialType() >= 8) continue;
		obj->Draw(pointLightRes, spotLightRes);
	}
	
	if (player_ && player_->GetHookMarker()) {
		player_->GetHookMarker()->Draw(pointLightRes, spotLightRes);
	}
	if (animatedCube_) {
		animatedCube_->Draw(pointLightRes, spotLightRes);
	}

	// 3. 中間描画
	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	LightEditor::GetInstance()->Draw3D();

	// 4. 半透明モデル描画
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

	// 5. ローカルフォグ
	bool hasFog = false;
	for (auto& obj : objects) {
		if (obj->GetMaterialType() == 7) { hasFog = true; break; }
	}
	if (hasFog) {
		dxCommon_->PreDrawLocalFog();
		for (auto& obj : objects) {
			if (obj->GetMaterialType() == 7) {
				obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
			}
		}
		dxCommon_->PostDrawLocalFog();
	}

	// 6. GPUパーティクル / 流体
	bool hasFluid = false;
	for (auto& obj : objects) {
		if (obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 20) { hasFluid = true; break; }
	}
	bool hasGPUParticles = !GPUParticleManager::GetInstance()->IsEmpty();
	bool grabUpdated = false;
	if (hasFluid || hasGPUParticles) {
		dxCommon_->UpdateGrabTexture();
		grabUpdated = true;
		if (hasFluid) {
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
			}
		}
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

	// =======================================================
	// 6. メッシュエフェクト（アタッチ済み）の描画
	//    エフェクトの歪み(Distortion)はGrabTextureを参照するため、
	//    Object3d::Draw() の中ではなくGrabTexture更新後にここで描画する。
	// =======================================================
	{
		bool hasMeshEffects = false;
		for (auto& obj : objects) {
			if (!obj->GetMeshEffect1Name().empty() || !obj->GetMeshEffect2Name().empty()) {
				hasMeshEffects = true;
				break;
			}
		}
		if (hasMeshEffects) {
			if (!grabUpdated) {
				dxCommon_->UpdateGrabTexture();
			}
			for (auto& obj : objects) {
				if (!obj->GetIsVisible()) continue;
				obj->DrawAttachedEffects(pointLightRes, spotLightRes);
			}
		}
	}
}

void PreviewScene::DrawUI() {
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
	for (auto& sprite : sprites_) {
		sprite->Draw();
	}
	if (isDrawLockOn_ && lockOnSprite_) {
		lockOnSprite_->Draw();
	}
	if (player_) {
		player_->DrawUI();
	}
	DrawPreviewHUD();
}

void PreviewScene::DrawShadow() {
	if (objectManager_) {
		objectManager_->DrawShadow();
	}
}

void PreviewScene::UpdateUI(float deltaTime) {
	UpdatePreviewHUD(deltaTime);
}

std::unique_ptr<Sprite> PreviewScene::CreatePreviewHUDSprite(const std::string& texturePath, const Vector2& position, const Vector2& size, const Vector2& anchor, const Vector4& color) {
	auto sprite = std::make_unique<Sprite>();
	sprite->Initialize(spriteCommon_.get(), texturePath);
	sprite->SetPosition(position);
	sprite->SetSize(size);
	sprite->SetAnchorPoint(anchor);
	sprite->SetColor(color);
	sprite->SetVisible(true);
	sprite->Update();
	return sprite;
}

void PreviewScene::InitializePreviewHUD() {
	hudLifeMeter_ = CreatePreviewHUDSprite(
		"Resources/sprite/ui/hud/life_meter_6.png",
		{ static_cast<float>(WinApp::kClientWidth) - 118.0f, 92.0f },
		{ 138.0f, 138.0f },
		{ 0.5f, 0.5f },
		{ 1.0f, 1.0f, 1.0f, 0.96f }
	);
	hudLifeMeterDigit_ = CreatePreviewHUDSprite(
		"Resources/sprite/number/big6.png",
		{ static_cast<float>(WinApp::kClientWidth) - 118.0f, 95.0f },
		{ 52.0f, 76.0f },
		{ 0.5f, 0.5f },
		{ 1.0f, 0.88f, 0.20f, 1.0f }
	);
	hudLifeIcon_ = CreatePreviewHUDSprite(
		"Resources/sprite/title/slime_save_icon.png",
		{ 38.0f, 100.0f },
		{ 50.0f, 50.0f },
		{ 0.0f, 0.5f },
		{ 1.0f, 1.0f, 1.0f, 0.96f }
	);
	hudLifeXIcon_ = CreatePreviewHUDSprite(
		"Resources/sprite/ui/hud/xUi.png",
		{ 92.0f, 100.0f },
		{ 44.0f, 44.0f },
		{ 0.5f, 0.5f },
		{ 1.0f, 0.96f, 0.62f, 0.96f }
	);
	for (auto& digit : hudLifeDigits_) {
		digit = CreatePreviewHUDSprite(
			"Resources/sprite/number/0.png",
			{ 100.0f, 100.0f },
			{ 26.0f, 38.0f },
			{ 0.5f, 0.5f },
			{ 1.0f, 0.95f, 0.56f, 1.0f }
		);
	}

	hudCoinIcon_ = CreatePreviewHUDSprite(
		"Resources/sprite/ui/hud/coin_icon.png",
		{ 38.0f, 154.0f },
		{ 48.0f, 48.0f },
		{ 0.0f, 0.5f },
		{ 1.0f, 1.0f, 1.0f, 0.96f }
	);
	hudCoinXIcon_ = CreatePreviewHUDSprite(
		"Resources/sprite/ui/hud/xUi.png",
		{ 92.0f, 154.0f },
		{ 44.0f, 44.0f },
		{ 0.5f, 0.5f },
		{ 1.0f, 0.90f, 0.42f, 0.96f }
	);
	for (auto& digit : hudCoinDigits_) {
		digit = CreatePreviewHUDSprite(
			"Resources/sprite/number/0.png",
			{ 100.0f, 154.0f },
			{ 26.0f, 38.0f },
			{ 0.5f, 0.5f },
			{ 1.0f, 0.86f, 0.28f, 1.0f }
		);
	}

	hudPreviousHp_ = player_ ? player_->GetHp() : 0.0f;
	hudDamagePulseTimer_ = 0.0f;
	hudDisplayedLife_ = 6;
	UpdatePreviewHUD(0.0f);
}

void PreviewScene::SetPreviewHUDNumber(std::array<std::unique_ptr<Sprite>, 2>& digits, int value, const Vector2& rightAlignedPosition, float digitHeight, const Vector4& color, bool visible) {
	value = std::clamp(value, 0, 99);

	const std::array<int, 2> digitValues = { value / 10, value % 10 };
	const int digitCount = value >= 10 ? 2 : 1;
	const float digitWidth = digitHeight * 0.68f;
	const float spacing = digitWidth * 0.82f;
	const float totalWidth = digitCount == 2 ? spacing + digitWidth : digitWidth;
	const float startX = rightAlignedPosition.x - totalWidth + digitWidth * 0.5f;

	for (int i = 0; i < 2; ++i) {
		Sprite* sprite = digits[i].get();
		if (!sprite) continue;

		const bool digitVisible = visible && (digitCount == 2 || i == 1);
		sprite->SetVisible(digitVisible);
		if (!digitVisible) {
			continue;
		}

		const int sourceIndex = digitCount == 2 ? i : 1;
		const int digit = digitValues[sourceIndex];
		const uint32_t handle = Sprite::LoadTexture("number/" + std::to_string(digit) + ".png");
		sprite->SetTextureHandle(handle);
		sprite->SetPosition({ startX + (sourceIndex - (2 - digitCount)) * spacing, rightAlignedPosition.y });
		sprite->SetSize({ digitWidth, digitHeight });
		sprite->SetColor(color);
		sprite->Update();
	}
}

void PreviewScene::UpdatePreviewHUD(float deltaTime) {
	const bool visible = player_ != nullptr;
	const float maxHp = player_ ? std::max(player_->GetMaxHp(), 1.0f) : 1.0f;
	const float hp = player_ ? std::clamp(player_->GetHp(), 0.0f, maxHp) : 0.0f;
	const float hpRate = hp / maxHp;
	int lifeValue = hp <= 0.0f ? 0 : static_cast<int>(std::ceil(hpRate * 6.0f));
	lifeValue = std::clamp(lifeValue, 0, 6);

	if (visible && (hp < hudPreviousHp_ - 0.01f || lifeValue != hudDisplayedLife_)) {
		hudDamagePulseTimer_ = 0.28f;
	}
	hudDisplayedLife_ = lifeValue;
	hudPreviousHp_ = hp;
	hudDamagePulseTimer_ = std::max(0.0f, hudDamagePulseTimer_ - deltaTime);

	const float pulse = hudDamagePulseTimer_ > 0.0f ? std::sin(hudDamagePulseTimer_ * 70.0f) : 0.0f;
	const float lifePulse = hudDamagePulseTimer_ > 0.0f ? 1.0f + std::abs(pulse) * 0.08f : 1.0f;
	const Vector2 meterCenter = { static_cast<float>(WinApp::kClientWidth) - 118.0f, 92.0f };

	if (hudLifeMeter_) {
		const uint32_t handle = Sprite::LoadTexture("ui/hud/life_meter_" + std::to_string(lifeValue) + ".png");
		hudLifeMeter_->SetTextureHandle(handle);
		hudLifeMeter_->SetVisible(visible);
		hudLifeMeter_->SetPosition(meterCenter);
		hudLifeMeter_->SetSize({ 138.0f * lifePulse, 138.0f * lifePulse });
		hudLifeMeter_->SetRotation(pulse * 0.03f);
		hudLifeMeter_->SetColor({ 1.0f, 1.0f, 1.0f, visible ? 0.96f : 0.0f });
		hudLifeMeter_->Update();
	}
	if (hudLifeMeterDigit_) {
		const uint32_t handle = Sprite::LoadTexture("number/big" + std::to_string(lifeValue) + ".png");
		hudLifeMeterDigit_->SetTextureHandle(handle);
		hudLifeMeterDigit_->SetVisible(visible);
		hudLifeMeterDigit_->SetPosition({ meterCenter.x, meterCenter.y + 3.0f });
		hudLifeMeterDigit_->SetSize({ 52.0f * lifePulse, 76.0f * lifePulse });
		hudLifeMeterDigit_->SetColor(lifeValue <= 1 ? Vector4{ 1.0f, 0.35f, 0.25f, 1.0f } : Vector4{ 1.0f, 0.88f, 0.20f, 1.0f });
		hudLifeMeterDigit_->Update();
	}
	if (hudLifeIcon_) {
		hudLifeIcon_->SetVisible(visible);
		hudLifeIcon_->SetPosition({ 38.0f, 100.0f });
		hudLifeIcon_->SetSize({ 50.0f * lifePulse, 50.0f * lifePulse });
		hudLifeIcon_->SetColor({ 1.0f, 1.0f, 1.0f, visible ? 0.96f : 0.0f });
		hudLifeIcon_->Update();
	}
	if (hudLifeXIcon_) {
		hudLifeXIcon_->SetVisible(visible);
		hudLifeXIcon_->SetPosition({ 92.0f, 100.0f });
		hudLifeXIcon_->SetSize({ 44.0f, 44.0f });
		hudLifeXIcon_->SetColor({ 1.0f, 0.96f, 0.62f, visible ? 0.96f : 0.0f });
		hudLifeXIcon_->Update();
	}

	SetPreviewHUDNumber(
		hudLifeDigits_,
		GameDataManager::GetInstance()->GetLives(),
		{ 162.0f, 100.0f },
		40.0f,
		{ 1.0f, 0.95f, 0.56f, 1.0f },
		visible
	);

	if (hudCoinIcon_) {
		hudCoinIcon_->SetVisible(visible);
		hudCoinIcon_->SetPosition({ 38.0f, 154.0f });
		hudCoinIcon_->SetSize({ 48.0f, 48.0f });
		hudCoinIcon_->SetColor({ 1.0f, 1.0f, 1.0f, visible ? 0.96f : 0.0f });
		hudCoinIcon_->Update();
	}
	if (hudCoinXIcon_) {
		hudCoinXIcon_->SetVisible(visible);
		hudCoinXIcon_->SetPosition({ 92.0f, 154.0f });
		hudCoinXIcon_->SetSize({ 44.0f, 44.0f });
		hudCoinXIcon_->SetColor({ 1.0f, 0.90f, 0.42f, visible ? 0.96f : 0.0f });
		hudCoinXIcon_->Update();
	}
	SetPreviewHUDNumber(
		hudCoinDigits_,
		GameDataManager::GetInstance()->GetCoins(),
		{ 162.0f, 154.0f },
		40.0f,
		{ 1.0f, 0.86f, 0.28f, 1.0f },
		visible
	);
}

void PreviewScene::DrawPreviewHUD() {
	if (hudLifeIcon_) hudLifeIcon_->Draw();
	if (hudLifeXIcon_) hudLifeXIcon_->Draw();
	for (auto& digit : hudLifeDigits_) {
		if (digit) digit->Draw();
	}
	if (hudCoinIcon_) hudCoinIcon_->Draw();
	if (hudCoinXIcon_) hudCoinXIcon_->Draw();
	for (auto& digit : hudCoinDigits_) {
		if (digit) digit->Draw();
	}
	if (hudLifeMeter_) hudLifeMeter_->Draw();
	if (hudLifeMeterDigit_) hudLifeMeterDigit_->Draw();
}

void PreviewScene::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_INFO_CIRCLE " Scene: Preview Scene");
    ImGui::Separator();

    if (ImGui::Button("Back to Title")) {
        SceneManager::GetInstance()->ChangeScene("TITLE");
    }

    if (ImGui::CollapsingHeader(ICON_FA_MAP " Preview Stage Config", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Currently replicating GamePlayScene environment.");
        if (ImGui::Button("Reload Preview")) {
            SceneManager::GetInstance()->ChangeScene("PREVIEW");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_HEART " Life / Coin Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        int lives = GameDataManager::GetInstance()->GetLives();
        int coins = GameDataManager::GetInstance()->GetCoins();
        ImGui::Text(ICON_FA_HEART " Remaining Lives: %d", lives);
        ImGui::Text(ICON_FA_COINS " Coins: %d / 100", coins);
        if (ImGui::Button("Add 100 Coins (1UP Test)")) {
            GameDataManager::GetInstance()->AddCoin(100);
        }
        if (ImGui::Button("Reset Lives to 3")) {
            GameDataManager::GetInstance()->ResetLives();
        }
        if (ImGui::Button("Reset Coins")) {
            GameDataManager::GetInstance()->ResetCoins();
        }
    }
    
    ImGui::Separator();
    ImGui::TextDisabled("※PreviewScene is a perfect clone of GamePlayScene.");
#endif
}

void PreviewScene::StartBridgeDropMovie() {}

bool PreviewScene::IsVisible(Object3d* obj) {
    if (!obj) return false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return true;
    AABB worldAabb = obj->GetModelWorldAABB();
    return Math::IntersectFrustumAABB(camera->GetFrustum(), worldAabb.min, worldAabb.max);
}
