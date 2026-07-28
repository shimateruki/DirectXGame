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
#include "Fade.h"
#include "StageManager.h"
#include "GameDataManager.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr const char* kGoalPresentationTuningPath = "Resources/json/cinematic/goal_clear.json";
constexpr const char* kGoalCinematicTimelinePath = "Resources/json/scenario/goal_clear_timeline.json";

float GoalClamp01(float value) {
	return std::clamp(value, 0.0f, 1.0f);
}

float GoalEaseOut(float value) {
	value = GoalClamp01(value);
	const float inv = 1.0f - value;
	return 1.0f - inv * inv * inv;
}

float GoalEaseInOut(float value) {
	value = GoalClamp01(value);
	return value * value * (3.0f - 2.0f * value);
}
}

void GamePlayScene::InitializeGoalCinematicTimeline() {
    goalCinematicPlayer_.Initialize(SceneManager::GetInstance());
    goalCinematicPlayer_.SetAnimationCallback(
        [this](Object3d* target, const CinematicAnimationClipData& clip, float localTime, bool isPreview) {
            HandleGoalCinematicAnimation(target, clip, localTime, isPreview);
        });
    goalCinematicPlayer_.SetSignalCallback(
        [this](Object3d* target, const CinematicSignalMarker& signal, bool isPreview) {
            HandleGoalCinematicSignal(target, signal, isPreview);
        });

    goalCinematicTimelineLoaded_ = goalCinematicSequence_.Load(kGoalCinematicTimelinePath);
    if (!goalCinematicTimelineLoaded_) {
        DebugConsole::GetInstance()->AddLog(
            std::string("Goal Timeline load failed: ") + kGoalCinematicTimelinePath);
        goalCinematicSequence_.Clear();
        goalCinematicSequence_.name = "goal_clear_timeline";
        SyncGoalCinematicTimelineFromTuning();
        goalCinematicTimelineLoaded_ = true;
    } else {
        ApplyGoalCinematicTimingFromSequence();
    }
    goalCinematicPlayer_.SetSequence(&goalCinematicSequence_);
}

void GamePlayScene::ApplyGoalCinematicTimingFromSequence() {
    GoalClearPlayerAnimator::Tuning animation = goalClearPlayerAnimator_.GetTuning();
    for (const auto& marker : goalCinematicSequence_.signals) {
        if (!marker.enabled) {
            continue;
        }
        if (marker.signal == "goal.crown_focus_end") {
            goalPresentationTuning_.crownFocusEndTime = marker.time;
        } else if (marker.signal == "goal.crown_move_start") {
            goalPresentationTuning_.crownMoveStartTime = marker.time;
        } else if (marker.signal == "goal.crown_land") {
            animation.crownLandTime = marker.time;
        } else if (marker.signal == "goal.anticipation") {
            animation.anticipationStartTime = marker.time;
        } else if (marker.signal == "goal.jump") {
            animation.jumpStartTime = marker.time;
        } else if (marker.signal == "goal.apex") {
            animation.apexTime = marker.time;
        } else if (marker.signal == "goal.result") {
            animation.resultUiTime = marker.time;
        } else if (marker.signal == "goal.ready") {
            animation.readyTime = marker.time;
        }
    }
    goalClearPlayerAnimator_.SetTuning(animation);
    SanitizeGoalPresentationTuning();
}

void GamePlayScene::SyncGoalCinematicTimelineFromTuning() {
    const GoalClearPlayerAnimator::Tuning& animation = goalClearPlayerAnimator_.GetTuning();
    auto SetSignalTime = [this](const char* signalId, const char* displayName, float time) {
        for (auto& marker : goalCinematicSequence_.signals) {
            if (marker.signal == signalId) {
                marker.time = time;
                marker.enabled = true;
                return;
            }
        }
        CinematicSignalMarker marker;
        marker.signal = signalId;
        marker.name = displayName;
        marker.time = time;
        goalCinematicSequence_.signals.push_back(marker);
    };

    SetSignalTime("goal.crown_focus_end", "Crown Focus End", goalPresentationTuning_.crownFocusEndTime);
    SetSignalTime("goal.crown_move_start", "Crown Move Start", goalPresentationTuning_.crownMoveStartTime);
    SetSignalTime("goal.crown_land", "Crown Land", animation.crownLandTime);
    SetSignalTime("goal.anticipation", "Anticipation", animation.anticipationStartTime);
    SetSignalTime("goal.jump", "Big Jump", animation.jumpStartTime);
    SetSignalTime("goal.apex", "Jump Apex", animation.apexTime);
    SetSignalTime("goal.result", "Result UI", animation.resultUiTime);
    SetSignalTime("goal.ready", "Ready To Return", animation.readyTime);

    bool hasAnimationDriver = false;
    for (auto& clip : goalCinematicSequence_.animationClips) {
        if (clip.driver == "GoalClearPlayer") {
            clip.startTime = 0.0f;
            clip.duration = animation.readyTime;
            clip.binding.targetName = player_ ? player_->GetName() : "player";
            clip.binding.targetEventId = player_ ? player_->GetEventID() : -1;
            hasAnimationDriver = true;
        }
    }
    if (!hasAnimationDriver) {
        CinematicAnimationClipData clip;
        clip.name = "Player Goal Celebration";
        clip.driver = "GoalClearPlayer";
        clip.clipName = "goal_clear";
        clip.duration = animation.readyTime;
        clip.binding.targetName = player_ ? player_->GetName() : "player";
        clip.binding.targetEventId = player_ ? player_->GetEventID() : -1;
        goalCinematicSequence_.animationClips.push_back(clip);
    }

    bool hasCrownFocusVfx = false;
    bool hasCrownVfx = false;
    bool hasResultVfx = false;
    for (auto& track : goalCinematicSequence_.vfxTracks) {
        if (track.sequenceName == "crown_focus_cue") {
            track.startTime = std::max(0.05f, goalPresentationTuning_.crownFocusEndTime - 0.22f);
            hasCrownFocusVfx = true;
        } else if (track.sequenceName == "crown_get_cue") {
            track.startTime = animation.crownLandTime;
            hasCrownVfx = true;
        } else if (track.sequenceName == "crown_result_cue") {
            track.startTime = animation.apexTime;
            hasResultVfx = true;
        }
    }
    Object3d* crown = FindGoalCrownObject();
    if (!hasCrownFocusVfx) {
        CinematicVFXTrackData track;
        track.name = "Crown Focus VFX";
        track.sequenceName = "crown_focus_cue";
        track.startTime = std::max(0.05f, goalPresentationTuning_.crownFocusEndTime - 0.22f);
        track.binding.targetName = crown ? crown->GetName() : "goal";
        track.binding.targetEventId = crown ? crown->GetEventID() : -1;
        goalCinematicSequence_.vfxTracks.push_back(track);
    }
    if (!hasCrownVfx) {
        CinematicVFXTrackData track;
        track.name = "Crown Landing VFX";
        track.sequenceName = "crown_get_cue";
        track.startTime = animation.crownLandTime;
        track.binding.targetName = crown ? crown->GetName() : "goal";
        track.binding.targetEventId = crown ? crown->GetEventID() : -1;
        goalCinematicSequence_.vfxTracks.push_back(track);
    }
    if (!hasResultVfx) {
        CinematicVFXTrackData track;
        track.name = "Result Burst VFX";
        track.sequenceName = "crown_result_cue";
        track.startTime = animation.apexTime;
        track.binding.targetName = player_ ? player_->GetName() : "player";
        track.binding.targetEventId = player_ ? player_->GetEventID() : -1;
        goalCinematicSequence_.vfxTracks.push_back(track);
    }

    bool hasJumpAudio = false;
    for (auto& clip : goalCinematicSequence_.audioClips) {
        if (clip.audioId == "slime_stretch") {
            clip.startTime = animation.jumpStartTime;
            hasJumpAudio = true;
        }
    }
    if (!hasJumpAudio) {
        CinematicAudioClipData clip;
        clip.name = "Jump Stretch SE";
        clip.audioId = "slime_stretch";
        clip.startTime = animation.jumpStartTime;
        clip.duration = 0.2f;
        clip.volume = 0.85f;
        goalCinematicSequence_.audioClips.push_back(clip);
    }
    goalCinematicSequence_.duration = animation.readyTime;
    goalCinematicSequence_.Sort();
}

