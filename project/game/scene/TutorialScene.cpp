#define NOMINMAX
#include "TutorialScene.h"
#include "ScenePreloader.h"
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
#include "RenderStats.h"
#include <cassert>
#include "BulletManager.h"
#include "MoveStrategy3D.h"
#include "MoveStrategy2D.h"
#include "LevelLoader.h"
#include "LockOnSystem.h"
#include "GameRule.h"
#include "ObjectManager.h" 
#include "BossCore.h"
#include "DebrisEffectManager.h"
#include "MeshEffectManager.h"
#include "VFXSequencer.h"
#include "WinApp.h"
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
#include "TutorialDirector.h"

#include <algorithm>
#include <iterator>

namespace {
constexpr const char* kControlsGuidePortraitsToPreload[] = {
	"Resources/sprite/ui/control_guide/portraits/pink_slime.png",
	"Resources/sprite/ui/control_guide/portraits/bomb_slime.png",
	"Resources/sprite/ui/control_guide/portraits/wind_slime.png",
	"Resources/sprite/ui/control_guide/portraits/fire_slime.png",
	"Resources/sprite/ui/control_guide/portraits/thunder_slime.png"
};

constexpr const char* kControlsGuideAbilityLabelsToPreload[] = {
	"Resources/sprite/ui/control_guide/labels/absorb.png",
	"Resources/sprite/ui/control_guide/labels/throw.png",
	"Resources/sprite/ui/control_guide/labels/slime_dive.png",
	"Resources/sprite/ui/control_guide/labels/puni_straight.png",
	"Resources/sprite/ui/control_guide/labels/bounce_evade.png",
	"Resources/sprite/ui/control_guide/labels/bomb_throw.png",
	"Resources/sprite/ui/control_guide/labels/bomb_place.png",
	"Resources/sprite/ui/control_guide/labels/blast_jump.png",
	"Resources/sprite/ui/control_guide/labels/fireball.png",
	"Resources/sprite/ui/control_guide/labels/flame_breath.png",
	"Resources/sprite/ui/control_guide/labels/blaze_step.png",
	"Resources/sprite/ui/control_guide/labels/thunder_chain.png",
	"Resources/sprite/ui/control_guide/labels/charged_discharge.png",
	"Resources/sprite/ui/control_guide/labels/thunder_step.png",
	"Resources/sprite/ui/control_guide/labels/updraft.png",
	"Resources/sprite/ui/control_guide/labels/wind_breath.png",
	"Resources/sprite/ui/control_guide/labels/wind_dash.png"
};

constexpr const char* kTutorialMeshEffectsToPreload[] = {
	"Resources/json/effect/effect_pink_slime_charge_pulse_ring.json",
	"Resources/json/effect/effect_pink_slime_charge_core_flash.json",
	"Resources/json/effect/effect_pink_slime_charge_vortex_streak.json",
	"Resources/json/effect/effect_pink_slime_launch_kick_ring.json",
	"Resources/json/effect/effect_pink_slime_apex_focus_flash.json",
	"Resources/json/effect/effect_pink_slime_dive_streak.json",
	"Resources/json/effect/effect_pink_slime_landing_burst_ring.json",
	"Resources/json/effect/effect_pink_slime_landing_core_flash.json",
	"Resources/json/effect/effect_pink_slime_landing_shock_arc.json",
	"Resources/json/effect/effect_player_pink_straight_arc.json",
	"Resources/json/effect/effect_player_pink_straight_impact.json",
	"Resources/json/effect/effect_player_pink_guard_start.json",
	"Resources/json/effect/effect_player_pink_guard_shell.json",
	"Resources/json/effect/effect_player_pink_guard_release.json"
};

constexpr const char* kTutorialGpuParticlePresetsToPreload[] = {
	"hit_pull_bind",
	"hit_pull_catch",
	"hit_slime_elastic",
	"hit_throw_slam_dust",
	"hit_enemy_ability",
	"player_jump_dust",
	"player_land_dust",
	"player_pink_straight_splash"
};

constexpr const char* kTutorialVfxSequencesToPreload[] = {
	"slime_elastic_hit_cue",
	"pull_bind_cue",
	"pull_catch_cue",
	"throw_slam_cue",
	"enemy_ability_hit_cue"
};

constexpr const char* kTutorialDebrisPresetsToPreload[] = {
	"throw_slam_pebble_burst",
	"pink_slime_charge_pebble_pull",
	"pink_slime_landing_pebble_burst"
};

constexpr const char* kTutorialPromptTexturesToPreload[] = {
	"Resources/sprite/ui/tutorial/tutorial_move.png",
	"Resources/sprite/ui/tutorial/tutorial_look.png",
	"Resources/sprite/ui/tutorial/tutorial_jump.png",
	"Resources/sprite/ui/tutorial/tutorial_pull.png",
	"Resources/sprite/ui/tutorial/tutorial_throw.png",
	"Resources/sprite/ui/tutorial/tutorial_absorb.png",
	"Resources/sprite/ui/tutorial/tutorial_ability.png",
	"Resources/sprite/ui/tutorial/tutorial_release.png",
	"Resources/sprite/ui/tutorial/tutorial_controls.png",
	"Resources/sprite/ui/tutorial/tutorial_exit.png",
	"Resources/sprite/ui/tutorial/tutorial_objective_marker.png"
};
}

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

