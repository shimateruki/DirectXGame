#define NOMINMAX
#include "GameSelectScene.h"

#include <algorithm>
#include <cmath>
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
#include "GameAudioSettings.h"
#include "GameSettingsManager.h"
#include "GimmickStageGate.h"
#include "CollisionConfig.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <sstream>

namespace {
constexpr float kUnlockPresentationDuration = 2.6f;
constexpr uint32_t kStageSelectSolidMask = 0xFFFFFFFFu;
constexpr const char* kStageClearCrownPrefix = "__Editor_StageClearCrown_";
constexpr const char* kStageClearCrownModel = "Stages/crown";
constexpr float kStageClearCrownBaseScale = 0.42f;
constexpr float kStageClearCrownHeightOffset = 0.85f;
constexpr const char* kCrownIdleParticlePreset = "crown_idle_sparkle";
constexpr const char* kCrownUnlockParticlePreset = "crown_get_burst";
constexpr const char* kCrownUnlockRayParticlePreset = "crown_get_rays";
constexpr const char* kCrownUnlockFountainParticlePreset = "crown_get_twinkle_fountain";
constexpr const char* kCrownUnlockAfterglowParticlePreset = "crown_get_afterglow";
constexpr const char* kCrownAuraEffectPath = "Resources/json/effect/effect_crown_aura_ring.json";
constexpr const char* kCrownRayEffectPath = "Resources/json/effect/effect_crown_ray_plane.json";
constexpr const char* kCrownUnlockFlashRingEffectPath = "Resources/json/effect/effect_crown_get_flash_ring.json";
constexpr const char* kCrownUnlockRayFlashEffectPath = "Resources/json/effect/effect_crown_get_ray_flash.json";
constexpr const char* kSpriteResourcePrefix = "Resources/sprite/";
constexpr float kCrownCountPresentationDuration = 2.65f;
constexpr float kCrownCountImpactTime = 1.05f;
constexpr float kCrownCountParticleInterval = 0.08f;

// ゲート突入演出は、CameraEditorの保存位置ではなくゲートのTransformから自動生成する。
// ゲートを移動してもカメラ演出を再調整しなくて済むように、距離・高さ・補間時間だけをここで管理する。
constexpr float kGateEntryCinematicDuration = 1.55f;
constexpr float kGateEntryMinPlaneThickness = 0.16f;
constexpr float kGateEntryMaxPlaneThickness = 0.42f;
constexpr float kGateEntryMinHalfWidth = 0.85f;
constexpr float kGateEntryMaxHalfWidth = 2.45f;
constexpr float kGateEntryPlayerTouchRadius = 0.24f;
constexpr float kGateEntrySurfaceOffset = 1.18f;
constexpr float kGateEntryInsideDepth = 1.38f;
constexpr float kGateEntryLiftHeight = 0.18f;
constexpr float kGateEntryCenteringDelay = 0.06f;
constexpr float kGateEntryCenteringDuration = 0.36f;
constexpr float kGateEntryCameraBackDistance = 14.25f;
constexpr float kGateEntryCameraSideOffset = 0.0f;
constexpr float kGateEntryCameraHeight = 0.58f;
constexpr float kGateEntryCameraFocusHeight = 0.92f;
constexpr float kGateEntryCameraBlendTime = 0.34f;
constexpr float kGateEntryAlignEndTime = 0.28f;
constexpr float kGateEntryDiveStartTime = 0.18f;
constexpr float kGateEntryDiveEndTime = 0.88f;
constexpr float kGateEntryDissolveStartTime = 0.10f;
constexpr float kGateEntryDissolveEndTime = 0.74f;
constexpr float kGateReturnEmergingDuration = 1.68f;
constexpr float kGateReturnCameraIntroDuration = 0.24f;
constexpr float kGateReturnCameraRestoreStartTime = 1.62f;
constexpr float kGateReturnCameraRestoreDuration = 1.05f;
constexpr float kGateReturnTotalDuration = kGateReturnCameraRestoreStartTime + kGateReturnCameraRestoreDuration;
constexpr float kGateReturnExitForwardOffset = 5.60f;
constexpr float kGateReturnGroundOffsetFromGateOrigin = 1.50f;
constexpr float kGateReturnGroundProbeHeight = 24.0f;
constexpr float kGateReturnGroundProbeDistance = 80.0f;
constexpr float kGateReturnHopHeight = 2.10f;
constexpr float kGateReturnCameraBackDistance = 20.0f;
constexpr float kGateReturnCameraSideOffset = 4.20f;
constexpr float kGateReturnCameraHeight = 3.00f;
constexpr float kGateReturnCameraFocusHeight = 1.15f;
constexpr float kGateReturnCameraLandingFocusRate = 0.72f;
constexpr float kGateReturnThirdPersonDistance = 16.0f;
constexpr float kGateReturnThirdPersonHeight = 3.20f;
constexpr float kGateReturnThirdPersonTargetHeight = 1.45f;
constexpr float kGateReturnThirdPersonPitch = 0.32f;
constexpr float kGateCinematicBarHeight = 0.105f;
constexpr float kGateCinematicBarOpenSpeed = 5.8f;
constexpr float kGateCinematicBarCloseSpeed = 4.2f;

float SelectClamp01(float value) {
	return std::clamp(value, 0.0f, 1.0f);
}

float SelectEaseOutCubic(float value) {
	value = SelectClamp01(value);
	const float inv = 1.0f - value;
	return 1.0f - inv * inv * inv;
}

Vector3 SelectLookAtRotation(const Vector3& eye, const Vector3& target) {
	Vector3 direction{
		target.x - eye.x,
		target.y - eye.y,
		target.z - eye.z
	};
	const float lengthSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
	if (lengthSq <= 0.000001f) {
		return { 0.0f, 0.0f, 0.0f };
	}

	const float invLength = 1.0f / std::sqrt(lengthSq);
	direction.x *= invLength;
	direction.y *= invLength;
	direction.z *= invLength;

	const float pitch = -std::asin(std::clamp(direction.y, -0.98f, 0.98f));
	const float yaw = std::atan2(direction.x, direction.z);
	return { pitch, yaw, 0.0f };
}

Vector3 SelectNormalizeGateDirectionXZ(const Vector3& value, const Vector3& fallback) {
	Vector3 direction{ value.x, 0.0f, value.z };
	float lengthSq = direction.x * direction.x + direction.z * direction.z;
	if (lengthSq <= 0.000001f) {
		direction = { fallback.x, 0.0f, fallback.z };
		lengthSq = direction.x * direction.x + direction.z * direction.z;
		if (lengthSq <= 0.000001f) {
			return { 0.0f, 0.0f, 1.0f };
		}
	}

	const float invLength = 1.0f / std::sqrt(lengthSq);
	direction.x *= invLength;
	direction.z *= invLength;
	return direction;
}

float SelectResolveGateReturnGroundY(const Vector3& probePosition, const Vector3& gatePos, const Vector3& stageNodePosition, const Vector3& playerPosition, const Object3d* ignoredObject) {
	// ゲートの原点や縁は浮いていることがあるため、帰還演出の接地Yにはそのまま使いません。
	// まず着地予定XZから下向きレイを飛ばし、本当に床として登録されている位置を優先します。
	const float gateBasedGroundY = gatePos.y - kGateReturnGroundOffsetFromGateOrigin;
	const float fallbackGroundY = (std::min)({ stageNodePosition.y, playerPosition.y, gateBasedGroundY });
	const float rayStartY = (std::max)({ gatePos.y, stageNodePosition.y, playerPosition.y, fallbackGroundY }) + kGateReturnGroundProbeHeight;
	const Vector3 sampleOffsets[] = {
		{ 0.0f, 0.0f, 0.0f },
		{ 0.65f, 0.0f, 0.0f },
		{ -0.65f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.65f },
		{ 0.0f, 0.0f, -0.65f }
	};

	bool foundGround = false;
	float resolvedGroundY = fallbackGroundY;
	for (const Vector3& offset : sampleOffsets) {
		const Vector3 rayStart = {
			probePosition.x + offset.x,
			rayStartY,
			probePosition.z + offset.z
		};
		const RaycastHit hit = CollisionManager::GetInstance()->Raycast(rayStart, { 0.0f, -1.0f, 0.0f }, kGateReturnGroundProbeDistance, kAllGround);
		if (!hit.isHit || hit.hitObject == ignoredObject) {
			continue;
		}

		if (!foundGround || hit.hitPoint.y > resolvedGroundY) {
			resolvedGroundY = hit.hitPoint.y;
			foundGround = true;
		}
	}

	return resolvedGroundY;
}

Vector3 SelectGateEntryDirection(Object3d* gateObject, const Vector3& fallbackDirection) {
	const Vector3 fallback = SelectNormalizeGateDirectionXZ(fallbackDirection, { 0.0f, 0.0f, 1.0f });
	if (!gateObject) {
		return fallback;
	}

	Transform* transform = gateObject->GetTransform();
	if (!transform) {
		return fallback;
	}

	const Matrix4x4& world = transform->matWorld;
	Vector3 candidates[] = {
		SelectNormalizeGateDirectionXZ({ world.m[0][0], world.m[0][1], world.m[0][2] }, fallback),
		SelectNormalizeGateDirectionXZ({ world.m[1][0], world.m[1][1], world.m[1][2] }, fallback),
		SelectNormalizeGateDirectionXZ({ world.m[2][0], world.m[2][1], world.m[2][2] }, fallback)
	};

	Vector3 gateAxis = fallback;
	float bestDot = -1.0f;
	for (const Vector3& candidate : candidates) {
		const float dot = std::fabs(candidate.x * fallback.x + candidate.z * fallback.z);
		if (dot > bestDot) {
			bestDot = dot;
			gateAxis = candidate;
		}
	}

	const float dot = gateAxis.x * fallback.x + gateAxis.z * fallback.z;
	if (dot < 0.0f) {
		gateAxis.x *= -1.0f;
		gateAxis.z *= -1.0f;
	}
	return gateAxis;
}

struct SelectGateEntryRoute {
	Vector3 surface;
	Vector3 center;
	Vector3 inside;
};

SelectGateEntryRoute SelectBuildGateEntryRoute(const Vector3& gatePos, const Vector3& direction, float entryY) {
	const Vector3 entryDirection = SelectNormalizeGateDirectionXZ(direction, { 0.0f, 0.0f, 1.0f });
	return {
		{
			gatePos.x - entryDirection.x * kGateEntrySurfaceOffset,
			entryY,
			gatePos.z - entryDirection.z * kGateEntrySurfaceOffset
		},
		{
			gatePos.x,
			entryY,
			gatePos.z
		},
		{
			gatePos.x + entryDirection.x * kGateEntryInsideDepth,
			entryY,
			gatePos.z + entryDirection.z * kGateEntryInsideDepth
		}
	};
}

float SelectEaseInOutCubic(float value) {
	value = SelectClamp01(value);
	if (value < 0.5f) {
		return 4.0f * value * value * value;
	}
	const float f = -2.0f * value + 2.0f;
	return 1.0f - (f * f * f) * 0.5f;
}

Vector3 SelectLerpVector3(const Vector3& a, const Vector3& b, float t) {
	t = SelectClamp01(t);
	return {
		a.x + (b.x - a.x) * t,
		a.y + (b.y - a.y) * t,
		a.z + (b.z - a.z) * t
	};
}

Vector3 SelectBezierVector3(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
	t = SelectClamp01(t);
	const float inv = 1.0f - t;
	const float b0 = inv * inv * inv;
	const float b1 = 3.0f * inv * inv * t;
	const float b2 = 3.0f * inv * t * t;
	const float b3 = t * t * t;
	return {
		p0.x * b0 + p1.x * b1 + p2.x * b2 + p3.x * b3,
		p0.y * b0 + p1.y * b1 + p2.y * b2 + p3.y * b3,
		p0.z * b0 + p1.z * b1 + p2.z * b2 + p3.z * b3
	};
}

Vector3 SelectNormalizeXZ(const Vector3& value, const Vector3& fallback) {
	const float lengthSq = value.x * value.x + value.z * value.z;
	if (lengthSq <= 0.0001f) {
		return fallback;
	}
	const float invLength = 1.0f / std::sqrt(lengthSq);
	return { value.x * invLength, 0.0f, value.z * invLength };
}

float SelectEaseOutBack(float value) {
	value = SelectClamp01(value);
	constexpr float c1 = 1.70158f;
	constexpr float c3 = c1 + 1.0f;
	const float x = value - 1.0f;
	return 1.0f + c3 * x * x * x + c1 * x * x;
}

Vector3 SelectLerp(const Vector3& a, const Vector3& b, float t) {
	t = SelectClamp01(t);
	return {
		a.x + (b.x - a.x) * t,
		a.y + (b.y - a.y) * t,
		a.z + (b.z - a.z) * t
	};
}

std::string ToSpriteRelativePath(const std::string& path) {
	if (path.rfind(kSpriteResourcePrefix, 0) == 0) {
		return path.substr(std::string(kSpriteResourcePrefix).size());
	}
	return path;
}

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

Vector4 GetStageGateModelColor(bool unlocked, bool selected, bool cleared, bool unlocking, float pulse) {
	if (!unlocked && !unlocking) {
		return { 0.20f, 0.20f, 0.24f, 0.82f };
	}
	if (unlocking) {
		return { 1.0f, 0.82f + pulse * 0.10f, 0.28f, 1.0f };
	}
	if (selected) {
		return { 1.0f, 0.92f, 0.42f, 1.0f };
	}
	if (cleared) {
		return { 0.72f, 1.0f, 0.78f, 1.0f };
	}
	return { 0.86f, 0.95f, 1.0f, 1.0f };
}

float GetStageGateModelEmissive(bool unlocked, bool selected, bool cleared, bool unlocking, float pulse) {
	if (unlocking) {
		return 2.7f + pulse * 1.2f;
	}
	if (!unlocked) {
		return 0.25f;
	}
	if (selected) {
		return 1.9f + pulse * 0.55f;
	}
	return cleared ? 1.35f : 1.0f;
}

std::string MakeStageClearCrownName(int stageIndex) {
	return std::string(kStageClearCrownPrefix) + std::to_string(stageIndex);
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
	crownCountPresentationActive_ = false;
	crownCountPresentationImpactDone_ = false;
	crownCountPresentationStageIndex_ = -1;
	crownCountPresentationFrom_ = 0;
	crownCountPresentationTo_ = 0;
	crownCountPresentationTimer_ = 0.0f;
	crownCountPresentationParticleTimer_ = 0.0f;
	gateReturnPresentationActive_ = false;
	gateReturnPresentationTimer_ = 0.0f;
	gateReturnStageIndex_ = -1;
	gateReturnTargetGate_ = nullptr;
	pendingStageClearRewardPresentation_ = {};

	bgmHandle_ = audioPlayer_->LoadSoundFile(
		ResolveSceneBgmPath("Resources/bgm/Alarm02.mp3"));

	// --- 2. 各種マネージャ初期化 ---
	EventManager::GetInstance()->ClearAllListeners();
	CameraManager::GetInstance()->Initialize();
	CameraManager::GetInstance()->SetInputManager(inputManager_);

	spriteCommon_ = std::make_unique<SpriteCommon>();
	spriteCommon_->Initialize(dxCommon_);

	object3dCommon_ = std::make_unique<Object3dCommon>();
	object3dCommon_->Initialize(dxCommon_);
	MeshEffectManager::GetInstance()->Initialize(object3dCommon_.get());

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

	gatePromptSprite_ = std::make_unique<Sprite>();
	gatePromptSprite_->Initialize(spriteCommon_.get(), "Resources/sprite/ui/gate/gate_prompt_player_e.png");
	gatePromptSprite_->SetAnchorPoint({ 0.5f, 0.5f });
	gatePromptSprite_->SetSize({ 192.0f, 64.0f });
	gatePromptSprite_->SetVisible(false);
	BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());