void GamePlayScene::HandleGoalCinematicAnimation(
    Object3d* target,
    const CinematicAnimationClipData& clip,
    float localTime,
    bool isPreview) {
    (void)isPreview;
    if (clip.driver != "GoalClearPlayer" || !player_ || (target && target != player_)) {
        return;
    }
    goalClearPlayerAnimator_.Update(localTime);
    goalPlayerPosePosition_ = goalClearPlayerAnimator_.GetPosePosition();
}

void GamePlayScene::HandleGoalCinematicSignal(
    Object3d* target,
    const CinematicSignalMarker& signal,
    bool isPreview) {
    (void)target;
    (void)isPreview;
    if (signal.signal == "goal.ready" && goalPresentationState_ == GoalPresentationState::Celebrating) {
        goalPresentationState_ = GoalPresentationState::ReadyToReturn;
    }
}

GamePlayScene::GamePlayScene() {}
GamePlayScene::~GamePlayScene() {}

void GamePlayScene::Draw() {
	// --- 一人称視点（カメラのめり込み）判定 ---
	bool isFirstPerson = false;
	Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
#ifndef _DEBUG
	if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
		isFirstPerson = true;
	}
#endif

	// =========================================================
	// カメラがプレイヤーに近すぎたら、強制的に「非表示(一人称扱い)」にする
	// =========================================================
	if (!isFirstPerson && player_ && camera) {
		Vector3 pPos = player_->GetWorldPosition();
		pPos.y += 1.0f; // プレイヤーの胸の高さを基準にする
		Vector3 cPos = camera->GetEye();
		Vector3 toCam = { cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z };
		float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);

		// 距離が 3.0m 未満なら、プレイヤーを完全に消す判定フラグを立てる
		if (dist < 3.0f) {
			isFirstPerson = true;
		}
	}

	ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
	ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();

    if (lifeLostPresentationActive_) {
        DrawLifeLostPresentationWorld(pointLightRes, spotLightRes);
        return;
    }

    if (lifeLostBlackHold_) {
        return;
    }

	auto& objects = objectManager_->GetObjects();

	// =======================================================
	// 1. 背景描画
	// =======================================================
	if (skybox_ && camera && LightManager::GetInstance()->IsSkyboxEnabled()) {
		skybox_->SetTextureHandle(LightManager::GetInstance()->GetSkyboxTextureHandle());
		skybox_->Draw(camera->GetConstantBuffer());
	}

	// =======================================================
	// 2. 不透明モデル描画
	// =======================================================
	object3dCommon_->SetGraphicsCommand();
	object3dCommon_->SetPipelineState(BlendMode::kNone); // 不透明設定

	for (auto& obj : objects) {
		if (!IsVisible(obj.get())) continue;

		// プレイヤー本体および「子パーツ（緑のブロック等）」かどうかの判定
		bool isPlayerPart = false;
		if (isFirstPerson) {
			Object3d* current = obj.get();
			while (current) {
				if (current == player_) { isPlayerPart = true; break; }
				current = current->GetParent();
			}
		}

		// プレイヤーの一部なら描画をスキップ
		if (isPlayerPart) {
			continue;
		}

		// 半透明マテリアル(1)、ローカルフォグ(7)、特殊描画マテリアル(8〜22)はここでは描画しない
		if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || IsSpecialMaterialType(obj->GetMaterialType())) continue;

		obj->Draw(pointLightRes, spotLightRes);
	}
	
	// フックマーカーの描画（プレイヤーがカメラ外に判定されて非表示になってもマーカーだけは描画する）
	if (player_ && player_->GetHookMarker()) {
		player_->GetHookMarker()->Draw(pointLightRes, spotLightRes);
	}

	if (animatedCube_) {
		animatedCube_->Draw(pointLightRes, spotLightRes);
	}

	// =======================================================
	// 3. 中間描画 (弾・デバッグUIなど)
	// =======================================================
	BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
	LightEditor::GetInstance()->Draw3D();

	// =======================================================
	// 4. 半透明モデル描画
	// =======================================================
	for (auto& obj : objects) {
		bool isPlayerPart = false;
		if (isFirstPerson) {
			Object3d* current = obj.get();
			while (current) {
				if (current == player_) { isPlayerPart = true; break; }
				current = current->GetParent();
			}
		}

		if (isPlayerPart) {
			continue;
		}

		if (obj->GetMaterialType() == 1) { // 半透明マテリアルのみ描画
			obj->Draw(pointLightRes, spotLightRes);
		}
	}
	particleSystem_->Draw();

	// =======================================================
	// 5. ローカルフォグ (霧の箱) の描画
	// =======================================================
	DrawLocalFogObjects(objects, dxCommon_, player_, isFirstPerson);

	// =======================================================
	// 6. GPUパーティクル / 流体 (水・マグマ・氷) の描画
	// =======================================================
	bool hasFluid = BulletManager::GetInstance()->HasSpecialMaterialBullets();
	for (auto& obj : objects) {
		if (IsSpecialMaterialType(obj->GetMaterialType()) || obj->HasOwnedSpecialMaterialVisuals()) {
			hasFluid = true;
			break;
		}
	}

	GPUParticleManager* gpuParticleManager = GPUParticleManager::GetInstance();
	const bool hasGPUParticles = !gpuParticleManager->IsEmpty();
	const bool gpuParticlesNeedGrab = hasGPUParticles && gpuParticleManager->RequiresSceneColorCopy();
	bool grabUpdated = false;
	const bool isCameraPreview = dxCommon_ && dxCommon_->IsCameraPreviewRendering();
	if (!isCameraPreview && (hasFluid || hasGPUParticles)) {
		// 特殊マテリアルか歪みパーティクルが背景色を使う時だけ、画面全体をコピーする。
		if (hasFluid || gpuParticlesNeedGrab) {
			dxCommon_->UpdateGrabTexture();
			grabUpdated = true;
		}
		// 水・炎・GPUパーティクルは深度をSRVとして読むため、この区間ではDSVを外します。
		dxCommon_->PreDrawLocalFog();

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
				if (isPlayerPart) continue; // 流体の場合は分身になることはないので単純スキップ

				int matType = obj->GetMaterialType();
				if (matType == 8) {
					obj->DrawWater(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 9) {
					obj->DrawMagma(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 10) {
					obj->DrawIce(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 11) {
					obj->DrawFire(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 12) {
					obj->DrawLaser(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 13) {
					obj->DrawSlimeGel(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 14) {
					obj->DrawShockwave(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 15) {
					obj->DrawLiquidContact(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 16) {
					obj->DrawDamageCrack(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 17) {
					obj->DrawUpdraft(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 18) {
					obj->DrawStunBind(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 19) {
					obj->DrawCrownUnlock(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 20) {
					obj->DrawPoisonSpore(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 21) {
					obj->DrawCloud(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 22) {
					obj->DrawGatePortal(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				else if (matType == 26) {
					obj->DrawWindOrb(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
				}
				obj->DrawOwnedSpecialMaterialVisuals(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
			}
			BulletManager::GetInstance()->DrawSpecial(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
		}

		if (hasGPUParticles) {
			gpuParticleManager->Draw(
				dxCommon_->GetCommandList(),
				camera->GetViewMatrix(),
				camera->GetProjectionMatrix(),
				gpuParticleTexHandle_,
				dxCommon_->GetDepthSrvHandle()
			);
		}
		dxCommon_->PostDrawLocalFog();
	}

	// =======================================================
	// 7. メッシュエフェクト（アタッチ済み）の描画
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
		if (!isCameraPreview && hasMeshEffects) {
			// GrabTextureがまだ更新されていなければ、ここで更新する
			if (!grabUpdated) {
				dxCommon_->UpdateGrabTexture();
			}
			// 各オブジェクトのアタッチ済みエフェクトを描画
			for (auto& obj : objects) {
				if (!obj->GetIsVisible()) continue;
				ID3D12Resource* ptLight = pointLightRes;
				ID3D12Resource* spLight = spotLightRes;
				obj->DrawAttachedEffects(ptLight, spLight);
			}
		}
	}
}
// ====================================================================
// UI描画専用の関数
// ====================================================================
void GamePlayScene::DrawUI() {
	// --- 4. 2D描画 (UIスプライト) ---
	spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    if (lifeLostPresentationActive_ || lifeLostBlackHold_) {
        DrawGameplayHUD();
        return;
    }

    // クリア演出中は通常HUDを隠し、カメラとリザルトへ視線を集中させます。
    if (isGoal_) {
        DrawGoalPresentationOverlay();
        return;
    }
	for (auto& sprite : sprites_) {
		if (sprite && !IsGameplayHUDSprite(sprite.get())) {
			sprite->Draw();
		}
	}
	if (isDrawLockOn_ && lockOnSprite_) {
		lockOnSprite_->Draw();
	}
	if (player_) {
		player_->DrawUI();
	}
	DrawGameplayHUD();
    if (controlsGuideOverlay_ && controlsGuideOverlay_->IsActive()) {
        controlsGuideOverlay_->Draw();
    }
    if (pauseMenuOverlay_ && pauseMenuOverlay_->IsActive() && !(settingsOverlay_ && settingsOverlay_->IsActive())) {
        pauseMenuOverlay_->Draw();
    }
	if (settingsOverlay_ && settingsOverlay_->IsActive()) {
		settingsOverlay_->Draw();
	}
    if (saveIndicatorOverlay_ && saveIndicatorOverlay_->IsActive()) {
        saveIndicatorOverlay_->Draw();
    }
	DrawGoalPresentationOverlay();
}

void GamePlayScene::CollectReplaySprites(std::vector<Sprite*>& replaySprites) {
	BaseScene::CollectReplaySprites(replaySprites);
	auto add = [&replaySprites](Sprite* sprite) {
		if (sprite) {
			replaySprites.push_back(sprite);
		}
	};

	add(lockOnSprite_.get());
	add(goalOverlayBackdrop_.get());
	add(goalOverlayFlash_.get());
	add(goalOverlayGlow_.get());
	add(goalOverlayTopLine_.get());
	add(goalOverlayBottomLine_.get());
	add(goalOverlayStageClearText_.get());
	add(goalOverlayReturnText_.get());
	for (const auto& sparkle : goalOverlaySparkles_) {
		add(sparkle.get());
	}
	if (pauseMenuOverlay_) {
		pauseMenuOverlay_->CollectReplaySprites(replaySprites);
	}
	if (controlsGuideOverlay_) {
		controlsGuideOverlay_->CollectReplaySprites(replaySprites);
	}
	if (settingsOverlay_) {
		settingsOverlay_->CollectReplaySprites(replaySprites);
	}
	if (saveIndicatorOverlay_) {
		saveIndicatorOverlay_->CollectReplaySprites(replaySprites);
	}
}

void GamePlayScene::CaptureReplaySceneState(json& state) const {
	json pauseState = json::object();
	json controlsGuideState = json::object();
	json settingsState = json::object();
	json saveIndicatorState = json::object();
	if (pauseMenuOverlay_) pauseMenuOverlay_->CaptureReplayState(pauseState);
	if (controlsGuideOverlay_) controlsGuideOverlay_->CaptureReplayState(controlsGuideState);
	if (settingsOverlay_) settingsOverlay_->CaptureReplayState(settingsState);
	if (saveIndicatorOverlay_) saveIndicatorOverlay_->CaptureReplayState(saveIndicatorState);

	state = {
		{ "goal", {
			{ "active", isGoal_ },
			{ "state", static_cast<int>(goalPresentationState_) },
			{ "timer", goalPresentationTimer_ },
			{ "starEmitTimer", goalStarEmitTimer_ },
			{ "burstEmitTimer", goalBurstEmitTimer_ },
			{ "crownIdleTime", goalCrownIdleTime_ },
			{ "crownSparkleTimer", goalCrownSparkleTimer_ },
			{ "crownSparklePattern", goalCrownSparklePatternIndex_ },
			{ "crownSpringPosition", { goalCrownSpringPosition_.x, goalCrownSpringPosition_.y, goalCrownSpringPosition_.z } },
			{ "crownSpringVelocity", { goalCrownSpringVelocity_.x, goalCrownSpringVelocity_.y, goalCrownSpringVelocity_.z } },
			{ "crownSpringRotation", { goalCrownSpringRotation_.x, goalCrownSpringRotation_.y, goalCrownSpringRotation_.z } },
			{ "crownSpringRotationVelocity", { goalCrownSpringRotationVelocity_.x, goalCrownSpringRotationVelocity_.y, goalCrownSpringRotationVelocity_.z } },
			{ "crownSpringInitialized", goalCrownSpringInitialized_ },
			{ "returnFadeStarted", goalReturnFadeStarted_ },
			{ "landingCuePlayed", goalLandingCuePlayed_ },
			{ "resultCuePlayed", goalResultCuePlayed_ },
			{ "cinematicPlaying", goalCinematicPlayer_.IsPlaying() }
		} },
		{ "hud", {
			{ "stageStarPulseTimers", hudStageStarPulseTimers_ },
			{ "stageStarCollected", hudStageStarVisualCollected_ },
			{ "lifeGainPulseTimer", hudLifeGainPulseTimer_ },
			{ "coinPulseTimer", hudCoinPulseTimer_ },
			{ "previousLives", hudPreviousLives_ },
			{ "previousCoins", hudPreviousCoins_ },
			{ "previousHp", hudPreviousHp_ },
			{ "damagePulseTimer", hudDamagePulseTimer_ },
			{ "hurtIconTimer", hudHurtIconTimer_ },
			{ "hpDamageHoldTimer", hudHpDamageHoldTimer_ },
			{ "hpDelayedRate", hudHpDelayedRate_ },
			{ "hpAnimationTimer", hudHpAnimationTimer_ },
			{ "morphGaugeTimer", hudMorphGaugeTimer_ },
			{ "morphGaugeVisibleTimer", hudMorphGaugeVisibleTimer_ }
		} },
		{ "lifeLost", {
			{ "active", lifeLostPresentationActive_ },
			{ "finished", lifeLostPresentationFinished_ },
			{ "blackHold", lifeLostBlackHold_ },
			{ "numberDropped", lifeLostNumberDropped_ },
			{ "timer", lifeLostPresentationTimer_ },
			{ "beforeLives", lifeLostBeforeLives_ },
			{ "afterLives", lifeLostAfterLives_ },
			{ "revive", lifeLostRevive_ }
		} },
		{ "overlays", {
			{ "pause", std::move(pauseState) },
			{ "controlsGuide", std::move(controlsGuideState) },
			{ "settings", std::move(settingsState) },
			{ "saveIndicator", std::move(saveIndicatorState) }
		} }
	};
}

void GamePlayScene::RestoreReplaySceneState(const json& state) {
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
		const json& goal = *found;
		isGoal_ = goal.value("active", isGoal_);
		const int stateValue = std::clamp(goal.value("state", static_cast<int>(goalPresentationState_)), 0, 3);
		goalPresentationState_ = static_cast<GoalPresentationState>(stateValue);
		goalPresentationTimer_ = goal.value("timer", goalPresentationTimer_);
		goalStarEmitTimer_ = goal.value("starEmitTimer", goalStarEmitTimer_);
		goalBurstEmitTimer_ = goal.value("burstEmitTimer", goalBurstEmitTimer_);
		goalCrownIdleTime_ = goal.value("crownIdleTime", goalCrownIdleTime_);
		goalCrownSparkleTimer_ = goal.value("crownSparkleTimer", goalCrownSparkleTimer_);
		goalCrownSparklePatternIndex_ = goal.value("crownSparklePattern", goalCrownSparklePatternIndex_);
		restoreVector3(goal, "crownSpringPosition", goalCrownSpringPosition_);
		restoreVector3(goal, "crownSpringVelocity", goalCrownSpringVelocity_);
		restoreVector3(goal, "crownSpringRotation", goalCrownSpringRotation_);
		restoreVector3(goal, "crownSpringRotationVelocity", goalCrownSpringRotationVelocity_);
		goalCrownSpringInitialized_ = goal.value("crownSpringInitialized", goalCrownSpringInitialized_);
		goalReturnFadeStarted_ = goal.value("returnFadeStarted", goalReturnFadeStarted_);
		goalLandingCuePlayed_ = goal.value("landingCuePlayed", goalLandingCuePlayed_);
		goalResultCuePlayed_ = goal.value("resultCuePlayed", goalResultCuePlayed_);

		if (goalCinematicTimelineLoaded_ && isGoal_ && goalPresentationState_ != GoalPresentationState::Inactive) {
			const bool shouldPlay = goal.value("cinematicPlaying", goalCinematicPlayer_.IsPlaying());
			goalCinematicPlayer_.SetTime(goalPresentationTimer_, false);
			if (shouldPlay && !goalCinematicPlayer_.IsPlaying()) {
				goalCinematicPlayer_.Resume(false);
			} else if (!shouldPlay && goalCinematicPlayer_.IsPlaying()) {
				goalCinematicPlayer_.Pause();
			}
		}
	}

	if (const auto found = state.find("hud"); found != state.end() && found->is_object()) {
		const json& hud = *found;
		if (const auto values = hud.find("stageStarPulseTimers"); values != hud.end() && values->is_array()) {
			for (size_t i = 0; i < hudStageStarPulseTimers_.size() && i < values->size(); ++i) {
				hudStageStarPulseTimers_[i] = (*values)[i].get<float>();
			}
		}
		if (const auto values = hud.find("stageStarCollected"); values != hud.end() && values->is_array()) {
			for (size_t i = 0; i < hudStageStarVisualCollected_.size() && i < values->size(); ++i) {
				hudStageStarVisualCollected_[i] = (*values)[i].get<bool>();
			}
		}
		hudLifeGainPulseTimer_ = hud.value("lifeGainPulseTimer", hudLifeGainPulseTimer_);
		hudCoinPulseTimer_ = hud.value("coinPulseTimer", hudCoinPulseTimer_);
		hudPreviousLives_ = hud.value("previousLives", hudPreviousLives_);
		hudPreviousCoins_ = hud.value("previousCoins", hudPreviousCoins_);
		hudPreviousHp_ = hud.value("previousHp", hudPreviousHp_);
		hudDamagePulseTimer_ = hud.value("damagePulseTimer", hudDamagePulseTimer_);
		hudHurtIconTimer_ = hud.value("hurtIconTimer", hudHurtIconTimer_);
		hudHpDamageHoldTimer_ = hud.value("hpDamageHoldTimer", hudHpDamageHoldTimer_);
		hudHpDelayedRate_ = hud.value("hpDelayedRate", hudHpDelayedRate_);
		hudHpAnimationTimer_ = hud.value("hpAnimationTimer", hudHpAnimationTimer_);
		hudMorphGaugeTimer_ = hud.value("morphGaugeTimer", hudMorphGaugeTimer_);
		hudMorphGaugeVisibleTimer_ = hud.value("morphGaugeVisibleTimer", hudMorphGaugeVisibleTimer_);
	}

	if (const auto found = state.find("lifeLost"); found != state.end() && found->is_object()) {
		const json& lifeLost = *found;
		lifeLostPresentationActive_ = lifeLost.value("active", lifeLostPresentationActive_);
		lifeLostPresentationFinished_ = lifeLost.value("finished", lifeLostPresentationFinished_);
		lifeLostBlackHold_ = lifeLost.value("blackHold", lifeLostBlackHold_);
		lifeLostNumberDropped_ = lifeLost.value("numberDropped", lifeLostNumberDropped_);
		lifeLostPresentationTimer_ = lifeLost.value("timer", lifeLostPresentationTimer_);
		lifeLostBeforeLives_ = lifeLost.value("beforeLives", lifeLostBeforeLives_);
		lifeLostAfterLives_ = lifeLost.value("afterLives", lifeLostAfterLives_);
		lifeLostRevive_ = lifeLost.value("revive", lifeLostRevive_);
	}

	if (const auto found = state.find("overlays"); found != state.end() && found->is_object()) {
		const json& overlays = *found;
		if (pauseMenuOverlay_ && overlays.contains("pause")) {
			pauseMenuOverlay_->RestoreReplayState(overlays["pause"]);
		}
		if (controlsGuideOverlay_ && overlays.contains("controlsGuide")) {
			controlsGuideOverlay_->RestoreReplayState(overlays["controlsGuide"]);
		}
		if (settingsOverlay_ && overlays.contains("settings")) {
			settingsOverlay_->RestoreReplayState(overlays["settings"]);
		}
		if (saveIndicatorOverlay_ && overlays.contains("saveIndicator")) {
			saveIndicatorOverlay_->RestoreReplayState(overlays["saveIndicator"]);
		}
	}
}

void GamePlayScene::InitializeGoalPresentationOverlay() {
	if (!spriteCommon_) {
		return;
	}

	const uint32_t whiteHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
	const uint32_t glowHandle = TextureManager::GetInstance()->Load("Resources/sprite/particle/glow_core.png");
	const uint32_t sparkleHandle = TextureManager::GetInstance()->Load("Resources/sprite/fade/fade_sparkle.png");
	const uint32_t clearTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/ui/result/clear/stage_clear_text.png");
	const uint32_t returnTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/ui/result/clear/returning_select_text.png");

	goalOverlayBackdrop_ = std::make_unique<Sprite>();
	goalOverlayBackdrop_->Initialize(spriteCommon_.get(), whiteHandle);
	goalOverlayBackdrop_->SetName("GoalClear_Backdrop");
	goalOverlayBackdrop_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayFlash_ = std::make_unique<Sprite>();
	goalOverlayFlash_->Initialize(spriteCommon_.get(), whiteHandle);
	goalOverlayFlash_->SetName("GoalClear_Flash");
	goalOverlayFlash_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayGlow_ = std::make_unique<Sprite>();
	goalOverlayGlow_->Initialize(spriteCommon_.get(), glowHandle);
	goalOverlayGlow_->SetName("GoalClear_Glow");
	goalOverlayGlow_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayTopLine_ = std::make_unique<Sprite>();
	goalOverlayTopLine_->Initialize(spriteCommon_.get(), whiteHandle);
	goalOverlayTopLine_->SetName("GoalClear_TopLine");
	goalOverlayTopLine_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayBottomLine_ = std::make_unique<Sprite>();
	goalOverlayBottomLine_->Initialize(spriteCommon_.get(), whiteHandle);
	goalOverlayBottomLine_->SetName("GoalClear_BottomLine");
	goalOverlayBottomLine_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayStageClearText_ = std::make_unique<Sprite>();
	goalOverlayStageClearText_->Initialize(spriteCommon_.get(), clearTextHandle);
	goalOverlayStageClearText_->SetName("GoalClear_Title");
	goalOverlayStageClearText_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayReturnText_ = std::make_unique<Sprite>();
	goalOverlayReturnText_->Initialize(spriteCommon_.get(), returnTextHandle);
	goalOverlayReturnText_->SetName("GoalClear_ReturnText");
	goalOverlayReturnText_->SetAnchorPoint({ 0.5f, 0.5f });

	for (size_t i = 0; i < goalOverlaySparkles_.size(); ++i) {
		auto& sparkle = goalOverlaySparkles_[i];
		sparkle = std::make_unique<Sprite>();
		sparkle->Initialize(spriteCommon_.get(), sparkleHandle);
		sparkle->SetName("GoalClear_Sparkle_" + std::to_string(i));
		sparkle->SetAnchorPoint({ 0.5f, 0.5f });
	}

	UpdateGoalPresentationOverlay();
}

void GamePlayScene::UpdateGoalPresentationOverlay() {
	const bool visible = isGoal_ && goalPresentationState_ != GoalPresentationState::Inactive;
	const float screenW = static_cast<float>(WinApp::kClientWidth);
	const float screenH = static_cast<float>(WinApp::kClientHeight);
	const float t = goalPresentationTimer_;
	const GoalClearPlayerAnimator::Tuning& animation = goalClearPlayerAnimator_.GetTuning();
	const GoalPresentationTuning& presentation = goalPresentationTuning_;
	const float baseScale = std::min(screenW / 1280.0f, screenH / 720.0f);
	const float uiScale = baseScale * presentation.resultUiScale;
	const float centerX = screenW * presentation.resultUiCenterX;
	const float centerY = screenH * presentation.resultUiCenterY;
	const float backdropIn = GoalEaseInOut((t - animation.resultUiTime + 0.28f) / 0.28f);
	const float resultIn = GoalEaseOut((t - animation.resultUiTime) / 0.34f);
	const float titlePhase = GoalClamp01((t - animation.resultUiTime) / 0.36f);
	const float titleScale = 0.72f + 0.28f * resultIn + std::sin(titlePhase * std::numbers::pi_v<float>) * 0.10f;
	const float returnStartTime = std::max(animation.resultUiTime + 0.52f, animation.readyTime - 0.48f);
	const float returnIn = GoalEaseInOut((t - returnStartTime) / 0.34f);
	const float flashIn = GoalEaseOut((t - animation.resultUiTime) / 0.04f);
	const float flashOut = 1.0f - GoalEaseOut((t - animation.resultUiTime - 0.06f) / 0.30f);
	const float flash = visible ? flashIn * flashOut : 0.0f;
	const float textFloat = std::sin((t - animation.resultUiTime) * 3.2f) * 2.0f * resultIn * uiScale;

	if (goalOverlayBackdrop_) {
		goalOverlayBackdrop_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
		goalOverlayBackdrop_->SetSize({ screenW + 8.0f, screenH + 8.0f });
		goalOverlayBackdrop_->SetColor({ 0.005f, 0.008f, 0.025f, visible ? presentation.resultBackdropAlpha * backdropIn : 0.0f });
		goalOverlayBackdrop_->Update();
	}
	if (goalOverlayFlash_) {
		goalOverlayFlash_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
		goalOverlayFlash_->SetSize({ screenW + 8.0f, screenH + 8.0f });
		goalOverlayFlash_->SetColor({ 1.0f, 0.82f, 0.38f, flash * 0.20f });
		goalOverlayFlash_->Update();
	}
	if (goalOverlayGlow_) {
		const float glowScale = 0.78f + 0.22f * resultIn;
		goalOverlayGlow_->SetPosition({ centerX, centerY });
		goalOverlayGlow_->SetSize({ 980.0f * uiScale * glowScale, 300.0f * uiScale * glowScale });
		goalOverlayGlow_->SetColor({ 1.0f, 0.64f, 0.18f, visible ? presentation.resultGlowAlpha * backdropIn : 0.0f });
		goalOverlayGlow_->Update();
	}
	const float lineWidth = 760.0f * uiScale * resultIn;
	if (goalOverlayTopLine_) {
		goalOverlayTopLine_->SetPosition({ centerX, centerY - 76.0f * uiScale });
		goalOverlayTopLine_->SetSize({ lineWidth, 2.0f * uiScale });
		goalOverlayTopLine_->SetColor({ 1.0f, 0.86f, 0.38f, visible ? 0.62f * resultIn : 0.0f });
		goalOverlayTopLine_->Update();
	}
	if (goalOverlayBottomLine_) {
		goalOverlayBottomLine_->SetPosition({ centerX, centerY + 72.0f * uiScale });
		goalOverlayBottomLine_->SetSize({ lineWidth * 0.82f, 2.0f * uiScale });
		goalOverlayBottomLine_->SetColor({ 1.0f, 0.76f, 0.22f, visible ? 0.44f * resultIn : 0.0f });
		goalOverlayBottomLine_->Update();
	}
	if (goalOverlayStageClearText_) {
		goalOverlayStageClearText_->SetPosition({ centerX, centerY + 20.0f * (1.0f - resultIn) * uiScale + textFloat });
		goalOverlayStageClearText_->SetSize({ 780.0f * uiScale * titleScale, 140.0f * uiScale * titleScale });
		goalOverlayStageClearText_->SetRotation(0.0f);
		goalOverlayStageClearText_->SetColor({ 1.0f, 1.0f, 1.0f, visible ? resultIn : 0.0f });
		goalOverlayStageClearText_->Update();
	}
	if (goalOverlayReturnText_) {
		goalOverlayReturnText_->SetPosition({ centerX, centerY + (112.0f + 12.0f * (1.0f - returnIn)) * uiScale });
		goalOverlayReturnText_->SetSize({ 358.0f * uiScale, 43.0f * uiScale });
		goalOverlayReturnText_->SetRotation(0.0f);
		goalOverlayReturnText_->SetColor({ 1.0f, 0.96f, 0.80f, visible ? returnIn * 0.86f : 0.0f });
		goalOverlayReturnText_->Update();
	}

	static const std::array<Vector2, 8> kSparkOffsets = {
		Vector2{ -420.0f, -64.0f }, Vector2{ -342.0f, 72.0f }, Vector2{ -236.0f, -96.0f },
		Vector2{ 242.0f, -92.0f }, Vector2{ 346.0f, 70.0f }, Vector2{ 424.0f, -58.0f },
		Vector2{ -116.0f, 92.0f }, Vector2{ 126.0f, 96.0f }
	};
	for (size_t i = 0; i < goalOverlaySparkles_.size(); ++i) {
		Sprite* sparkle = goalOverlaySparkles_[i].get();
		if (!sparkle) {
			continue;
		}
		const float localTime = std::max(0.0f, t - animation.resultUiTime - static_cast<float>(i) * 0.035f);
		const float sparkleIn = GoalEaseOut(localTime / 0.18f) * resultIn;
		const float wave = 0.5f + 0.5f * std::sin(localTime * (5.2f + static_cast<float>(i) * 0.23f) + static_cast<float>(i) * 0.9f);
		const float shimmer = 0.28f + 0.72f * std::pow(wave, 3.0f);
		const float baseSize = (i % 3 == 0) ? 42.0f : 28.0f;
		const float drift = std::sin(localTime * 1.8f + static_cast<float>(i)) * 4.0f * uiScale;
		sparkle->SetPosition({
			centerX + kSparkOffsets[i].x * uiScale + drift,
			centerY + kSparkOffsets[i].y * uiScale - localTime * 3.0f * uiScale
		});
		sparkle->SetSize({ baseSize * uiScale * (0.76f + 0.30f * shimmer), baseSize * uiScale * (0.76f + 0.30f * shimmer) });
		sparkle->SetRotation(localTime * (0.25f + static_cast<float>(i) * 0.035f));
		const bool coolSparkle = (i % 3) == 1;
		sparkle->SetColor({ coolSparkle ? 0.72f : 1.0f, coolSparkle ? 0.92f : 0.86f, 1.0f, visible ? sparkleIn * shimmer * 0.92f : 0.0f });
		sparkle->Update();
	}
}

void GamePlayScene::DrawGoalPresentationOverlay() {
	if (!isGoal_ || goalPresentationState_ == GoalPresentationState::Inactive) {
		return;
	}

	if (goalOverlayBackdrop_) {
		goalOverlayBackdrop_->Draw();
	}
	if (goalOverlayFlash_) {
		goalOverlayFlash_->Draw();
	}
	if (goalOverlayGlow_) {
		goalOverlayGlow_->Draw();
	}
	if (goalOverlayTopLine_) {
		goalOverlayTopLine_->Draw();
	}
	if (goalOverlayBottomLine_) {
		goalOverlayBottomLine_->Draw();
	}
	for (auto& sparkle : goalOverlaySparkles_) {
		if (sparkle) {
			sparkle->Draw();
		}
	}
	if (goalOverlayStageClearText_) {
		goalOverlayStageClearText_->Draw();
	}
	if (goalOverlayReturnText_) {
		goalOverlayReturnText_->Draw();
	}
}


void GamePlayScene::DrawShadow() {
	if (objectManager_) {

		objectManager_->DrawShadow();
	}
}

void GamePlayScene::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_INFO_CIRCLE " Scene: GamePlay");
    ImGui::Separator();

    if (ImGui::CollapsingHeader(ICON_FA_MAP " Stage Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& stages = StageManager::GetInstance()->GetStages();
        int currentIndex = StageManager::GetInstance()->GetCurrentStageIndex();

        ImGui::Text("ステージJSON切り替え");
        constexpr int kStageButtonCount = 5;
        for (int stageIndex = 0; stageIndex < kStageButtonCount; ++stageIndex) {
            const bool registered = stageIndex < static_cast<int>(stages.size());
            const bool isCurrent = stageIndex == currentIndex;
            if (!registered || isCurrent) {
                ImGui::BeginDisabled();
            }

            std::string label = std::string(ICON_FA_PLAY) + " Stage " + std::to_string(stageIndex + 1);
            if (ImGui::Button(label.c_str(), ImVec2(112.0f, 30.0f)) && registered) {
                StageManager::GetInstance()->SetCurrentStage(stageIndex);
                SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            }

            if (!registered || isCurrent) {
                ImGui::EndDisabled();
            }

            if (stageIndex < kStageButtonCount - 1) {
                ImGui::SameLine();
            }
        }

        if (currentIndex >= 0 && currentIndex < static_cast<int>(stages.size())) {
            const StageData& currentStage = stages[currentIndex];
            ImGui::TextDisabled("現在: %s", currentStage.name.c_str());
            ImGui::TextDisabled("3D: %s", currentStage.levelPath.c_str());
            ImGui::TextDisabled("Sprite: %s", currentStage.spritePath.c_str());
        }
        ImGui::Separator();

        std::vector<const char*> stageNames;
        for (const auto& s : stages) stageNames.push_back(s.name.c_str());

        if (ImGui::Combo("Select Stage", &currentIndex, stageNames.data(), (int)stageNames.size())) {
            StageManager::GetInstance()->SetCurrentStage(currentIndex);
        }

        if (ImGui::Button(ICON_FA_SYNC " Reload Scene with Selected Stage", ImVec2(-1, 30))) {
            SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_TROPHY " Stage Status", ImGuiTreeNodeFlags_DefaultOpen)) {
        int currentStage = StageManager::GetInstance()->GetCurrentStageIndex();
        bool isCleared = GameDataManager::GetInstance()->IsStageCleared(currentStage);

        ImGui::Text("Stage ID: %d", currentStage);
        ImGui::SameLine();
        if (isCleared) ImGui::TextColored(ImVec4(0, 1, 0, 1), "[ CLEARED ]");
        else ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "[ NOT CLEARED ]");

        ImGui::Separator();
        ImGui::Text("Star Coins (Session):");
        for (int i = 0; i < 3; i++) {
            ImGui::SameLine();
            if (sessionStarCoins_[i]) {
                ImGui::TextColored(ImVec4(1, 0.9f, 0, 1), ICON_FA_STAR);
            }
            else {
                ImGui::TextDisabled(ICON_FA_STAR);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Star Coin %d", i);
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_USER " Player Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (player_) {
            Vector3 pos = player_->GetTranslate();
            ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
            
            float hp = player_->GetHp();
            float maxHp = player_->GetMaxHp();
            ImGui::ProgressBar(hp / maxHp, ImVec2(-1, 0), "HP");

            // --- Debug HP Control ---
            if (ImGui::Button(ICON_FA_AMBULANCE " HPを1にする (Debug)")) {
                if (player_->param_.has_value()) {
                    player_->param_->hp = 1.0f;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_SKULL " HPを0にする (Debug)")) {
                if (player_->param_.has_value()) {
                    player_->param_->hp = 0.0f;
                }
            }

            ImGui::Separator();
            int lives = GameDataManager::GetInstance()->GetLives();
            int coins = GameDataManager::GetInstance()->GetCoins();
            ImGui::Text(ICON_FA_HEART " Remaining Lives: %d", lives);
            ImGui::Text(ICON_FA_COINS " Coins: %d / 100", coins);
            if (ImGui::Button("Reset Lives to 3")) {
                GameDataManager::GetInstance()->ResetLives();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Coins")) {
                GameDataManager::GetInstance()->ResetCoins();
            }
        }
        else {
            ImGui::TextDisabled("Player not found");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_GHOST " Scene Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(ICON_FA_PLAY " Start Bridge Drop Movie", ImVec2(-1, 30))) {
            StartBridgeDropMovie();
        }
    }

    if (saveIndicatorOverlay_ && ImGui::CollapsingHeader(ICON_FA_SAVE " Save Indicator", ImGuiTreeNodeFlags_DefaultOpen)) {
        saveIndicatorOverlay_->DrawImGui();
    }

    DrawGoalPresentationEditor();
    
    ImGui::Separator();
    ImGui::TextDisabled("※この項目は GamePlayScene::DrawImGui() で編集可能です");
#endif
}

void GamePlayScene::LoadGoalPresentationTuning() {
    std::ifstream file(kGoalPresentationTuningPath, std::ios::binary);
    if (!file.is_open()) {
        SanitizeGoalPresentationTuning();
        return;
    }

    try {
        nlohmann::json root;
        file >> root;
        GoalClearPlayerAnimator::Tuning animation = goalClearPlayerAnimator_.GetTuning();

        if (const auto it = root.find("timeline"); it != root.end() && it->is_object()) {
            const auto& j = *it;
            goalPresentationTuning_.crownFocusEndTime = j.value("crownFocusEndTime", goalPresentationTuning_.crownFocusEndTime);
            goalPresentationTuning_.crownMoveStartTime = j.value("crownMoveStartTime", goalPresentationTuning_.crownMoveStartTime);
            animation.crownLandTime = j.value("crownLandTime", animation.crownLandTime);
            animation.anticipationStartTime = j.value("anticipationStartTime", animation.anticipationStartTime);
            animation.jumpStartTime = j.value("jumpStartTime", animation.jumpStartTime);
            animation.apexTime = j.value("apexTime", animation.apexTime);
            animation.resultUiTime = j.value("resultUiTime", animation.resultUiTime);
            animation.readyTime = j.value("readyTime", animation.readyTime);
        }
        if (const auto it = root.find("player"); it != root.end() && it->is_object()) {
            const auto& j = *it;
            animation.jumpHeight = j.value("jumpHeight", animation.jumpHeight);
            animation.forwardDistance = j.value("forwardDistance", animation.forwardDistance);
            animation.anticipationDepth = j.value("anticipationDepth", animation.anticipationDepth);
            animation.landingSquash = j.value("landingSquash", animation.landingSquash);
            animation.anticipationSquash = j.value("anticipationSquash", animation.anticipationSquash);
            animation.takeoffStretch = j.value("takeoffStretch", animation.takeoffStretch);
            animation.resultStretch = j.value("resultStretch", animation.resultStretch);
            animation.resultYawBias = j.value("resultYawBias", animation.resultYawBias);
        }
        if (const auto it = root.find("crown"); it != root.end() && it->is_object()) {
            const auto& j = *it;
            goalPresentationTuning_.crownDropHeight = j.value("dropHeight", goalPresentationTuning_.crownDropHeight);
            goalPresentationTuning_.crownSeatDepth = j.value("seatDepth", goalPresentationTuning_.crownSeatDepth);
        }
        if (const auto it = root.find("camera"); it != root.end() && it->is_object()) {
            const auto& j = *it;
            goalPresentationTuning_.crownFocusDistance = j.value("crownFocusDistance", goalPresentationTuning_.crownFocusDistance);
            goalPresentationTuning_.crownFocusSide = j.value("crownFocusSide", goalPresentationTuning_.crownFocusSide);
            goalPresentationTuning_.crownFocusHeight = j.value("crownFocusHeight", goalPresentationTuning_.crownFocusHeight);
            goalPresentationTuning_.landingCameraDistance = j.value("landingDistance", goalPresentationTuning_.landingCameraDistance);
            goalPresentationTuning_.landingCameraSide = j.value("landingSide", goalPresentationTuning_.landingCameraSide);
            goalPresentationTuning_.landingCameraHeight = j.value("landingHeight", goalPresentationTuning_.landingCameraHeight);
            goalPresentationTuning_.jumpCameraDistance = j.value("jumpDistance", goalPresentationTuning_.jumpCameraDistance);
            goalPresentationTuning_.jumpCameraSide = j.value("jumpSide", goalPresentationTuning_.jumpCameraSide);
            goalPresentationTuning_.jumpCameraHeight = j.value("jumpHeight", goalPresentationTuning_.jumpCameraHeight);
            goalPresentationTuning_.resultCameraDistance = j.value("resultDistance", goalPresentationTuning_.resultCameraDistance);
            goalPresentationTuning_.resultCameraSide = j.value("resultSide", goalPresentationTuning_.resultCameraSide);
            goalPresentationTuning_.resultCameraHeight = j.value("resultHeight", goalPresentationTuning_.resultCameraHeight);
            goalPresentationTuning_.resultTargetSide = j.value("resultTargetSide", goalPresentationTuning_.resultTargetSide);
            goalPresentationTuning_.crownFocusFov = j.value("crownFocusFov", goalPresentationTuning_.crownFocusFov);
            goalPresentationTuning_.landingFov = j.value("landingFov", goalPresentationTuning_.landingFov);
            goalPresentationTuning_.jumpFov = j.value("jumpFov", goalPresentationTuning_.jumpFov);
            goalPresentationTuning_.resultFov = j.value("resultFov", goalPresentationTuning_.resultFov);
        }
        if (const auto it = root.find("ui"); it != root.end() && it->is_object()) {
            const auto& j = *it;
            goalPresentationTuning_.resultUiCenterX = j.value("centerX", goalPresentationTuning_.resultUiCenterX);
            goalPresentationTuning_.resultUiCenterY = j.value("centerY", goalPresentationTuning_.resultUiCenterY);
            goalPresentationTuning_.resultUiScale = j.value("scale", goalPresentationTuning_.resultUiScale);
            goalPresentationTuning_.resultBackdropAlpha = j.value("backdropAlpha", goalPresentationTuning_.resultBackdropAlpha);
            goalPresentationTuning_.resultGlowAlpha = j.value("glowAlpha", goalPresentationTuning_.resultGlowAlpha);
        }

        goalClearPlayerAnimator_.SetTuning(animation);
        SanitizeGoalPresentationTuning();
    } catch (const std::exception& e) {
        DebugConsole::GetInstance()->AddLog(std::string("Goal presentation settings load failed: ") + e.what());
        SanitizeGoalPresentationTuning();
    }
}

void GamePlayScene::SaveGoalPresentationTuning() const {
    const GoalClearPlayerAnimator::Tuning& a = goalClearPlayerAnimator_.GetTuning();
    const GoalPresentationTuning& g = goalPresentationTuning_;
    nlohmann::json root;
    root["version"] = 1;
    root["timeline"] = {
        {"crownFocusEndTime", g.crownFocusEndTime}, {"crownMoveStartTime", g.crownMoveStartTime},
        {"crownLandTime", a.crownLandTime},
        {"anticipationStartTime", a.anticipationStartTime}, {"jumpStartTime", a.jumpStartTime},
        {"apexTime", a.apexTime}, {"resultUiTime", a.resultUiTime}, {"readyTime", a.readyTime}
    };
    root["player"] = {
        {"jumpHeight", a.jumpHeight}, {"forwardDistance", a.forwardDistance},
        {"anticipationDepth", a.anticipationDepth}, {"landingSquash", a.landingSquash},
        {"anticipationSquash", a.anticipationSquash}, {"takeoffStretch", a.takeoffStretch},
        {"resultStretch", a.resultStretch}, {"resultYawBias", a.resultYawBias}
    };
    root["crown"] = {
        {"dropHeight", g.crownDropHeight}, {"seatDepth", g.crownSeatDepth}
    };
    root["camera"] = {
        {"crownFocusDistance", g.crownFocusDistance}, {"crownFocusSide", g.crownFocusSide},
        {"crownFocusHeight", g.crownFocusHeight}, {"landingDistance", g.landingCameraDistance},
        {"landingSide", g.landingCameraSide}, {"landingHeight", g.landingCameraHeight},
        {"jumpDistance", g.jumpCameraDistance}, {"jumpSide", g.jumpCameraSide},
        {"jumpHeight", g.jumpCameraHeight}, {"resultDistance", g.resultCameraDistance},
        {"resultSide", g.resultCameraSide}, {"resultHeight", g.resultCameraHeight},
        {"resultTargetSide", g.resultTargetSide}, {"crownFocusFov", g.crownFocusFov},
        {"landingFov", g.landingFov}, {"jumpFov", g.jumpFov}, {"resultFov", g.resultFov}
    };
    root["ui"] = {
        {"centerX", g.resultUiCenterX}, {"centerY", g.resultUiCenterY},
        {"scale", g.resultUiScale}, {"backdropAlpha", g.resultBackdropAlpha},
        {"glowAlpha", g.resultGlowAlpha}
    };

    std::ofstream file(kGoalPresentationTuningPath, std::ios::binary);
    if (file.is_open()) {
        file << root.dump(4);
    }
}

void GamePlayScene::SanitizeGoalPresentationTuning() {
    GoalClearPlayerAnimator::Tuning a = goalClearPlayerAnimator_.GetTuning();
    GoalPresentationTuning& g = goalPresentationTuning_;
    g.crownFocusEndTime = std::clamp(g.crownFocusEndTime, 0.20f, 1.50f);
    g.crownMoveStartTime = std::max(g.crownMoveStartTime, g.crownFocusEndTime + 0.12f);
    a.crownLandTime = std::max(a.crownLandTime, g.crownMoveStartTime + 0.70f);
    a.anticipationStartTime = std::max(a.anticipationStartTime, a.crownLandTime + 0.06f);
    a.jumpStartTime = std::max(a.jumpStartTime, a.anticipationStartTime + 0.10f);
    a.apexTime = std::max(a.apexTime, a.jumpStartTime + 0.36f);
    a.resultUiTime = std::max(a.resultUiTime, a.apexTime + 0.02f);
    a.readyTime = std::max(a.readyTime, a.resultUiTime + 0.50f);
    a.jumpHeight = std::clamp(a.jumpHeight, 1.0f, 7.0f);
    a.forwardDistance = std::clamp(a.forwardDistance, 0.0f, 3.0f);
    a.anticipationDepth = std::clamp(a.anticipationDepth, 0.02f, 0.65f);
    a.landingSquash = std::clamp(a.landingSquash, 0.0f, 0.40f);
    a.anticipationSquash = std::clamp(a.anticipationSquash, 0.0f, 0.50f);
    a.takeoffStretch = std::clamp(a.takeoffStretch, 0.0f, 0.65f);
    a.resultStretch = std::clamp(a.resultStretch, 0.0f, 0.35f);
    g.crownDropHeight = std::clamp(g.crownDropHeight, 0.30f, 4.0f);
    g.crownSeatDepth = std::clamp(g.crownSeatDepth, 0.0f, 0.45f);
    g.crownFocusFov = std::clamp(g.crownFocusFov, 0.30f, 1.10f);
    g.landingFov = std::clamp(g.landingFov, 0.30f, 1.10f);
    g.jumpFov = std::clamp(g.jumpFov, 0.30f, 1.10f);
    g.resultFov = std::clamp(g.resultFov, 0.30f, 1.10f);
    g.resultUiCenterX = std::clamp(g.resultUiCenterX, 0.30f, 0.70f);
    g.resultUiCenterY = std::clamp(g.resultUiCenterY, 0.45f, 0.82f);
    g.resultUiScale = std::clamp(g.resultUiScale, 0.60f, 1.35f);
    g.resultBackdropAlpha = std::clamp(g.resultBackdropAlpha, 0.0f, 0.65f);
    g.resultGlowAlpha = std::clamp(g.resultGlowAlpha, 0.0f, 0.45f);
    goalClearPlayerAnimator_.SetTuning(a);
}

void GamePlayScene::DrawGoalPresentationEditor() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader(ICON_FA_FILM " ゴール演出シーケンス", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    GoalClearPlayerAnimator::Tuning& a = goalClearPlayerAnimator_.EditTuning();
    GoalPresentationTuning& g = goalPresentationTuning_;
    bool changed = false;
    ImGui::TextUnformatted("タイムライン");
    changed |= ImGui::DragFloat("王冠フォーカス完了", &g.crownFocusEndTime, 0.01f, 0.20f, 1.50f, "%.2f 秒");
    changed |= ImGui::DragFloat("王冠移動開始", &g.crownMoveStartTime, 0.01f, 0.30f, 2.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("王冠着地", &a.crownLandTime, 0.01f, 1.00f, 4.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("溜め開始", &a.anticipationStartTime, 0.01f, 1.10f, 4.50f, "%.2f 秒");
    changed |= ImGui::DragFloat("大ジャンプ開始", &a.jumpStartTime, 0.01f, 1.20f, 5.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("頂点・静止", &a.apexTime, 0.01f, 1.60f, 6.50f, "%.2f 秒");
    changed |= ImGui::DragFloat("リザルト表示", &a.resultUiTime, 0.01f, 1.70f, 7.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("帰還案内開始", &a.readyTime, 0.01f, 2.20f, 8.00f, "%.2f 秒");

    ImGui::Separator();
    ImGui::TextUnformatted("プレイヤー");
    changed |= ImGui::DragFloat("ジャンプ高さ", &a.jumpHeight, 0.02f, 1.0f, 7.0f, "%.2f");
    changed |= ImGui::DragFloat("前進距離", &a.forwardDistance, 0.01f, 0.0f, 3.0f, "%.2f");
    changed |= ImGui::DragFloat("溜め沈み", &a.anticipationDepth, 0.005f, 0.02f, 0.65f, "%.3f");
    changed |= ImGui::DragFloat("王冠着地の潰れ", &a.landingSquash, 0.005f, 0.0f, 0.40f, "%.3f");
    changed |= ImGui::DragFloat("ジャンプ前の潰れ", &a.anticipationSquash, 0.005f, 0.0f, 0.50f, "%.3f");
    changed |= ImGui::DragFloat("離陸の伸び", &a.takeoffStretch, 0.005f, 0.0f, 0.65f, "%.3f");
    changed |= ImGui::DragFloat("決め姿勢の伸び", &a.resultStretch, 0.005f, 0.0f, 0.35f, "%.3f");

    ImGui::Separator();
    ImGui::TextUnformatted("王冠");
    changed |= ImGui::DragFloat("降下開始高さ", &g.crownDropHeight, 0.01f, 0.30f, 4.0f, "%.2f");
    changed |= ImGui::DragFloat("頭への沈み", &g.crownSeatDepth, 0.005f, 0.0f, 0.45f, "%.3f");

    ImGui::Separator();
    ImGui::TextUnformatted("カメラ");
    changed |= ImGui::DragFloat3("王冠ショット 距離/横/高さ", &g.crownFocusDistance, 0.01f, -10.0f, 10.0f, "%.2f");
    changed |= ImGui::DragFloat3("着地ショット 距離/横/高さ", &g.landingCameraDistance, 0.01f, -12.0f, 12.0f, "%.2f");
    changed |= ImGui::DragFloat3("ジャンプショット 距離/横/高さ", &g.jumpCameraDistance, 0.01f, -14.0f, 14.0f, "%.2f");
    changed |= ImGui::DragFloat3("リザルト 距離/横/高さ", &g.resultCameraDistance, 0.01f, -16.0f, 16.0f, "%.2f");
    changed |= ImGui::DragFloat("リザルト注視点の横ずらし", &g.resultTargetSide, 0.01f, -4.0f, 4.0f, "%.2f");
    changed |= ImGui::DragFloat4("FOV 王冠/着地/ジャンプ/結果", &g.crownFocusFov, 0.005f, 0.30f, 1.10f, "%.3f");

    ImGui::Separator();
    ImGui::TextUnformatted("クリアUI");
    changed |= ImGui::DragFloat("表示位置 X", &g.resultUiCenterX, 0.005f, 0.30f, 0.70f, "%.3f");
    changed |= ImGui::DragFloat("表示位置 Y", &g.resultUiCenterY, 0.005f, 0.45f, 0.82f, "%.3f");
    changed |= ImGui::DragFloat("UIスケール", &g.resultUiScale, 0.005f, 0.60f, 1.35f, "%.3f");
    changed |= ImGui::DragFloat("背景暗転", &g.resultBackdropAlpha, 0.005f, 0.0f, 0.65f, "%.3f");
    changed |= ImGui::DragFloat("中央グロー", &g.resultGlowAlpha, 0.005f, 0.0f, 0.45f, "%.3f");

    if (changed) {
        SanitizeGoalPresentationTuning();
        SyncGoalCinematicTimelineFromTuning();
    }

    if (ImGui::Button(ICON_FA_PLAY " 実ステージでプレビュー")) {
        StartGoalPresentationPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " プレビュー停止")) {
        StopGoalPresentationPreview();
    }
    if (ImGui::Button(ICON_FA_SAVE " JSON保存")) {
        SanitizeGoalPresentationTuning();
        SyncGoalCinematicTimelineFromTuning();
        SaveGoalPresentationTuning();
        goalCinematicSequence_.Save(kGoalCinematicTimelinePath);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " JSON再読込")) {
        LoadGoalPresentationTuning();
        SyncGoalCinematicTimelineFromTuning();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(isGoal_);
    if (ImGui::Button(ICON_FA_FILM " Timeline再読込")) {
        goalCinematicTimelineLoaded_ = goalCinematicSequence_.Load(kGoalCinematicTimelinePath);
        if (goalCinematicTimelineLoaded_) {
            ApplyGoalCinematicTimingFromSequence();
            goalCinematicPlayer_.SetSequence(&goalCinematicSequence_);
        }
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("保存先: %s", kGoalPresentationTuningPath);
    ImGui::TextDisabled("Timeline: %s", kGoalCinematicTimelinePath);
    ImGui::TextColored(
        goalCinematicTimelineLoaded_ ? ImVec4(0.45f, 0.92f, 0.55f, 1.0f) : ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
        goalCinematicTimelineLoaded_ ? "Timeline Runtime: Ready" : "Timeline Runtime: Load Failed");
#endif
}

Object3d* GamePlayScene::FindGoalCrownObject() const {
    if (!objectManager_) {
        return nullptr;
    }
    for (const auto& object : objectManager_->GetObjects()) {
        if (object && object->GetEventType() == EventType::Goal) {
            return object.get();
        }
    }
    return nullptr;
}

void GamePlayScene::StartGoalPresentationPreview() {
    if (isGoal_) {
        if (!goalEditorPreviewMode_) {
            DebugConsole::GetInstance()->AddLog("Goal presentation is already running in gameplay.");
            return;
        }
        StopGoalPresentationPreview();
    }
    Object3d* crown = FindGoalCrownObject();
    if (!crown || !player_) {
        DebugConsole::GetInstance()->AddLog("Goal presentation preview requires player and crown.");
        return;
    }
    goalEditorPreviewMode_ = true;
    StartGoalPresentation(crown);
    goalSavePerformed_ = true;
}

void GamePlayScene::StopGoalPresentationPreview() {
    if (!goalEditorPreviewMode_) {
        return;
    }
    Camera* restoreCamera = goalLockedPrimaryCamera_ ? goalLockedPrimaryCamera_ : CameraManager::GetInstance()->GetMainCamera();
    SetIsGoal(false);
    if (restoreCamera) {
        CameraManager::GetInstance()->SetActiveCamera(restoreCamera);
    }
    goalEditorPreviewMode_ = false;
}

void GamePlayScene::StartBridgeDropMovie() {

}
bool GamePlayScene::IsVisible(Object3d* obj) {
    if (!obj) return false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return true;

    // モデルデータに基づいた正確なワールド空間AABBを取得
    AABB worldAabb = obj->GetModelWorldAABB();

    const bool visible = Math::IntersectFrustumAABB(camera->GetFrustum(), worldAabb.min, worldAabb.max);
    if (!visible) {
        RenderStats::GetInstance()->RecordCulledObject();
    }
    return visible;
}