void TutorialScene::CollectReplaySprites(std::vector<Sprite*>& replaySprites) {
	BaseScene::CollectReplaySprites(replaySprites);
	if (lockOnSprite_) {
		replaySprites.push_back(lockOnSprite_.get());
	}
	if (controlsGuideOverlay_) {
		controlsGuideOverlay_->CollectReplaySprites(replaySprites);
	}
	if (saveIndicatorOverlay_) {
		saveIndicatorOverlay_->CollectReplaySprites(replaySprites);
	}
}

void TutorialScene::CaptureReplaySceneState(json& state) const {
	json tutorialState = json::object();
	json controlsGuideState = json::object();
	json saveIndicatorState = json::object();
	if (tutorialDirector_) tutorialDirector_->CaptureReplayState(tutorialState);
	if (controlsGuideOverlay_) controlsGuideOverlay_->CaptureReplayState(controlsGuideState);
	if (saveIndicatorOverlay_) saveIndicatorOverlay_->CaptureReplayState(saveIndicatorState);

	state = {
		{ "goal", {
			{ "active", isGoal_ },
			{ "savePerformed", goalSavePerformed_ }
		} },
		{ "movie", {
			{ "state", static_cast<int>(movieState_) },
			{ "timer", movieTimer_ },
			{ "startCameraEye", { movieStartCameraEye_.x, movieStartCameraEye_.y, movieStartCameraEye_.z } },
			{ "startCameraTarget", { movieStartCameraTarget_.x, movieStartCameraTarget_.y, movieStartCameraTarget_.z } },
			{ "bridgeDropped", hasBridgeDropped_ }
		} },
		{ "sessionStarCoins", { sessionStarCoins_[0], sessionStarCoins_[1], sessionStarCoins_[2] } },
		{ "tutorial", std::move(tutorialState) },
		{ "overlays", {
			{ "controlsGuide", std::move(controlsGuideState) },
			{ "saveIndicator", std::move(saveIndicatorState) }
		} }
	};
}