	GPUParticleManager::GetInstance()->Initialize(dxCommon_);
	GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
	// パーティクルで使う画像を読み込み、ハンドル(番号)を保存しておく
	gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");

	// 1. キューブマップ（DDS）の読み込み
	skyboxTextureHandle_ = TextureManager::GetInstance()->Load(
		ResolveSceneSkyboxPath("Resources/output_skybox.dds"));

	// 2. スカイボックスの生成と初期化
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);

	// --- 5. レベルデータ読み込み (JSON) ---
	levelLoader_ = std::make_unique<LevelLoader>();
	// セレクトシーン用のレイアウトがあればそれを読み込むが、一旦 bossStage.json で代用
	levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/stageSelect.json");
	levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/stageSelect_sprite.json");
	InitializeStageSelectHUD();
	LightManager::GetInstance()->LoadState(
		ResolveSceneLightPath("Resources/json/light/light_layout.json"));
	CameraEditor::GetInstance()->Initialize();
	CameraEditor::GetInstance()->LoadFile(ResolveSceneCameraPath("game_camera.json"));

	auto* gameData = GameDataManager::GetInstance();
	gameData->MarkStageUnlockSeen(0);
	pendingStageClearRewardPresentation_ = gameData->ConsumeStageClearRewardPresentation();
	const bool startedReturnPresentation = StartStageReturnPresentation(gameData->ConsumeStageSelectReturnPresentation());
	if (!startedReturnPresentation) {
		StartStageClearRewardPresentation(pendingStageClearRewardPresentation_);
		pendingStageClearRewardPresentation_ = {};
	}
	unlockingStageIndex_ = (gateReturnPresentationActive_ || crownCountPresentationActive_) ? -1 : FindPendingUnlockStage();
	if (unlockingStageIndex_ >= 0) {
		selectedStageIndex_ = unlockingStageIndex_;
		DebugConsole::GetInstance()->AddLog("Stage Select: crown unlock presentation start.");
	}
	ApplyStageGateStates();
	UpdateStageSelectDecorations(0.0f);
    
	dxCommon_->FlushCommandQueue(false);
}

void GameSelectScene::Finalize() {
	MeshEffectManager::GetInstance()->Clear();
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
			camera->SetRotationSensitivity(GameSettingsManager::GetInstance()->GetCameraSensitivity());
			Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
			if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) camera->AddRotation(mouseDelta);
		}
	}

	// --- 全体更新 ---
	if (gateEntryCinematicActive_) {
		UpdateGateEntryCinematic(deltaTime);
	}
	if (gateReturnPresentationActive_) {
		UpdateStageReturnPresentation(deltaTime);
	}

	CameraManager::GetInstance()->Update(deltaTime);
	particleSystem_->Update(deltaTime);
	UpdateStageGateSelection(deltaTime);
	objectManager_->Update(deltaTime);
	if (gateReturnPresentationActive_) {
		UpdateStageReturnPresentation(0.0f);
		CameraManager::GetInstance()->Update(0.0f);
	}
	GPUParticleManager::GetInstance()->Update(deltaTime);
	for (auto& sprite : sprites_) sprite->Update();
	BulletManager::GetInstance()->Update(deltaTime);
	CollisionManager::GetInstance()->Update();
	UpdateUI();
	UpdateGateCinematicBars(deltaTime);

	if (animatedCube_) animatedCube_->Update(deltaTime);
}

void GameSelectScene::Draw() {
	bool isFirstPerson = false;
	Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

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

	if (skybox_ && camera && LightManager::GetInstance()->IsSkyboxEnabled()) {
		skybox_->SetTextureHandle(LightManager::GetInstance()->GetSkyboxTextureHandle());
		skybox_->Draw(camera->GetConstantBuffer());
	}

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
        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || (obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 22)) continue;
		obj->Draw(pointLightRes, spotLightRes);
	}

	if (animatedCube_) animatedCube_->Draw(pointLightRes, spotLightRes);

	// --- 2. 中間描画 ---
	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	LightEditor::GetInstance()->Draw3D();

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
	DrawLocalFogObjects(objects, dxCommon_, player_, isFirstPerson);

	// 5. 流体描画
	const bool grabUpdated = DrawSpecialMaterialObjects(objects, dxCommon_, BulletManager::GetInstance(), player_, isFirstPerson);

	// 6. GPUパーティクル
	DrawGPUParticles(dxCommon_, camera, gpuParticleTexHandle_, grabUpdated);
}

void GameSelectScene::DrawUI() {
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
	for (auto& sprite : sprites_) sprite->Draw();
	if (gatePromptSprite_) gatePromptSprite_->Draw();
	if (isDrawLockOn_ && lockOnSprite_) lockOnSprite_->Draw();
}

void GameSelectScene::DrawShadow() {
	if (objectManager_) objectManager_->DrawShadow();
}

void GameSelectScene::DrawImGui() {
#ifdef USE_IMGUI
	ImGui::Text("ステージセレクト設定");
	ImGui::Separator();

	if (ImGui::CollapsingHeader("ゲート演出デバッグ", ImGuiTreeNodeFlags_DefaultOpen)) {
		float nearestDistance = std::numeric_limits<float>::max();
		Object3d* nearestGate = FindNearestStageGate(&nearestDistance);
		const int nearestGateStageIndex = GetStageGateIndex(nearestGate);
		const auto& stages = StageManager::GetInstance()->GetStages();
		const bool hasValidGate =
			player_ &&
			nearestGate &&
			nearestGateStageIndex >= 0 &&
			nearestGateStageIndex < static_cast<int>(stages.size());
		const bool canPlayDebug = hasValidGate && !gateEntryCinematicActive_;

		if (hasValidGate) {
			ImGui::Text("対象ゲート: Stage %d", nearestGateStageIndex + 1);
			ImGui::Text("プレイヤーからの距離: %.2f", nearestDistance);
			ImGui::TextDisabled("通常の接触判定を使わず、同じゲート突入演出を強制再生します。");
		} else {
			ImGui::TextDisabled("対象ゲート: なし");
			ImGui::TextDisabled("プレイヤーまたはステージゲートが見つからないため再生できません。");
		}

		ImGui::TextDisabled("ショートカット: G キー");

		const auto playDebugGateEntry = [&]() {
			selectedStageIndex_ = nearestGateStageIndex;
			StageManager::GetInstance()->SetCurrentStage(selectedStageIndex_);
			DebugConsole::GetInstance()->AddLog("Stage Select: debug gate entry cinematic.");
			StartGateEntryCinematic(selectedStageIndex_);
		};

		const bool shortcutPressed =
			canPlayDebug &&
			!ImGui::GetIO().WantTextInput &&
			ImGui::IsKeyPressed(ImGuiKey_G, false);

		if (shortcutPressed) {
			playDebugGateEntry();
		}

		if (!canPlayDebug) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("最寄りゲートで突入演出を強制再生", ImVec2(-1.0f, 34.0f))) {
			playDebugGateEntry();
		}
		if (!canPlayDebug) {
			ImGui::EndDisabled();
		}

		if (gateEntryCinematicActive_) {
			ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "ゲート突入演出を再生中です。");
		}
	}