void TutorialScene::RestoreReplaySceneState(const json& state) {
	if (!state.is_object()) {
		return;
	}

	auto restoreVector3 = [](const json& object, const char* key, Vector3& value) {
		const auto found = object.find(key);
		if (found != object.end() && found->is_array() && found->size() >= 3) {
			value = { (*found)[0].get<float>(), (*found)[1].get<float>(), (*found)[2].get<float>() };
		}
	};

	if (const auto found = state.find("goal"); found != state.end() && found->is_object()) {
		isGoal_ = found->value("active", false);
		goalSavePerformed_ = found->value("savePerformed", false);
	}
	if (const auto found = state.find("movie"); found != state.end() && found->is_object()) {
		const int movieState = (std::clamp)(found->value("state", 0), 0, 1);
		movieState_ = static_cast<MovieState>(movieState);
		movieTimer_ = (std::max)(0.0f, found->value("timer", 0.0f));
		restoreVector3(*found, "startCameraEye", movieStartCameraEye_);
		restoreVector3(*found, "startCameraTarget", movieStartCameraTarget_);
		hasBridgeDropped_ = found->value("bridgeDropped", false);
	}
	if (const auto found = state.find("sessionStarCoins"); found != state.end() && found->is_array()) {
		for (size_t index = 0; index < 3 && index < found->size(); ++index) {
			sessionStarCoins_[index] = (*found)[index].get<bool>();
		}
	}
	if (tutorialDirector_ && state.contains("tutorial")) {
		tutorialDirector_->RestoreReplayState(state["tutorial"]);
	}
	if (const auto found = state.find("overlays"); found != state.end() && found->is_object()) {
		if (controlsGuideOverlay_ && found->contains("controlsGuide")) {
			controlsGuideOverlay_->RestoreReplayState((*found)["controlsGuide"]);
		}
		if (saveIndicatorOverlay_ && found->contains("saveIndicator")) {
			saveIndicatorOverlay_->RestoreReplayState((*found)["saveIndicator"]);
		}
	}
}

SceneLoadManifest TutorialScene::BuildAsyncLoadManifest() const {
	SceneLoadManifest manifest;
	manifest.AddObjectLayout(HasSceneAssetContext() && !GetSceneLoadContext().objectLayoutPath.empty()
		? GetSceneLoadContext().objectLayoutPath
		: "Resources/json/3Dobject/tutorial.json");
	manifest.AddSpriteLayout(HasSceneAssetContext() && !GetSceneLoadContext().spriteLayoutPath.empty()
		? GetSceneLoadContext().spriteLayoutPath
		: "Resources/json/sprite/tutorialScene.json");
	manifest.AddSpriteLayout("Resources/json/sprite/controlsGuide.json");
	for (const char* path : kControlsGuidePortraitsToPreload) {
		manifest.AddTexture(path);
	}
	for (const char* path : kControlsGuideAbilityLabelsToPreload) {
		manifest.AddTexture(path);
	}
	manifest.AddTexture("Resources/sprite/common/circle2.png");
	manifest.AddTexture("Resources/sprite/common/white.png");
	manifest.AddTexture("Resources/sprite/ui/hud/lockOn.png");
	for (const char* path : kTutorialPromptTexturesToPreload) {
		manifest.AddTexture(path);
	}
	manifest.AddTexture(GetSceneLoadContext().skyboxPath.empty()
		? "Resources/output_skybox.dds"
		: GetSceneLoadContext().skyboxPath);
	std::string preloadBgmPath = GetSceneLoadContext().bgmPath;
	if (preloadBgmPath.empty()) {
	    const auto& preloadStages = StageManager::GetInstance()->GetStages();
	    const int preloadStageIndex = StageManager::GetInstance()->GetCurrentStageIndex();
	    if (preloadStageIndex >= 0 &&
	        preloadStageIndex < static_cast<int>(preloadStages.size())) {
	        preloadBgmPath = preloadStages[preloadStageIndex].bgmPath;
	    }
	}
	if (!preloadBgmPath.empty()) {
	    manifest.AddAudio(preloadBgmPath);
	}
	return manifest;
}

void TutorialScene::Initialize() {
	BeginLoadingInitialize();
	while (!InitializeLoadingStep()) {
	}
}

void TutorialScene::BeginLoadingInitialize() {
	loadingInitializePhase_ = 0;
	loadingInitializeItemIndex_ = 0;
	loadingInitializeCompletedUnits_ = 0;
	loadingInitializeTotalUnits_ =
		10 +
		std::size(kTutorialGpuParticlePresetsToPreload) +
		std::size(kTutorialMeshEffectsToPreload) +
		std::size(kTutorialDebrisPresetsToPreload) +
		std::size(kTutorialVfxSequencesToPreload);
}

bool TutorialScene::InitializeLoadingStep() {
	auto completeUnit = [this]() {
		++loadingInitializeCompletedUnits_;
	};

	for (;;) {
		switch (loadingInitializePhase_) {
		case 0: {
			dxCommon_ = DirectXCommon::GetInstance();
			inputManager_ = InputManager::GetInstance();
			audioPlayer_ = AudioPlayer::GetInstance();
			LOG("Tutorial Scene Initialized!");
			const StageData& currentStage = StageManager::GetInstance()->GetCurrentStage();
			bgmHandle_ = audioPlayer_->LoadSoundFile(ResolveSceneBgmPath(currentStage.bgmPath));
			EventManager::GetInstance()->ClearAllListeners();
			CameraManager::GetInstance()->Initialize();
			CameraManager::GetInstance()->SetInputManager(inputManager_);
			++loadingInitializePhase_;
			completeUnit();
			return false;
		}
		case 1:
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
			++loadingInitializePhase_;
			completeUnit();
			return false;
		case 2: {
			objectManager_ = std::make_unique<ObjectManager>();
			lockOnSystem_ = std::make_unique<LockOnSystem>();
			lockOnSystem_->Initialize(inputManager_);
			const uint32_t lockOnTex = TextureManager::GetInstance()->Load("Resources/sprite/ui/hud/lockOn.png");
			lockOnSprite_ = std::make_unique<Sprite>();
			lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
			lockOnSprite_->SetAnchorPoint({ 0.5f, 0.5f });
			lockOnSprite_->SetSize({ 64.0f, 64.0f });
			BulletManager::GetInstance()->Initialize(object3dCommon_.get(), CollisionManager::GetInstance());
			++loadingInitializePhase_;
			completeUnit();
			return false;
		}
		case 3:
			GPUParticleManager::GetInstance()->Initialize(dxCommon_);
			GPUParticleManager::GetInstance()->LoadAllPresets("Resources/json/gpu_particles/");
			gpuParticleTexHandle_ = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
			++loadingInitializePhase_;
			loadingInitializeItemIndex_ = 0;
			completeUnit();
			return false;
		case 4:
			if (loadingInitializeItemIndex_ < std::size(kTutorialGpuParticlePresetsToPreload)) {
				GPUParticleManager::GetInstance()->PreloadPresetSystem(
					kTutorialGpuParticlePresetsToPreload[loadingInitializeItemIndex_++]);
				completeUnit();
				return false;
			}
			++loadingInitializePhase_;
			loadingInitializeItemIndex_ = 0;
			continue;
		case 5:
			if (loadingInitializeItemIndex_ < std::size(kTutorialMeshEffectsToPreload)) {
				MeshEffectManager::GetInstance()->PreloadEffect(
					kTutorialMeshEffectsToPreload[loadingInitializeItemIndex_++]);
				completeUnit();
				return false;
			}
			++loadingInitializePhase_;
			loadingInitializeItemIndex_ = 0;
			continue;
		case 6:
			DebrisEffectManager::GetInstance()->Initialize(object3dCommon_.get());
			DebrisEffectManager::GetInstance()->LoadAllPresets("Resources/json/debris/");
			++loadingInitializePhase_;
			loadingInitializeItemIndex_ = 0;
			completeUnit();
			return false;
		case 7:
			if (loadingInitializeItemIndex_ < std::size(kTutorialDebrisPresetsToPreload)) {
				DebrisEffectManager::GetInstance()->PrewarmPreset(
					kTutorialDebrisPresetsToPreload[loadingInitializeItemIndex_++]);
				completeUnit();
				return false;
			}
			++loadingInitializePhase_;
			loadingInitializeItemIndex_ = 0;
			continue;
		case 8:
			if (loadingInitializeItemIndex_ < std::size(kTutorialVfxSequencesToPreload)) {
				VFXSequencer sequence;
				sequence.Load(kTutorialVfxSequencesToPreload[loadingInitializeItemIndex_++]);
				completeUnit();
				return false;
			}
			++loadingInitializePhase_;
			continue;
		case 9:
			skyboxTextureHandle_ = TextureManager::GetInstance()->Load(
				ResolveSceneSkyboxPath("Resources/output_skybox.dds"));
			skybox_ = std::make_unique<Skybox>();
			skybox_->Initialize(object3dCommon_.get(), skyboxTextureHandle_);
			++loadingInitializePhase_;
			completeUnit();
			return false;
		case 10:
			levelLoader_ = std::make_unique<LevelLoader>();
			levelLoader_->LoadObjectLayout(this, "Resources/json/3Dobject/tutorial.json");
			++loadingInitializePhase_;
			completeUnit();
			return false;
		case 11:
			levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/tutorialScene.json");
			if (player_) {
				player_->SetTutorialSafetyEnabled(true);
			}
			++loadingInitializePhase_;
			completeUnit();
			return false;
		case 12:
			controlsGuideOverlay_ = std::make_unique<ControlsGuideOverlay>();
			controlsGuideOverlay_->Initialize(spriteCommon_.get(), player_);
			saveIndicatorOverlay_ = std::make_unique<SaveIndicatorOverlay>();
			saveIndicatorOverlay_->Initialize(spriteCommon_.get());
			tutorialDirector_ = std::make_unique<TutorialDirector>();
			if (tutorialDirector_->Initialize(this, player_, inputManager_)) {
				tutorialDirector_->SetCompletionCallback([this]() {
					HandleTutorialFlowCompleted();
				});
			}
			++loadingInitializePhase_;
			completeUnit();
			return false;
		case 13:
			LightManager::GetInstance()->LoadState(
				ResolveSceneLightPath("Resources/json/light/light_layout.json"));
			CameraEditor::GetInstance()->Initialize();
			// 本編と同じ三人称カメラ設定を共有します。
			CameraEditor::GetInstance()->LoadFile("game_camera.json");
			CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);
			if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
				camera->ResetFollowSmoothing();
			}
			++loadingInitializePhase_;
			completeUnit();
			return true;
		default:
			return true;
		}
	}
}