#endif
}

Sprite* GameSelectScene::FindSpriteByName(const std::string& name) const {
	for (const auto& sprite : sprites_) {
		if (sprite && sprite->GetName() == name) {
			return sprite.get();
		}
	}
	return nullptr;
}

GameSelectScene::StageSelectHudSprite GameSelectScene::BindStageSelectHUDSprite(
	const std::string& name,
	const std::string& texturePath,
	const Vector2& position,
	const Vector2& size,
	const Vector2& anchor,
	const Vector4& color) {

	Sprite* sprite = FindSpriteByName(name);
	bool createdFromFallback = false;
	if (!sprite) {
		auto newSprite = std::make_unique<Sprite>();
		newSprite->Initialize(spriteCommon_.get(), texturePath);
		newSprite->SetName(name);
		newSprite->SetTextureName(ToSpriteRelativePath(texturePath));
		sprite = newSprite.get();
		sprites_.push_back(std::move(newSprite));
		createdFromFallback = true;
	}

	if (sprite->GetTextureName().empty()) {
		sprite->SetTextureName(ToSpriteRelativePath(texturePath));
	}

	if (createdFromFallback) {
		sprite->SetPosition(position);
		sprite->SetSize(size);
		sprite->SetAnchorPoint(anchor);
		sprite->SetColor(color);
	}
	sprite->SetVisible(true);
	sprite->Update();

	return { sprite, sprite->GetPosition(), sprite->GetSize(), sprite->GetColor() };
}

void GameSelectScene::InitializeStageSelectHUD() {
	const Vector4 white = { 1.0f, 1.0f, 1.0f, 0.96f };
	const Vector4 crownNumber = { 1.0f, 0.94f, 0.50f, 1.0f };
	const Vector4 starNumber = { 1.0f, 0.90f, 0.30f, 1.0f };
	const Vector4 coinNumber = { 1.0f, 0.82f, 0.24f, 1.0f };
	const Vector4 xColor = { 1.0f, 0.95f, 0.58f, 0.96f };
	const std::array<float, 3> rows = { 86.0f, 142.0f, 198.0f };
	const std::array<std::string, 3> iconTextures = {
		"Resources/sprite/ui/title/crown_progress_icon.png",
		"Resources/sprite/ui/hud/stage_star_filled.png",
		"Resources/sprite/ui/hud/coin_icon.png"
	};

	stageSelectCrownIcon_ = BindStageSelectHUDSprite("stage_select_hud_crown_icon", iconTextures[0], { 42.0f, rows[0] }, { 48.0f, 48.0f }, { 0.0f, 0.5f }, white);
	stageSelectCrownXIcon_ = BindStageSelectHUDSprite("stage_select_hud_crown_x", "Resources/sprite/ui/hud/xUi.png", { 94.0f, rows[0] }, { 38.0f, 38.0f }, { 0.5f, 0.5f }, xColor);
	stageSelectStarIcon_ = BindStageSelectHUDSprite("stage_select_hud_star_icon", iconTextures[1], { 42.0f, rows[1] }, { 48.0f, 48.0f }, { 0.0f, 0.5f }, white);
	stageSelectStarXIcon_ = BindStageSelectHUDSprite("stage_select_hud_star_x", "Resources/sprite/ui/hud/xUi.png", { 94.0f, rows[1] }, { 38.0f, 38.0f }, { 0.5f, 0.5f }, xColor);
	stageSelectCoinIcon_ = BindStageSelectHUDSprite("stage_select_hud_coin_icon", iconTextures[2], { 42.0f, rows[2] }, { 48.0f, 48.0f }, { 0.0f, 0.5f }, white);
	stageSelectCoinXIcon_ = BindStageSelectHUDSprite("stage_select_hud_coin_x", "Resources/sprite/ui/hud/xUi.png", { 94.0f, rows[2] }, { 38.0f, 38.0f }, { 0.5f, 0.5f }, xColor);

	const float digitStartX = 124.0f;
	const float digitSpacing = 25.0f;
	const Vector2 digitSize = { 24.0f, 36.0f };
	for (int i = 0; i < 3; ++i) {
		const float x = digitStartX + digitSpacing * static_cast<float>(i);
		stageSelectCrownDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
			"stage_select_hud_crown_digit_" + std::to_string(i),
			"Resources/sprite/number/0.png",
			{ x, rows[0] },
			digitSize,
			{ 0.5f, 0.5f },
			crownNumber);
		stageSelectStarDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
			"stage_select_hud_star_digit_" + std::to_string(i),
			"Resources/sprite/number/0.png",
			{ x, rows[1] },
			digitSize,
			{ 0.5f, 0.5f },
			starNumber);
		stageSelectCoinDigits_[static_cast<size_t>(i)] = BindStageSelectHUDSprite(
			"stage_select_hud_coin_digit_" + std::to_string(i),
			"Resources/sprite/number/0.png",
			{ x, rows[2] },
			digitSize,
			{ 0.5f, 0.5f },
			coinNumber);
	}
}

void GameSelectScene::SetStageSelectHUDNumber(std::array<StageSelectHudSprite, 3>& digits, int value, const Vector4& color, bool visible) {
	value = std::clamp(value, 0, 999);
	const std::array<int, 3> digitValues = {
		value / 100,
		(value / 10) % 10,
		value % 10
	};
	const int digitCount = value >= 100 ? 3 : (value >= 10 ? 2 : 1);
	const int visibleStart = 3 - digitCount;

	for (int i = 0; i < 3; ++i) {
		StageSelectHudSprite& state = digits[static_cast<size_t>(i)];
		if (!state.sprite) {
			continue;
		}

		const bool digitVisible = visible && i >= visibleStart;
		state.sprite->SetVisible(digitVisible);
		if (digitVisible) {
			state.sprite->SetTextureHandle(Sprite::LoadTexture("number/" + std::to_string(digitValues[static_cast<size_t>(i)]) + ".png"));
			state.sprite->SetColor(color);
		}
		state.sprite->Update();
	}
}

void GameSelectScene::UpdateUI() {
	auto* save = GameDataManager::GetInstance();
	if (!save) {
		return;
	}

	const bool visible = true;
	const Vector4 crownNumber = { 1.0f, 0.94f, 0.50f, 1.0f };
	const Vector4 starNumber = { 1.0f, 0.90f, 0.30f, 1.0f };
	const Vector4 coinNumber = { 1.0f, 0.82f, 0.24f, 1.0f };
	const std::array<StageSelectHudSprite*, 6> icons = {
		&stageSelectCrownIcon_, &stageSelectCrownXIcon_,
		&stageSelectStarIcon_, &stageSelectStarXIcon_,
		&stageSelectCoinIcon_, &stageSelectCoinXIcon_
	};
	for (StageSelectHudSprite* state : icons) {
		if (state && state->sprite) {
			state->sprite->SetVisible(visible);
			state->sprite->Update();
		}
	}

	SetStageSelectHUDNumber(stageSelectCrownDigits_, GetDisplayedCrownCount(), crownNumber, visible);
	SetStageSelectHUDNumber(stageSelectStarDigits_, save->GetCollectedStarCoinCount(), starNumber, visible);
	SetStageSelectHUDNumber(stageSelectCoinDigits_, save->GetCoins(), coinNumber, visible);
	UpdateStageSelectCrownHudReward();
}

void GameSelectScene::UpdateStageGateSelection(float deltaTime) {
	stageSelectTime_ += deltaTime;

	if (stageDecisionCooldown_ > 0.0f) {
		stageDecisionCooldown_ -= deltaTime;
		if (stageDecisionCooldown_ < 0.0f) stageDecisionCooldown_ = 0.0f;
	}

	UpdateStageClearRewardPresentation(deltaTime);
	UpdateUnlockPresentation(deltaTime);

	if (gateEntryCinematicActive_) {
		ApplyStageGateStates();
		UpdateStageSelectDecorations(deltaTime);
		UpdateGatePrompt(nullptr, false, deltaTime);
		return;
	}

	if (gateReturnPresentationActive_) {
		ApplyStageGateStates();
		UpdateStageSelectDecorations(deltaTime);
		UpdateGatePrompt(nullptr, false, deltaTime);
		return;
	}

	if (crownCountPresentationActive_ || unlockingStageIndex_ >= 0) {
		ApplyStageGateStates();
		UpdateStageSelectDecorations(deltaTime);
		UpdateGatePrompt(nullptr, false, deltaTime);
		return;
	}

	float nearestDistance = std::numeric_limits<float>::max();
	Object3d* nearestGate = FindNearestStageGate(&nearestDistance);
	bool canEnterGate = false;
	if (nearestGate && nearestDistance <= gateSelectRadius_) {
		int gateStageIndex = GetStageGateIndex(nearestGate);
		if (gateStageIndex >= 0 && gateStageIndex < static_cast<int>(StageManager::GetInstance()->GetStages().size())) {
			selectedStageIndex_ = gateStageIndex;
			canEnterGate = IsStageUnlocked(gateStageIndex);
		}
	}

	ApplyStageGateStates();
	UpdateStageSelectDecorations(deltaTime);
	UpdateGatePrompt(nullptr, false, deltaTime);

	if (selectedStageIndex_ != previousSelectedStageIndex_) {
		const auto& stages = StageManager::GetInstance()->GetStages();
		if (selectedStageIndex_ >= 0 && selectedStageIndex_ < static_cast<int>(stages.size())) {
			const StageData& stage = stages[selectedStageIndex_];
			DebugConsole::GetInstance()->AddLog("Stage Select: " + stage.name);
		}
		previousSelectedStageIndex_ = selectedStageIndex_;
	}

	const bool decide = canEnterGate && IsPlayerTouchingStageGate(nearestGate);

	if (decide) {
		EnterSelectedStage();
	}
}

void GameSelectScene::UpdateGatePrompt(Object3d* nearestGate, bool canEnterGate, float deltaTime) {
	if (!gatePromptSprite_) {
		return;
	}

	if (!nearestGate || !canEnterGate || isChangingStage_ || !player_) {
		gatePromptSprite_->SetVisible(false);
		gatePromptSprite_->Update();
		return;
	}

	Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
	if (!camera) {
		gatePromptSprite_->SetVisible(false);
		gatePromptSprite_->Update();
		return;
	}

	Vector3 promptWorld = player_->GetWorldPosition();
	promptWorld.y += 1.35f;

	Math math;
	const Matrix4x4 viewProj = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
	const Vector3 ndc = Math::Transform(promptWorld, viewProj);
	if (ndc.z < 0.0f || ndc.z > 1.0f) {
		gatePromptSprite_->SetVisible(false);
		gatePromptSprite_->Update();
		return;
	}

	const float screenWidth = static_cast<float>(WinApp::kClientWidth);
	const float screenHeight = static_cast<float>(WinApp::kClientHeight);
	const float playerScreenX = (ndc.x + 1.0f) * 0.5f * screenWidth;
	const float playerScreenY = (1.0f - ndc.y) * 0.5f * screenHeight;
	const float baseWidth = 192.0f;
	const float baseHeight = 64.0f;
	float screenX = playerScreenX + 104.0f;
	float screenY = playerScreenY - 28.0f;

	gatePromptTimer_ += deltaTime;
	screenY += std::sin(gatePromptTimer_ * 4.0f) * 3.0f;
	if (screenX > screenWidth - baseWidth * 0.5f - 16.0f) {
		screenX = playerScreenX - 104.0f;
	}
	screenX = Math::Clamp(screenX, baseWidth * 0.5f + 12.0f, screenWidth - baseWidth * 0.5f - 12.0f);
	screenY = Math::Clamp(screenY, baseHeight * 0.5f + 12.0f, screenHeight - baseHeight * 0.5f - 18.0f);

	const float pulse = 1.0f + std::sin(gatePromptTimer_ * 5.5f) * 0.025f;
	gatePromptSprite_->SetVisible(true);
	gatePromptSprite_->SetPosition({ screenX, screenY });
	gatePromptSprite_->SetSize({ baseWidth * pulse, baseHeight * pulse });
	gatePromptSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.98f });
	gatePromptSprite_->Update();
}

void GameSelectScene::ApplyStageGateStates() {
	if (!objectManager_) {
		return;
	}

	float pulse = (std::sin(stageSelectTime_ * 5.0f) + 1.0f) * 0.5f;
	for (auto& object : objectManager_->GetObjects()) {
		if (!object || !IsStageGateObject(object.get())) {
			continue;
		}

		auto* gate = dynamic_cast<GimmickStageGate*>(object.get());
		int stageIndex = GetStageGateIndex(object.get());
		if (stageIndex < 0 || stageIndex >= static_cast<int>(StageManager::GetInstance()->GetStages().size())) {
			continue;
		}

		bool unlocked = IsStageUnlocked(stageIndex);
		bool cleared = GameDataManager::GetInstance()->IsStageCleared(stageIndex);
		bool unlocking = stageIndex == unlockingStageIndex_;
		bool selected = stageIndex == selectedStageIndex_;
		float gateActivation = unlocking ? 1.0f : 0.0f;
		if (player_) {
			const Vector3 playerPos = player_->GetWorldPosition();
			const Vector3 gatePos = object->GetWorldPosition();
			const float dx = gatePos.x - playerPos.x;
			const float dz = gatePos.z - playerPos.z;
			const float distance = std::sqrt(dx * dx + dz * dz);
			const float nearRadius = gateSelectRadius_ * 0.55f;
			const float wakeRadius = gateSelectRadius_ * 1.55f;
			gateActivation = 1.0f - Math::Clamp((distance - nearRadius) / std::max(wakeRadius - nearRadius, 0.001f), 0.0f, 1.0f);
			gateActivation = gateActivation * gateActivation * (3.0f - 2.0f * gateActivation);
			if (!unlocked) {
				gateActivation *= 0.18f;
			}
		}
		if ((gateEntryCinematicActive_ && object.get() == gateEntryTargetGate_) ||
			(gateReturnPresentationActive_ && object.get() == gateReturnTargetGate_)) {
			gateActivation = 1.0f;
			selected = true;
		}

		if (gate) {
			gate->SetGateState(selected, unlocked, cleared, unlocking);
			gate->SetGateActivation(gateActivation);
			continue;
		}

		object->SetIsVisible(true);
		object->SetCollisionAttribute(unlocked ? CollisionAttribute::kTrigger : 0);
		object->SetCollisionMask(unlocked ? CollisionAttribute::kPlayer : 0);
		object->SetColliderType(ColliderType::kSphere);
		object->SetCollisionRadius(gateSelectRadius_ * 0.55f);
		object->SetColor(GetStageGateModelColor(unlocked, selected, cleared, unlocking, pulse));
		object->SetEmissive(GetStageGateModelEmissive(unlocked, selected, cleared, unlocking, pulse));
	}
}

void GameSelectScene::RefreshDebugStageStates() {
	const auto& stages = StageManager::GetInstance()->GetStages();
	if (stages.empty()) {
		return;
	}

	if (selectedStageIndex_ < 0 || selectedStageIndex_ >= static_cast<int>(stages.size()) ||
		!IsStageUnlocked(selectedStageIndex_)) {
		selectedStageIndex_ = 0;
		for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
			if (IsStageUnlocked(stageIndex)) {
				selectedStageIndex_ = stageIndex;
				break;
			}
		}
	}

	previousSelectedStageIndex_ = -1;
	unlockingStageIndex_ = FindPendingUnlockStage();
	unlockPresentationTimer_ = 0.0f;
	unlockParticleTimer_ = 0.0f;
	ApplyStageGateStates();
	UpdateStageSelectDecorations(0.0f);
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

	const bool firstFrame = unlockPresentationTimer_ <= 0.0f;
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
		crown->SetGPUParticleName(kCrownIdleParticlePreset);
		crown->SetMeshEffect1Name(kCrownAuraEffectPath);
		crown->SetMeshEffect2Name(kCrownRayEffectPath);
	}

	if (firstFrame && crown) {
		const Vector3 crownPos = crown->GetWorldPosition();
		const Vector3 flashPos = crownPos + Vector3{ 0.0f, 0.75f, 0.0f };
		const Vector3 burstPos = crownPos + Vector3{ 0.0f, 1.18f, 0.0f };
		MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockFlashRingEffectPath, flashPos, { 0.0f, 0.0f, 0.0f });
		MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockRayFlashEffectPath, burstPos, { 0.0f, 0.0f, 0.0f });
		GPUParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, burstPos);
		GPUParticleManager::GetInstance()->Emit(kCrownUnlockRayParticlePreset, burstPos);
		GPUParticleManager::GetInstance()->Emit(kCrownUnlockFountainParticlePreset, burstPos);
	}

	if (unlockParticleTimer_ <= 0.0f && particleSystem_) {
		unlockParticleTimer_ = 0.18f;
		Vector3 up = { 0.0f, 1.0f, 0.0f };
		Vector3 nodePos = GetStageNodePosition(unlockingStageIndex_);
		if (crown) {
			GPUParticleManager::GetInstance()->Emit(
				kCrownUnlockParticlePreset,
				crown->GetWorldPosition() + Vector3{ 0.0f, 1.15f, 0.0f }
			);
			GPUParticleManager::GetInstance()->Emit(
				kCrownUnlockFountainParticlePreset,
				crown->GetWorldPosition() + Vector3{ 0.0f, 1.20f, 0.0f }
			);
			particleSystem_->SpawnParticles(
				crown->GetWorldPosition() + Vector3{ 0.0f, 1.6f, 0.0f },
				4,
				4.7f,
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
		crown->SetGPUParticleName(kCrownIdleParticlePreset);
		crown->SetMeshEffect1Name(kCrownAuraEffectPath);
		crown->SetMeshEffect2Name(kCrownRayEffectPath);
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
	UpdateStageClearCrownDisplays(deltaTime);
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

Object3d* GameSelectScene::EnsureStageClearCrown(int stageIndex) {
	if (!objectManager_ || !object3dCommon_) {
		return nullptr;
	}

	const std::string name = MakeStageClearCrownName(stageIndex);
	if (Object3d* existing = FindObjectByName(name)) {
		return existing;
	}

	auto crown = std::make_unique<Object3d>();
	crown->Initialize(object3dCommon_.get());
	crown->SetModel(kStageClearCrownModel);
	crown->SetName(name);
	crown->SetClassName("EditorOnly");
	crown->SetSaveCategory("Object");
	crown->SetIsLocked(true);
	crown->SetTargetID(stageIndex);
	crown->SetColliderType(ColliderType::kNone);
	crown->SetCollisionAttribute(0);
	crown->SetCollisionMask(0);
	crown->SetScale({ kStageClearCrownBaseScale, kStageClearCrownBaseScale, kStageClearCrownBaseScale });
	crown->SetColor({ 0.58f, 0.62f, 0.70f, 1.0f });
	crown->SetEmissive(0.45f);
	crown->SetGPUParticleName(kCrownIdleParticlePreset);
	crown->SetMeshEffect1Name("");
	crown->SetMeshEffect2Name("");
	crown->SetTranslate(GetStageClearCrownPosition(stageIndex));
	crown->UpdateLocalMatrix();
	crown->UpdateWorldMatrix();

	Object3d* raw = crown.get();
	objectManager_->GetObjects().push_back(std::move(crown));
	return raw;
}

void GameSelectScene::UpdateStageClearCrownDisplays(float deltaTime) {
	const auto& stages = StageManager::GetInstance()->GetStages();
	auto* save = GameDataManager::GetInstance();
	const float pulse = (std::sin(stageSelectTime_ * 4.2f) + 1.0f) * 0.5f;

	for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
		Object3d* crown = EnsureStageClearCrown(stageIndex);
		if (!crown) {
			continue;
		}

		const bool cleared = save->IsStageCleared(stageIndex);
		crown->SetIsVisible(cleared);
		crown->SetCollisionAttribute(0);
		crown->SetCollisionMask(0);
		if (!cleared) {
			continue;
		}

		if (crownCountPresentationActive_ && stageIndex == crownCountPresentationStageIndex_) {
			UpdateStageClearRewardCrown(crown, stageIndex, deltaTime);
			continue;
		}

		Transform* transform = crown->GetTransform();
		const float bob = std::sin(stageSelectTime_ * 2.6f + static_cast<float>(stageIndex) * 0.75f) * 0.10f;
		Vector3 targetPos = GetStageClearCrownPosition(stageIndex);
		targetPos.y += bob;
		transform->translate = targetPos;
		transform->rotate.y += deltaTime * 1.15f;
		transform->rotate.z = std::sin(stageSelectTime_ * 2.0f + static_cast<float>(stageIndex)) * 0.08f;
		transform->isQuaternionMaster = false;

		const float scale = kStageClearCrownBaseScale * (1.0f + pulse * 0.08f);
		transform->scale = { scale, scale, scale };
		crown->SetColor({ 0.58f + pulse * 0.06f, 0.62f + pulse * 0.06f, 0.70f + pulse * 0.07f, 1.0f });
		crown->SetEmissive(0.38f + pulse * 0.16f);
		crown->SetGPUParticleName(kCrownIdleParticlePreset);
		crown->SetMeshEffect1Name("");
		crown->SetMeshEffect2Name("");
		crown->UpdateLocalMatrix();
		crown->UpdateWorldMatrix();
	}
}

void GameSelectScene::StartStageClearRewardPresentation(const GameDataManager::StageClearRewardPresentation& request) {
	const auto& stages = StageManager::GetInstance()->GetStages();
	if (!request.active ||
		request.stageIndex < 0 ||
		request.stageIndex >= static_cast<int>(stages.size())) {
		return;
	}

	crownCountPresentationActive_ = true;
	crownCountPresentationImpactDone_ = false;
	crownCountPresentationStageIndex_ = request.stageIndex;
	crownCountPresentationFrom_ = std::clamp(request.previousCrownCount, 0, 999);
	crownCountPresentationTo_ = std::clamp(request.newCrownCount, 0, 999);
	crownCountPresentationTimer_ = 0.0f;
	crownCountPresentationParticleTimer_ = 0.0f;
	selectedStageIndex_ = crownCountPresentationStageIndex_;

	DebugConsole::GetInstance()->AddLog("Stage Select: crown count presentation start.");
}

void GameSelectScene::UpdateStageClearRewardPresentation(float deltaTime) {
	if (!crownCountPresentationActive_) {
		return;
	}

	crownCountPresentationTimer_ += deltaTime;
	crownCountPresentationParticleTimer_ -= deltaTime;
	selectedStageIndex_ = crownCountPresentationStageIndex_;

	Object3d* crown = EnsureStageClearCrown(crownCountPresentationStageIndex_);
	const Vector3 targetPos = GetStageClearCrownPosition(crownCountPresentationStageIndex_);
	const Vector3 crownPos = crown ? crown->GetWorldPosition() : targetPos;
	const Vector3 burstPos = (crownCountPresentationTimer_ < kCrownCountImpactTime ? crownPos : targetPos) + Vector3{ 0.0f, 1.05f, 0.0f };

	if (crownCountPresentationParticleTimer_ <= 0.0f) {
		crownCountPresentationParticleTimer_ = kCrownCountParticleInterval;
		GPUParticleManager::GetInstance()->Emit(kCrownUnlockFountainParticlePreset, burstPos);
		if (crownCountPresentationTimer_ >= kCrownCountImpactTime) {
			GPUParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, burstPos);
		}
	}

	if (!crownCountPresentationImpactDone_ &&
		crownCountPresentationTimer_ >= kCrownCountImpactTime) {
		crownCountPresentationImpactDone_ = true;
		MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockFlashRingEffectPath, targetPos + Vector3{ 0.0f, 0.45f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.15f, 1.0f, 1.15f });
		MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockRayFlashEffectPath, burstPos, { 0.0f, 0.0f, 0.0f }, { 1.05f, 1.05f, 1.05f });
		GPUParticleManager::GetInstance()->Emit(kCrownUnlockParticlePreset, burstPos);
		GPUParticleManager::GetInstance()->Emit(kCrownUnlockRayParticlePreset, burstPos);
		GPUParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, burstPos);
		GameAudioSettings::GetInstance()->PlaySE("crown_get", 0.95f);

		if (particleSystem_) {
			Vector3 up = { 0.0f, 1.0f, 0.0f };
			particleSystem_->SpawnParticles(
				burstPos,
				12,
				6.0f,
				&up,
				1.35f,
				{ 1.0f, 0.88f, 0.24f, 1.0f },
				{ 1.0f, 0.98f, 0.58f, 0.0f },
				0.34f,
				0.82f,
				0.58f,
				0.04f
			);
		}
	}

	if (crownCountPresentationTimer_ >= kCrownCountPresentationDuration) {
		crownCountPresentationActive_ = false;
		crownCountPresentationStageIndex_ = -1;
		crownCountPresentationTimer_ = 0.0f;
		crownCountPresentationParticleTimer_ = 0.0f;
		crownCountPresentationImpactDone_ = false;

		unlockingStageIndex_ = FindPendingUnlockStage();
		if (unlockingStageIndex_ >= 0) {
			selectedStageIndex_ = unlockingStageIndex_;
			unlockPresentationTimer_ = 0.0f;
			unlockParticleTimer_ = 0.0f;
			DebugConsole::GetInstance()->AddLog("Stage Select: crown unlock presentation start.");
		}
	}
}