float TutorialScene::GetLoadingInitializeProgress() const {
	return loadingInitializeTotalUnits_ == 0
		? 1.0f
		: std::clamp(
			static_cast<float>(loadingInitializeCompletedUnits_) /
				static_cast<float>(loadingInitializeTotalUnits_),
			0.0f,
			1.0f);
}

void TutorialScene::Finalize() {
	if (tutorialDirector_) {
		tutorialDirector_->Finalize();
	}
	tutorialDirector_.reset();
	if (controlsGuideOverlay_) {
		controlsGuideOverlay_->Finalize();
	}
	controlsGuideOverlay_.reset();
	MeshEffectManager::GetInstance()->Clear();
	VFXSequencer::ClearOneShots();
	GPUParticleManager::GetInstance()->ClearSceneRuntime();
	DebrisEffectManager::GetInstance()->Clear();
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

	if (HandleControlsGuideOverlay(deltaTime)) {
		if (saveIndicatorOverlay_) {
			saveIndicatorOverlay_->Update(deltaTime);
		}
		return;
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
	CameraManager::GetInstance()->Update(deltaTime);
	particleSystem_->Update(deltaTime);
	if (tutorialDirector_) {
		tutorialDirector_->Update(deltaTime);
	}
	objectManager_->Update(deltaTime);
	if (gameRule_) {
		gameRule_->Update(deltaTime);
	}
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

}

void TutorialScene::Draw() {
	bool isFirstPerson = false;
	Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

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

	if (skybox_ && camera && LightManager::GetInstance()->IsSkyboxEnabled()) {
		skybox_->SetTextureHandle(LightManager::GetInstance()->GetSkyboxTextureHandle());
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
		if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || IsSpecialMaterialType(obj->GetMaterialType())) continue;

		obj->Draw(pointLightRes, spotLightRes);
	}
	
	if (player_ && player_->GetHookMarker()) {
		player_->GetHookMarker()->Draw(pointLightRes, spotLightRes);
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

	DrawLocalFogObjects(objects, dxCommon_, player_, isFirstPerson);

	bool hasGPUParticles = !GPUParticleManager::GetInstance()->IsEmpty();
	bool grabUpdated = DrawSpecialMaterialObjects(objects, dxCommon_, BulletManager::GetInstance(), player_, isFirstPerson);
	if (hasGPUParticles) {
		DrawGPUParticles(dxCommon_, camera, gpuParticleTexHandle_, grabUpdated);
	}
}

void TutorialScene::DrawUI() {
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
	for (auto& sprite : sprites_) sprite->Draw();
	if (isDrawLockOn_ && lockOnSprite_) lockOnSprite_->Draw();
	if (player_) player_->DrawUI();
	if (controlsGuideOverlay_ && controlsGuideOverlay_->IsActive()) {
		controlsGuideOverlay_->Draw();
	}
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
    if (tutorialDirector_ && ImGui::CollapsingHeader("Tutorial Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
        tutorialDirector_->DrawImGui();
    }
    ImGui::Separator();
    ImGui::TextDisabled("※この項目は TutorialScene::DrawImGui() で編集可能です");
#endif
}

void TutorialScene::StartBridgeDropMovie() {
    if (tutorialDirector_) {
        tutorialDirector_->JumpToStep("path_reveal");
    }
}

bool TutorialScene::HandleControlsGuideOverlay(float deltaTime) {
    if (controlsGuideOverlay_ && controlsGuideOverlay_->IsActive()) {
        controlsGuideOverlay_->SetPlayer(player_);
        controlsGuideOverlay_->Update(deltaTime);
        return true;
    }

    if (!IsControlsGuideOpenTriggered()) {
        return false;
    }

    if (controlsGuideOverlay_) {
        controlsGuideOverlay_->SetPlayer(player_);
        controlsGuideOverlay_->SetActive(true);
    }
    if (tutorialDirector_) {
        tutorialDirector_->NotifyControlsGuideOpened();
    }
    return true;
}

bool TutorialScene::IsControlsGuideOpenTriggered() const {
    if (!inputManager_ || isGoal_) {
        return false;
    }

#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard || io.WantTextInput) {
        return false;
    }
#endif

    return inputManager_->IsKeyTriggered(DIK_TAB);
}

void TutorialScene::HandleTutorialFlowCompleted() {
    if (goalSavePerformed_) {
        return;
    }

    GameDataManager::GetInstance()->MarkStageCleared(-1);
    if (saveIndicatorOverlay_) {
        saveIndicatorOverlay_->Play(1.35f);
    }
    DebugConsole::GetInstance()->AddLog("Tutorial flow completed. Exit gate unlocked.");
    goalSavePerformed_ = true;
}

bool TutorialScene::IsVisible(Object3d* obj) {
    if (!obj || !obj->GetIsVisible()) return false;
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return true;
    AABB worldAabb = obj->GetModelWorldAABB();
    constexpr float kNearObjectCullBypassDistance = 24.0f;
    const Vector3& cameraPosition = camera->GetEye();
    if (Math::DistanceSquaredPointAABB(cameraPosition, worldAabb.min, worldAabb.max) <=
        kNearObjectCullBypassDistance * kNearObjectCullBypassDistance) {
        return true;
    }
    const bool visible = Math::IntersectFrustumAABB(camera->GetFrustum(), worldAabb.min, worldAabb.max);
    if (!visible) {
        RenderStats::GetInstance()->RecordCulledObject();
    }
    return visible;
}