void GameSelectScene::UpdateStageClearRewardCrown(Object3d* crown, int stageIndex, float deltaTime) {
	if (!crown) {
		return;
	}

	const Vector3 targetPos = GetStageClearCrownPosition(stageIndex);
	Transform* transform = crown->GetTransform();

	Vector3 position = targetPos;
	float scale = kStageClearCrownBaseScale;
	float emissive = 3.1f;
	float silverFlash = 0.0f;
	const float t = crownCountPresentationTimer_;

	if (t < kCrownCountImpactTime) {
		const float p = SelectClamp01(t / kCrownCountImpactTime);
		const float e = SelectEaseOutCubic(p);
		const Vector3 startPos = targetPos + Vector3{ 0.0f, 4.35f, 0.0f };
		position = SelectLerp(startPos, targetPos, e);
		position.y += std::sin(p * std::numbers::pi_v<float>) * 0.62f;
		scale = kStageClearCrownBaseScale * (0.18f + SelectEaseOutBack(p) * 0.92f);
		emissive = 2.6f + p * 2.8f;
		silverFlash = p;
		transform->rotate.y += deltaTime * (8.0f + (1.0f - p) * 7.5f);
		transform->rotate.z = std::sin(p * std::numbers::pi_v<float>) * 0.42f;
	} else {
		const float p = t - kCrownCountImpactTime;
		const float decay = std::max(0.0f, 1.0f - p / 1.35f);
		const float bounce = std::abs(std::sin(p * 11.0f)) * 0.24f * decay;
		const float pop = std::sin(SelectClamp01(p / 0.42f) * std::numbers::pi_v<float>);
		position.y += bounce;
		scale = kStageClearCrownBaseScale * (1.0f + pop * 0.34f + std::sin(p * 14.0f) * 0.08f * decay);
		emissive = 2.55f + pop * 2.2f + decay * 0.9f;
		silverFlash = std::max(pop, decay * 0.45f);
		transform->rotate.y += deltaTime * (2.0f + decay * 4.5f);
		transform->rotate.z = std::sin(stageSelectTime_ * 5.0f) * 0.12f * std::max(0.25f, decay);
	}

	transform->translate = position;
	transform->scale = { scale, scale, scale };
	transform->isQuaternionMaster = false;
	crown->SetIsVisible(true);
	silverFlash = SelectClamp01(silverFlash);
	crown->SetColor({ 0.58f + silverFlash * 0.30f, 0.62f + silverFlash * 0.28f, 0.70f + silverFlash * 0.24f, 1.0f });
	crown->SetEmissive(emissive);
	crown->SetGPUParticleName(kCrownIdleParticlePreset);
	crown->SetMeshEffect1Name("");
	crown->SetMeshEffect2Name("");
	crown->UpdateLocalMatrix();
	crown->UpdateWorldMatrix();
}

void GameSelectScene::UpdateStageSelectCrownHudReward() {
	auto applySprite = [](StageSelectHudSprite& state, float scale, const Vector4& color, float rotation) {
		if (!state.sprite) {
			return;
		}

		state.sprite->SetPosition(state.basePosition);
		state.sprite->SetSize({ state.baseSize.x * scale, state.baseSize.y * scale });
		state.sprite->SetColor(color);
		state.sprite->SetRotation(rotation);
		state.sprite->Update();
	};

	if (!crownCountPresentationActive_) {
		applySprite(stageSelectCrownIcon_, 1.0f, stageSelectCrownIcon_.baseColor, 0.0f);
		applySprite(stageSelectCrownXIcon_, 1.0f, stageSelectCrownXIcon_.baseColor, 0.0f);
		for (StageSelectHudSprite& digit : stageSelectCrownDigits_) {
			applySprite(digit, 1.0f, digit.baseColor, 0.0f);
		}
		return;
	}

	const float t = crownCountPresentationTimer_;
	const float impactAge = std::max(0.0f, t - kCrownCountImpactTime);
	const float prePulse = std::sin(stageSelectTime_ * 11.0f) * 0.035f;
	const float pop = crownCountPresentationImpactDone_
		? std::sin(SelectClamp01(impactAge / 0.52f) * std::numbers::pi_v<float>) * 0.42f
		: 0.0f;
	const float glow = crownCountPresentationImpactDone_
		? std::max(0.0f, 1.0f - impactAge / 1.15f)
		: 0.0f;
	const Vector4 hotCrown = { 1.0f, 0.88f + glow * 0.10f, 0.22f + glow * 0.34f, 1.0f };
	const Vector4 hotNumber = { 1.0f, 0.92f + glow * 0.08f, 0.36f + glow * 0.32f, 1.0f };

	applySprite(stageSelectCrownIcon_, 1.0f + prePulse + pop * 0.62f, hotCrown, std::sin(stageSelectTime_ * 7.0f) * 0.06f * (1.0f + glow));
	applySprite(stageSelectCrownXIcon_, 1.0f + pop * 0.24f, hotNumber, 0.0f);
	for (StageSelectHudSprite& digit : stageSelectCrownDigits_) {
		applySprite(digit, 1.0f + pop, hotNumber, 0.0f);
	}
}

int GameSelectScene::GetDisplayedCrownCount() const {
	if (crownCountPresentationActive_ &&
		!crownCountPresentationImpactDone_ &&
		crownCountPresentationTimer_ < kCrownCountImpactTime) {
		return crownCountPresentationFrom_;
	}
	return crownCountPresentationActive_
		? crownCountPresentationTo_
		: GameDataManager::GetInstance()->GetClearedStageCount();
}

Vector3 GameSelectScene::GetStageClearCrownPosition(int stageIndex) const {
	Vector3 fallback = GetStageNodePosition(stageIndex) + Vector3{ 0.0f, 3.0f, 0.0f };
	if (!objectManager_) {
		return fallback;
	}

	for (const auto& object : objectManager_->GetObjects()) {
		if (!object || !IsStageGateObject(object.get()) || GetStageGateIndex(object.get()) != stageIndex) {
			continue;
		}

		AABB gateAabb = object->GetModelWorldAABB();
		const bool validAabb =
			std::isfinite(gateAabb.min.x) && std::isfinite(gateAabb.min.y) && std::isfinite(gateAabb.min.z) &&
			std::isfinite(gateAabb.max.x) && std::isfinite(gateAabb.max.y) && std::isfinite(gateAabb.max.z) &&
			gateAabb.max.y > gateAabb.min.y;
		if (!validAabb) {
			return fallback;
		}

		return {
			(gateAabb.min.x + gateAabb.max.x) * 0.5f,
			gateAabb.max.y + kStageClearCrownHeightOffset,
			(gateAabb.min.z + gateAabb.max.z) * 0.5f
		};
	}

	return fallback;
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
			if (object && IsStageGateObject(object.get()) && GetStageGateIndex(object.get()) == stageIndex) {
				return object->GetWorldPosition();
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
	StartGateEntryCinematic(selectedStageIndex_);
}

void GameSelectScene::CaptureGateEntryMaterialState(Object3d* rootObject) {
	gateEntryMaterialSnapshots_.clear();
	if (!rootObject) {
		return;
	}

	std::vector<Object3d*> stack;
	stack.push_back(rootObject);
	while (!stack.empty()) {
		Object3d* object = stack.back();
		stack.pop_back();
		if (!object) {
			continue;
		}

		GateEntryMaterialSnapshot snapshot;
		snapshot.object = object;
		snapshot.color = object->GetColor();
		snapshot.materialType = object->GetMaterialType();
		snapshot.emissive = object->GetEmissive();
		if (auto* material = object->GetMaterialData()) {
			snapshot.portalClipEnabled = material->portalClipEnabled;
			snapshot.portalClipProgress = material->portalClipProgress;
			snapshot.portalClipCenter = material->portalClipCenter;
			snapshot.portalClipEdgeWidth = material->portalClipEdgeWidth;
			snapshot.portalClipNormal = material->portalClipNormal;
			snapshot.portalClipDissolve = material->portalClipDissolve;
			snapshot.portalClipColor = material->portalClipColor;
		}
		gateEntryMaterialSnapshots_.push_back(snapshot);

		for (Object3d* child : object->GetChildren()) {
			if (child) {
				stack.push_back(child);
			}
		}
	}
}

void GameSelectScene::ApplyGateEntryDissolveMaterial(float progress, const Vector3& gatePosition, const Vector3& direction) {
	const float dissolveProgress = SelectClamp01(progress);
	Vector3 dissolveDirection = direction;
	const float directionLength = std::sqrt(
		direction.x * direction.x +
		direction.y * direction.y +
		direction.z * direction.z);
	if (directionLength > 0.0001f) {
		dissolveDirection.x /= directionLength;
		dissolveDirection.y /= directionLength;
		dissolveDirection.z /= directionLength;
	} else {
		dissolveDirection = { 0.0f, 0.0f, 1.0f };
	}
	// ゲートに入り始めた瞬間から輪郭を焼き、透明化は後半だけに寄せて削れて消える印象を強める。
	const float clipProgress = SelectClamp01(0.14f + SelectEaseOutCubic(dissolveProgress) * 0.86f);
	const float edgePeak = 1.0f - std::abs(clipProgress * 2.0f - 1.0f);
	const float fadeProgress = SelectEaseInOutCubic(SelectClamp01((clipProgress - 0.72f) / 0.28f));
	const float heat = SelectClamp01(0.08f + clipProgress * 0.18f + edgePeak * 0.30f);
	const float edgeWidth = 0.30f - 0.16f * clipProgress;
	const float emissive = 1.25f + clipProgress * 0.95f + edgePeak * 1.45f;
	const float dissolveNoise = 0.74f - 0.18f * clipProgress;
	const Vector4 portalColor = { 1.0f, 0.66f, 0.26f, 0.94f };

	for (const GateEntryMaterialSnapshot& snapshot : gateEntryMaterialSnapshots_) {
		Object3d* object = snapshot.object;
		if (!object) {
			continue;
		}

		const Vector4 baseColor = snapshot.color;
		const Vector4 dissolveColor = {
			baseColor.x * (1.0f - heat) + 1.0f * heat,
			baseColor.y * (1.0f - heat) + 0.76f * heat,
			baseColor.z * (1.0f - heat) + 0.34f * heat,
			1.0f - fadeProgress
		};

		object->SetMaterialType(4);
		object->SetBlendMode(BlendMode::kNormal);
		object->SetEmissive(std::max(snapshot.emissive, emissive));
		object->SetColor(dissolveColor);

		if (auto* material = object->GetMaterialData()) {
			material->portalClipEnabled = 1.0f;
			material->portalClipProgress = clipProgress;
			material->portalClipCenter = gatePosition;
			material->portalClipNormal = dissolveDirection;
			material->portalClipEdgeWidth = edgeWidth;
			material->portalClipDissolve = dissolveNoise;
			material->portalClipColor = portalColor;
		}
	}
}

void GameSelectScene::RestoreGateEntryMaterialState() {
	for (const GateEntryMaterialSnapshot& snapshot : gateEntryMaterialSnapshots_) {
		Object3d* object = snapshot.object;
		if (!object) {
			continue;
		}

		object->SetMaterialType(snapshot.materialType);
		object->SetEmissive(snapshot.emissive);
		object->SetColor(snapshot.color);

		if (auto* material = object->GetMaterialData()) {
			material->portalClipEnabled = snapshot.portalClipEnabled;
			material->portalClipProgress = snapshot.portalClipProgress;
			material->portalClipCenter = snapshot.portalClipCenter;
			material->portalClipEdgeWidth = snapshot.portalClipEdgeWidth;
			material->portalClipNormal = snapshot.portalClipNormal;
			material->portalClipDissolve = snapshot.portalClipDissolve;
			material->portalClipColor = snapshot.portalClipColor;
		}
	}
	gateEntryMaterialSnapshots_.clear();
}

void GameSelectScene::UpdateGateCinematicBars(float deltaTime) {
	PostEffect::Params* params = PostEffect::GetInstance()->GetParams();
	if (!params) {
		return;
	}

	const bool isCinematicActive = gateEntryCinematicActive_ || gateReturnPresentationActive_;
	if (isCinematicActive && !gateCinematicBarOverrideActive_) {
		gateCinematicBarBaseHeight_ = params->cinemaBarHeight;
		gateCinematicBarOverrideActive_ = true;
	}

	if (!gateCinematicBarOverrideActive_) {
		return;
	}

	const float targetBlend = isCinematicActive ? 1.0f : 0.0f;
	const float speed = isCinematicActive ? kGateCinematicBarOpenSpeed : kGateCinematicBarCloseSpeed;
	const float step = speed * std::max(deltaTime, 0.0f);
	if (gateCinematicBarBlend_ < targetBlend) {
		gateCinematicBarBlend_ = std::min(targetBlend, gateCinematicBarBlend_ + step);
	} else if (gateCinematicBarBlend_ > targetBlend) {
		gateCinematicBarBlend_ = std::max(targetBlend, gateCinematicBarBlend_ - step);
	}

	const float targetHeight = std::max(gateCinematicBarBaseHeight_, kGateCinematicBarHeight);
	params->cinemaBarHeight = gateCinematicBarBaseHeight_ + (targetHeight - gateCinematicBarBaseHeight_) * gateCinematicBarBlend_;

	if (!isCinematicActive && gateCinematicBarBlend_ <= 0.0f) {
		params->cinemaBarHeight = gateCinematicBarBaseHeight_;
		gateCinematicBarBaseHeight_ = 0.0f;
		gateCinematicBarOverrideActive_ = false;
	}
}

void GameSelectScene::ResetGateCinematicBars() {
	PostEffect::Params* params = PostEffect::GetInstance()->GetParams();
	if (params && gateCinematicBarOverrideActive_) {
		params->cinemaBarHeight = gateCinematicBarBaseHeight_;
	}
	gateCinematicBarOverrideActive_ = false;
	gateCinematicBarBlend_ = 0.0f;
	gateCinematicBarBaseHeight_ = 0.0f;
}

bool GameSelectScene::StartStageReturnPresentation(const GameDataManager::StageSelectReturnPresentation& request) {
	if (!request.active || gateEntryCinematicActive_) {
		return false;
	}
	const auto& stages = StageManager::GetInstance()->GetStages();
	if (request.stageIndex < 0 || request.stageIndex >= static_cast<int>(stages.size())) {
		return false;
	}

	gateReturnTargetGate_ = FindStageGateByIndex(request.stageIndex);
	if (!gateReturnTargetGate_ || !player_) {
		return false;
	}

	const Vector3 gatePos = gateReturnTargetGate_->GetWorldPosition();
	const Vector3 selectPlayerStart = player_->GetWorldPosition();
	const Vector3 stageNodePosition = GetStageNodePosition(request.stageIndex);
	const Vector3 fallbackToGate = {
		gatePos.x - stageNodePosition.x,
		0.0f,
		gatePos.z - stageNodePosition.z
	};
	const float fallbackLengthSq = fallbackToGate.x * fallbackToGate.x + fallbackToGate.z * fallbackToGate.z;
	const Vector3 fallbackFromPlayer = {
		gatePos.x - selectPlayerStart.x,
		0.0f,
		gatePos.z - selectPlayerStart.z
	};
	const Vector3 gateDirection = SelectGateEntryDirection(
		gateReturnTargetGate_,
		fallbackLengthSq > 0.0001f ? fallbackToGate : fallbackFromPlayer
	);
	const Vector3 outwardDirection = SelectNormalizeGateDirectionXZ(
		{ -gateDirection.x, 0.0f, -gateDirection.z },
		{ 0.0f, 0.0f, -1.0f }
	);
	const SelectGateEntryRoute probeRoute = SelectBuildGateEntryRoute(gatePos, gateDirection, selectPlayerStart.y);
	const Vector3 landingProbePosition = {
		probeRoute.surface.x + outwardDirection.x * kGateReturnExitForwardOffset,
		selectPlayerStart.y,
		probeRoute.surface.z + outwardDirection.z * kGateReturnExitForwardOffset
	};
	const float returnGroundY = SelectResolveGateReturnGroundY(landingProbePosition, gatePos, stageNodePosition, selectPlayerStart, gateReturnTargetGate_);
	// 出現開始はゲートの見た目中心に合わせ、着地先だけを床レイキャストのYに分離します。
	const float gateExitY = gatePos.y;
	const SelectGateEntryRoute route = SelectBuildGateEntryRoute(gatePos, gateDirection, gateExitY);

	gateReturnStageIndex_ = request.stageIndex;
	selectedStageIndex_ = request.stageIndex;
	previousSelectedStageIndex_ = -1;
	unlockingStageIndex_ = -1;
	gateReturnDirection_ = gateDirection;
	gateReturnInsidePlayerPosition_ = route.inside;
	gateReturnCenterPlayerPosition_ = route.center;
	gateReturnSurfacePlayerPosition_ = route.surface;
	gateReturnEndPlayerPosition_ = {
		route.surface.x + outwardDirection.x * kGateReturnExitForwardOffset,
		returnGroundY,
		route.surface.z + outwardDirection.z * kGateReturnExitForwardOffset
	};
	gateReturnPlayerScale_ = player_->GetScale();
	gateReturnHadPlayerControl_ = player_->IsControlActive();

	PlayerGateReturnAnimation::Route returnRoute;
	returnRoute.start = gateReturnInsidePlayerPosition_;
	returnRoute.gateCenter = gateReturnCenterPlayerPosition_;
	returnRoute.end = gateReturnEndPlayerPosition_;
	returnRoute.direction = outwardDirection;
	returnRoute.baseScale = gateReturnPlayerScale_;
	player_->StartGateReturnAnimation(returnRoute);

	if (auto* stageGate = dynamic_cast<GimmickStageGate*>(gateReturnTargetGate_)) {
		stageGate->TriggerEntryReaction();
	}

	if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
		const Vector3 side = { gateDirection.z, 0.0f, -gateDirection.x };
		const Vector3 gateFocus = {
			gatePos.x,
			returnGroundY + kGateReturnCameraFocusHeight,
			gatePos.z
		};
		const Vector3 landingFocus = {
			gateReturnEndPlayerPosition_.x,
			gateReturnEndPlayerPosition_.y + kGateReturnCameraFocusHeight,
			gateReturnEndPlayerPosition_.z
		};
		// ゲート真正面だけを見ると、着地後のプレイヤーが画面外やゲート裏へ逃げやすいです。
		// 注視点を着地点寄りにし、少し斜めから見ることでゲートから出る動きと着地を同時に見せます。
		const Vector3 focus = SelectLerpVector3(gateFocus, landingFocus, kGateReturnCameraLandingFocusRate);
		const Vector3 restoreTarget = {
			gateReturnEndPlayerPosition_.x,
			gateReturnEndPlayerPosition_.y + kGateReturnThirdPersonTargetHeight,
			gateReturnEndPlayerPosition_.z
		};
		const Vector3 restoreEye = {
			restoreTarget.x - outwardDirection.x * kGateReturnThirdPersonDistance,
			restoreTarget.y + kGateReturnThirdPersonHeight,
			restoreTarget.z - outwardDirection.z * kGateReturnThirdPersonDistance
		};
		gateReturnCameraStartEye_ = camera->GetEye();
		gateReturnCameraStartTarget_ = camera->GetTargetPoint();
		gateReturnCameraRestoreEye_ = restoreEye;
		gateReturnCameraRestoreTarget_ = restoreTarget;
		gateReturnCameraEndTarget_ = focus;
		gateReturnCameraEndEye_ = {
			focus.x - gateDirection.x * kGateReturnCameraBackDistance + side.x * kGateReturnCameraSideOffset,
			focus.y + kGateReturnCameraHeight,
			focus.z - gateDirection.z * kGateReturnCameraBackDistance + side.z * kGateReturnCameraSideOffset
		};
		camera->SetInputEnabled(false);
		camera->SetFollowTarget(nullptr);
		camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
		camera->ConfigFixedPoint(gateReturnCameraStartEye_, SelectLookAtRotation(gateReturnCameraStartEye_, gateReturnCameraStartTarget_));
	}

	gateReturnPresentationActive_ = true;
	gateReturnPresentationTimer_ = 0.0f;
	gateReturnLandingImpulsePlayed_ = false;
	gateReturnRecoveryImpulsePlayed_ = false;
	stageDecisionCooldown_ = kGateReturnTotalDuration;
	if (gatePromptSprite_) {
		gatePromptSprite_->SetVisible(false);
		gatePromptSprite_->Update();
	}
	DebugConsole::GetInstance()->AddLog("Stage Select: gate return presentation start.");
	return true;
}

void GameSelectScene::UpdateStageReturnPresentation(float deltaTime) {
	if (!gateReturnPresentationActive_) {
		return;
	}

	gateReturnPresentationTimer_ += deltaTime;
	const Vector3 outwardDirection = SelectNormalizeGateDirectionXZ(
		{ -gateReturnDirection_.x, 0.0f, -gateReturnDirection_.z },
		{ 0.0f, 0.0f, -1.0f }
	);

	if (gateReturnTargetGate_) {
		const Vector3 gatePos = gateReturnTargetGate_->GetWorldPosition();
		const Vector3 stageNodePosition = gateReturnStageIndex_ >= 0 ? GetStageNodePosition(gateReturnStageIndex_) : gateReturnEndPlayerPosition_;
		const float returnGroundY = SelectResolveGateReturnGroundY(gateReturnEndPlayerPosition_, gatePos, stageNodePosition, gateReturnEndPlayerPosition_, gateReturnTargetGate_);
		// 出現開始はゲートの見た目中心、着地先は床レイキャストで分けます。
		const float gateExitY = gatePos.y;
		const SelectGateEntryRoute route = SelectBuildGateEntryRoute(gatePos, gateReturnDirection_, gateExitY);
		gateReturnInsidePlayerPosition_ = route.inside;
		gateReturnCenterPlayerPosition_ = route.center;
		gateReturnSurfacePlayerPosition_ = route.surface;
		gateReturnEndPlayerPosition_ = {
			route.surface.x + outwardDirection.x * kGateReturnExitForwardOffset,
			returnGroundY,
			route.surface.z + outwardDirection.z * kGateReturnExitForwardOffset
		};
	}

	if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
		camera->SetInputEnabled(false);
		camera->SetFollowTarget(nullptr);
		camera->SetFollowMode(Camera::FollowMode::kFixedPoint);

		Vector3 eye = gateReturnCameraEndEye_;
		Vector3 target = gateReturnCameraEndTarget_;
		if (gateReturnPresentationTimer_ < kGateReturnCameraRestoreStartTime) {
			const float introT = SelectEaseOutCubic(SelectClamp01(gateReturnPresentationTimer_ / kGateReturnCameraIntroDuration));
			eye = SelectLerpVector3(gateReturnCameraStartEye_, gateReturnCameraEndEye_, introT);
			target = SelectLerpVector3(gateReturnCameraStartTarget_, gateReturnCameraEndTarget_, introT);
		} else {
			const float restoreT = SelectEaseInOutCubic(SelectClamp01(
				(gateReturnPresentationTimer_ - kGateReturnCameraRestoreStartTime) / kGateReturnCameraRestoreDuration
			));
			eye = SelectLerpVector3(gateReturnCameraEndEye_, gateReturnCameraRestoreEye_, restoreT);
			target = SelectLerpVector3(gateReturnCameraEndTarget_, gateReturnCameraRestoreTarget_, restoreT);
		}
		camera->ConfigFixedPoint(eye, SelectLookAtRotation(eye, target));
	}

	if (player_) {
		player_->SetIsVisible(true);
		player_->SetIsControlActive(false);
		player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
	}

	const bool playerAnimationFinished = player_ && player_->IsGateReturnAnimationFinished();
	if (playerAnimationFinished || gateReturnPresentationTimer_ >= kGateReturnTotalDuration) {
		FinishStageReturnPresentation();
	}
}

void GameSelectScene::FinishStageReturnPresentation() {
	gateReturnPresentationActive_ = false;
	gateReturnPresentationTimer_ = 0.0f;
	gateReturnStageIndex_ = -1;
	gateReturnTargetGate_ = nullptr;
	stageDecisionCooldown_ = 0.25f;

	if (player_) {
		player_->StopGateReturnAnimation(gateReturnHadPlayerControl_);
		player_->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::Idle);
		player_->TriggerSlimeImpulse({ 0.96f, 1.08f, 0.96f }, 0.18f);
	}
	if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
		if (player_) {
			camera->SetFollowTarget(player_);
			camera->SetFollowMode(Camera::FollowMode::kAimable);
			camera->SnapToThirdPerson(kGateReturnThirdPersonDistance, kGateReturnThirdPersonHeight, kGateReturnThirdPersonPitch);
		}
		camera->SetInputEnabled(true);
	}

	if (pendingStageClearRewardPresentation_.active) {
		StartStageClearRewardPresentation(pendingStageClearRewardPresentation_);
		pendingStageClearRewardPresentation_ = {};
	} else {
		unlockingStageIndex_ = FindPendingUnlockStage();
		if (unlockingStageIndex_ >= 0) {
			selectedStageIndex_ = unlockingStageIndex_;
			DebugConsole::GetInstance()->AddLog("Stage Select: crown unlock presentation start.");
		}
	}
	DebugConsole::GetInstance()->AddLog("Stage Select: gate return presentation finished.");
}

void GameSelectScene::StartGateEntryCinematic(int stageIndex) {
	if (gateEntryCinematicActive_) {
		return;
	}

	gateEntryTargetGate_ = FindNearestStageGate(nullptr);
	gateEntryStartPlayerPosition_ = player_ ? player_->GetWorldPosition() : Vector3{};
	gateEntryStartPlayerScale_ = player_ ? player_->GetScale() : Vector3{ 1.0f, 1.0f, 1.0f };
	gateEntryTargetPlayerPosition_ = gateEntryStartPlayerPosition_;
	gateEntrySurfacePlayerPosition_ = gateEntryStartPlayerPosition_;
	gateEntryInsidePlayerPosition_ = gateEntryStartPlayerPosition_;
	gateEntryDirection_ = { 0.0f, 0.0f, 1.0f };
	gateEntryCameraStartEye_ = {};
	gateEntryCameraStartTarget_ = {};
	gateEntryCameraEndEye_ = {};
	gateEntryCameraEndTarget_ = {};
	gateEntryHadPlayerControl_ = player_ ? player_->IsControlActive() : true;

	if (gateEntryTargetGate_ && player_) {
		const Vector3 gatePos = gateEntryTargetGate_->GetWorldPosition();
		const Vector3 toGateRaw = {
			gatePos.x - gateEntryStartPlayerPosition_.x,
			0.0f,
			gatePos.z - gateEntryStartPlayerPosition_.z
		};
		// 接触位置そのものではなく、ゲート面の向きから突入方向を決める。
		// これにより、ゲートの端から触れてもカメラ構図と突入方向が大きく暴れない。
		const Vector3 toGate = SelectGateEntryDirection(gateEntryTargetGate_, toGateRaw);
		gateEntryDirection_ = toGate;
		const float entryY = gatePos.y;
		const SelectGateEntryRoute entryRoute = SelectBuildGateEntryRoute(gatePos, toGate, entryY);
		gateEntrySurfacePlayerPosition_ = entryRoute.surface;
		gateEntryTargetPlayerPosition_ = entryRoute.center;
		gateEntryInsidePlayerPosition_ = entryRoute.inside;
		player_->SetMoveYaw(std::atan2(toGate.x, toGate.z));
		player_->SetIsControlActive(false);
		player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
		player_->SetSlimeAnimationDirection(toGate);
		player_->TriggerSlimeImpulse({ 1.08f, 0.92f, 1.10f }, 0.20f);
		CaptureGateEntryMaterialState(player_);
		ApplyGateEntryDissolveMaterial(0.0f, gatePos, toGate);
		if (auto* stageGate = dynamic_cast<GimmickStageGate*>(gateEntryTargetGate_)) {
			stageGate->TriggerEntryReaction();
		}

		if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
			const Vector3 side = { toGate.z, 0.0f, -toGate.x };
			// カメラはプレイヤーの接触位置ではなくゲート中心を基準にする。
			// 表裏の差はtoGateの向きだけで反転させ、どちら側から入っても同じ構図に近づける。
			const Vector3 focus = {
				gatePos.x,
				gatePos.y + kGateEntryCameraFocusHeight,
				gatePos.z
			};
			gateEntryCameraStartEye_ = camera->GetEye();
			gateEntryCameraStartTarget_ = camera->GetTargetPoint();
			gateEntryCameraEndTarget_ = focus;
			gateEntryCameraEndEye_ = {
				focus.x - toGate.x * kGateEntryCameraBackDistance + side.x * kGateEntryCameraSideOffset,
				focus.y + kGateEntryCameraHeight,
				focus.z - toGate.z * kGateEntryCameraBackDistance + side.z * kGateEntryCameraSideOffset
			};
			camera->SetInputEnabled(false);
			camera->SetFollowTarget(nullptr);
			camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
			camera->ConfigFixedPoint(
				gateEntryCameraStartEye_,
				SelectLookAtRotation(gateEntryCameraStartEye_, gateEntryCameraStartTarget_)
			);
		}
	}

	gateEntryCinematicActive_ = true;
	gateEntryCinematicTimer_ = 0.0f;
	gateEntryPendingStageIndex_ = stageIndex;
	isChangingStage_ = true;
	stageDecisionCooldown_ = 0.35f;
	if (gatePromptSprite_) {
		gatePromptSprite_->SetVisible(false);
		gatePromptSprite_->Update();
	}
	DebugConsole::GetInstance()->AddLog("Stage Select: gate entry cinematic start.");
}

void GameSelectScene::UpdateGateEntryCinematic(float deltaTime) {
	if (!gateEntryCinematicActive_) {
		return;
	}

	gateEntryCinematicTimer_ += deltaTime;
	const float t = SelectClamp01(gateEntryCinematicTimer_ / kGateEntryCinematicDuration);
	const float cameraT = SelectEaseInOutCubic(SelectClamp01(gateEntryCinematicTimer_ / kGateEntryCameraBlendTime));
	const float alignT = SelectEaseOutCubic(SelectClamp01(gateEntryCinematicTimer_ / kGateEntryAlignEndTime));
	const float centeringT = SelectEaseInOutCubic(SelectClamp01((t - kGateEntryCenteringDelay) / kGateEntryCenteringDuration));
	const float diveT = SelectEaseInOutCubic(SelectClamp01((t - kGateEntryDiveStartTime) / std::max(kGateEntryDiveEndTime - kGateEntryDiveStartTime, 0.001f)));
	const float clipT = SelectEaseInOutCubic(SelectClamp01((t - kGateEntryDissolveStartTime) / std::max(kGateEntryDissolveEndTime - kGateEntryDissolveStartTime, 0.001f)));

	if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
		camera->SetInputEnabled(false);
		camera->SetFollowTarget(nullptr);
		camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
		const Vector3 currentEye = SelectLerpVector3(gateEntryCameraStartEye_, gateEntryCameraEndEye_, cameraT);
		const Vector3 currentTarget = SelectLerpVector3(gateEntryCameraStartTarget_, gateEntryCameraEndTarget_, cameraT);
		camera->ConfigFixedPoint(currentEye, SelectLookAtRotation(currentEye, currentTarget));
	}

	if (player_) {
		player_->SetIsVisible(true);
		player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
		player_->SetIsControlActive(false);

		if (gateEntryTargetGate_) {
			const Vector3 gatePos = gateEntryTargetGate_->GetWorldPosition();
			const Vector3 toGate = gateEntryDirection_;
			const float entryY = gatePos.y;
			// 演出中にゲートが親子関係やEditor操作で動いても、入口・中心・奥位置を最新Transformから作り直す。
			const SelectGateEntryRoute entryRoute = SelectBuildGateEntryRoute(gatePos, toGate, entryY);
			gateEntrySurfacePlayerPosition_ = entryRoute.surface;
			gateEntryTargetPlayerPosition_ = entryRoute.center;
			gateEntryInsidePlayerPosition_ = entryRoute.inside;
			player_->SetMoveYaw(std::atan2(toGate.x, toGate.z));
			player_->SetSlimeAnimationDirection(toGate);
			ApplyGateEntryDissolveMaterial(clipT, gatePos, toGate);
		}

		// 接触した瞬間の位置から直接ゲート中心へ飛ばすとワープ感が強いので、
		// まずゲート正面の入口ラインへ短く補間し、その後ゲート奥へ吸い込む。
		Vector3 centeredStart = gateEntryStartPlayerPosition_;
		centeredStart.y = gateEntryStartPlayerPosition_.y +
			(gateEntrySurfacePlayerPosition_.y - gateEntryStartPlayerPosition_.y) * centeringT;
		const Vector3 alignedStart = SelectLerpVector3(centeredStart, gateEntrySurfacePlayerPosition_, alignT);
		const Vector3 controlA = SelectLerpVector3(gateEntrySurfacePlayerPosition_, gateEntryTargetPlayerPosition_, 0.42f);
		const Vector3 controlB = SelectLerpVector3(gateEntryTargetPlayerPosition_, gateEntryInsidePlayerPosition_, 0.58f);
		const Vector3 divePos = SelectBezierVector3(
			gateEntrySurfacePlayerPosition_,
			{ controlA.x, controlA.y + kGateEntryLiftHeight, controlA.z },
			{ controlB.x, controlB.y + kGateEntryLiftHeight * 0.35f, controlB.z },
			gateEntryInsidePlayerPosition_,
			diveT
		);
		Vector3 playerPos = SelectLerpVector3(alignedStart, divePos, SelectClamp01((t - 0.08f) / 0.28f));

		if (Transform* transform = player_->GetTransform()) {
			transform->translate = playerPos;
			transform->scale = gateEntryStartPlayerScale_;
		}
		if (clipT >= 0.995f || t >= 0.94f) {
			player_->SetIsVisible(false);
		}
	}

	if (gateEntryCinematicTimer_ >= kGateEntryCinematicDuration) {
		if (player_) {
			const Vector3 gatePos = gateEntryTargetGate_ ? gateEntryTargetGate_->GetWorldPosition() : gateEntryInsidePlayerPosition_;
			ApplyGateEntryDissolveMaterial(1.0f, gatePos, gateEntryDirection_);
			gateEntryMaterialSnapshots_.clear();
		}
		gateEntryCinematicActive_ = false;
		gateEntryCinematicTimer_ = 0.0f;
		gateEntryPendingStageIndex_ = -1;
		gateEntryTargetGate_ = nullptr;
		ResetGateCinematicBars();
		SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
	}
}

bool GameSelectScene::IsStageGateObject(const Object3d* object) const {
	if (!object) {
		return false;
	}
	if (const auto* gate = dynamic_cast<const GimmickStageGate*>(object)) {
		return gate->IsStageSelectNodeMode();
	}
	if (object->GetGimmickType() == "StageGate") {
		if (!object->param_.has_value()) {
			return true;
		}
		return object->param_->actionMode == 0;
	}

	const std::string& modelName = object->GetModelName();
	const std::string& objectName = object->GetName();
	return modelName.find("portal_gate") != std::string::npos ||
		modelName.find("portal_surface") != std::string::npos ||
		objectName.find("portal_gate") != std::string::npos ||
		objectName.find("portal_surface") != std::string::npos ||
		objectName.find("StageGate") != std::string::npos;
}

int GameSelectScene::GetStageGateIndex(const Object3d* object) const {
	if (!object) {
		return -1;
	}
	if (const auto* gate = dynamic_cast<const GimmickStageGate*>(object)) {
		return gate->GetStageIndex();
	}
	return object->GetTargetID();
}

Object3d* GameSelectScene::FindStageGateByIndex(int stageIndex) const {
	if (!objectManager_) {
		return nullptr;
	}
	for (const auto& object : objectManager_->GetObjects()) {
		if (!object || !IsStageGateObject(object.get())) {
			continue;
		}
		if (GetStageGateIndex(object.get()) == stageIndex) {
			return object.get();
		}
	}
	return nullptr;
}

Object3d* GameSelectScene::FindNearestStageGate(float* outDistance) const {
	if (outDistance) {
		*outDistance = std::numeric_limits<float>::max();
	}
	if (!objectManager_ || !player_) {
		return nullptr;
	}

	const Vector3 playerPos = player_->GetWorldPosition();
	Object3d* nearestGate = nullptr;
	float nearestDistanceSq = std::numeric_limits<float>::max();

	for (const auto& object : objectManager_->GetObjects()) {
		if (!object || !IsStageGateObject(object.get())) {
			continue;
		}
		if (GetStageGateIndex(object.get()) < 0) {
			continue;
		}

		const Vector3 gatePos = object->GetWorldPosition();
		float dx = gatePos.x - playerPos.x;
		float dz = gatePos.z - playerPos.z;
		float distanceSq = dx * dx + dz * dz;
		if (distanceSq < nearestDistanceSq) {
			nearestDistanceSq = distanceSq;
			nearestGate = object.get();
		}
	}

	if (outDistance && nearestGate) {
		*outDistance = std::sqrt(nearestDistanceSq);
	}
	return nearestGate;
}

bool GameSelectScene::IsPlayerTouchingStageGate(Object3d* gate) const {
	if (!gate || !player_) {
		return false;
	}

	const Vector3 gatePos = gate->GetWorldPosition();
	const Vector3 playerPos = player_->GetWorldPosition();
	const Vector3 rel = {
		playerPos.x - gatePos.x,
		0.0f,
		playerPos.z - gatePos.z
	};
	const float distanceSq = rel.x * rel.x + rel.z * rel.z;

	const Vector3 gateScale = gate->GetScale();
	const float gateSize = std::max(1.0f, std::max(std::fabs(gateScale.x), std::fabs(gateScale.z)));
	const float halfWidth = std::clamp(gateSize * 0.36f, kGateEntryMinHalfWidth, kGateEntryMaxHalfWidth);
	const float planeThickness = std::clamp(gateSize * 0.045f, kGateEntryMinPlaneThickness, kGateEntryMaxPlaneThickness) + kGateEntryPlayerTouchRadius;

	float yaw = 0.0f;
	if (Transform* transform = gate->GetTransform()) {
		yaw = transform->rotate.y;
	}

	const Vector3 forward = { std::sin(yaw), 0.0f, std::cos(yaw) };
	const Vector3 right = { std::cos(yaw), 0.0f, -std::sin(yaw) };
	const float planeDistance = rel.x * forward.x + rel.z * forward.z;
	const float sideDistance = rel.x * right.x + rel.z * right.z;
	const bool yawPlaneTouch = std::fabs(planeDistance) <= planeThickness && std::fabs(sideDistance) <= halfWidth;

	const bool xPlaneTouch = std::fabs(rel.x) <= planeThickness && std::fabs(rel.z) <= halfWidth;
	const bool zPlaneTouch = std::fabs(rel.z) <= planeThickness && std::fabs(rel.x) <= halfWidth;
	return yawPlaneTouch || xPlaneTouch || zPlaneTouch;
}
