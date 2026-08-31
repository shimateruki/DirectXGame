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
#include <EnemyPrismSlime.h>
#include <EnemyFalseKingSlime.h>
#include <EnemyFactory.h>
#include <EnemySpawner.h>
#include <GimmickFactory.h>
#include <ItemFactory.h>
#include <LightEditor.h>
#include <ParticleManager.h>
#include <GPUParticleManager.h>
#include <SrvManager.h>
#include <PostEffect.h>
#include "Fade.h"
#include "StageManager.h"
#include "GameDataManager.h"
#include "PlayerState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

std::unique_ptr<Object3d> GamePlayScene::CreateReplayObject(const json& descriptor) {
    Object3dCommon* common = GetObject3dCommon();
    if (!common || !descriptor.is_object()) {
        return nullptr;
    }

    const std::string className = descriptor.value("className", std::string{});
    const std::string enemyType = descriptor.value("enemyType", std::string{});
    const std::string gimmickType = descriptor.value("gimmickType", std::string{});
    const std::string itemType = descriptor.value("itemType", std::string{});
    std::unique_ptr<Object3d> object;

    if (!enemyType.empty()) {
        object = EnemyFactory::GetInstance()->CreateEnemy(enemyType, common);
        if (!object || object->GetEnemyType() != enemyType) {
            return nullptr;
        }
    } else if (!gimmickType.empty()) {
        object = GimmickFactory::GetInstance()->CreateGimmick(gimmickType, common);
    } else if (!itemType.empty()) {
        object = ItemFactory::GetInstance()->CreateItem(itemType, common);
        if (!object || object->GetItemType() != itemType) {
            return nullptr;
        }
    } else if (className == "Model" || className == "Object") {
        object = std::make_unique<Object3d>();
        object->Initialize(common);
    }

    if (!object) {
        return nullptr;
    }

    object->SetName(descriptor.value("name", object->GetName()));
    object->SetClassName(className.empty() ? object->GetClassName() : className);
    object->SetSaveCategory(descriptor.value("saveCategory", object->GetSaveCategory()));
    if (!enemyType.empty()) object->SetEnemyType(enemyType);
    if (!gimmickType.empty()) object->SetGimmickType(gimmickType);
    if (!itemType.empty()) object->SetItemType(itemType);
    return object;
}

void GamePlayScene::OnReplayObjectsRecreated() {
    LevelLoader::ConfigureEnemyRuntimeReferences(this);
}

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

#ifdef USE_IMGUI
constexpr const char* kDebugPrismArenaAnchorName = "Stage1_Collision_V4_PreBossCheckpoint";
constexpr const char* kDebugPrismSlimeName = "Stage1_V4_PrismArena_Boss";
constexpr const char* kDebugGoalEntryAnchorName = "Stage1_Collision_V4_GoalApproach";
constexpr const char* kDebugGoalName = "goal";
constexpr float kDebugTeleportPlayerFloorClearance = 0.78f;
constexpr float kDebugTeleportCameraDistance = 16.0f;
constexpr float kDebugTeleportCameraHeight = 3.2f;
constexpr float kDebugTeleportCameraPitch = 0.32f;

const char* GetDebugMorphDisplayName(Player::EnemyMorphType type) {
    switch (type) {
    case Player::EnemyMorphType::Slime:
        return "ピンクスライム";
    case Player::EnemyMorphType::Bomber:
        return "ボムスライム";
    case Player::EnemyMorphType::FireSlime:
        return "炎スライム";
    case Player::EnemyMorphType::ThunderSlime:
        return "雷スライム";
    case Player::EnemyMorphType::WindSlime:
        return "風スライム";
    case Player::EnemyMorphType::None:
        return "通常スライム";
    default:
        return "その他";
    }
}
#endif
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
        } else if (marker.signal == "goal.victory_land") {
            animation.victoryLandTime = marker.time;
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
    SetSignalTime("goal.jump", "Celebration Bounce", animation.jumpStartTime);
    SetSignalTime("goal.apex", "First Bounce Apex", animation.apexTime);
    SetSignalTime("goal.result", "Result UI", animation.resultUiTime);
    SetSignalTime("goal.victory_land", "First Bounce Landing", animation.victoryLandTime);
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
    bool hasVictoryLandVfx = false;
    for (auto& track : goalCinematicSequence_.vfxTracks) {
        if (track.sequenceName.rfind("crown_focus", 0) == 0) {
            track.startTime = std::max(0.05f, goalPresentationTuning_.crownFocusEndTime - 0.22f);
            hasCrownFocusVfx = true;
        } else if (track.sequenceName.rfind("crown_get", 0) == 0) {
            track.startTime = animation.crownLandTime;
            hasCrownVfx = true;
        } else if (track.sequenceName.rfind("crown_result", 0) == 0) {
            track.startTime = animation.apexTime;
            hasResultVfx = true;
        } else if (track.sequenceName.rfind("crown_victory_land", 0) == 0) {
            track.startTime = animation.victoryLandTime;
            hasVictoryLandVfx = true;
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
    if (!hasVictoryLandVfx) {
        CinematicVFXTrackData track;
        track.name = "Victory Landing VFX";
        track.sequenceName = "crown_victory_land_cue";
        track.startTime = animation.victoryLandTime;
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
	add(goalOverlayFlash_.get());
	add(goalOverlayReturnText_.get());
	for (const auto& letter : goalOverlayStageClearLetters_) {
		add(letter.get());
	}
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
			{ "wasStageCleared", goalWasStageCleared_ },
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
		goalWasStageCleared_ = goal.value("wasStageCleared", goalWasStageCleared_);
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
	const uint32_t splashHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/circle2.png");
	const uint32_t returnTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/ui/result/clear/returning_select_text.png");

	goalOverlayFlash_ = std::make_unique<Sprite>();
	goalOverlayFlash_->Initialize(spriteCommon_.get(), whiteHandle);
	goalOverlayFlash_->SetName("GoalClear_Flash");
	goalOverlayFlash_->SetAnchorPoint({ 0.5f, 0.5f });

	static constexpr std::array<const char*, 10> kLetterTexturePaths = {
		"Resources/sprite/ui/result/clear/slime_letters/letter_s.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_t.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_a.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_g.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_e.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_c.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_l.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_e.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_a.png",
		"Resources/sprite/ui/result/clear/slime_letters/letter_r.png"
	};
	for (size_t i = 0; i < goalOverlayStageClearLetters_.size(); ++i) {
		auto& letter = goalOverlayStageClearLetters_[i];
		letter = std::make_unique<Sprite>();
		letter->Initialize(spriteCommon_.get(), TextureManager::GetInstance()->Load(kLetterTexturePaths[i]));
		letter->SetName("GoalClear_Letter_" + std::to_string(i));
		letter->SetAnchorPoint({ 0.5f, 0.5f });
	}

	goalOverlayReturnText_ = std::make_unique<Sprite>();
	goalOverlayReturnText_->Initialize(spriteCommon_.get(), returnTextHandle);
	goalOverlayReturnText_->SetName("GoalClear_ReturnText");
	goalOverlayReturnText_->SetAnchorPoint({ 0.5f, 0.5f });

	for (size_t i = 0; i < goalOverlaySparkles_.size(); ++i) {
		auto& sparkle = goalOverlaySparkles_[i];
		sparkle = std::make_unique<Sprite>();
		sparkle->Initialize(spriteCommon_.get(), splashHandle);
		sparkle->SetName("GoalClear_LetterSplash_" + std::to_string(i));
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
	const bool replayClear = goalWasStageCleared_;
	const Vector3 themePrimary = replayClear
		? Vector3{ 0.90f, 0.92f, 0.96f }
		: Vector3{ 1.0f, 0.72f, 0.12f };
	const Vector3 themeSecondary = replayClear
		? Vector3{ 0.58f, 0.64f, 0.74f }
		: Vector3{ 1.0f, 0.34f, 0.06f };
	const float baseScale = (std::min)(screenW / 1280.0f, screenH / 720.0f);
	const float uiScale = baseScale * presentation.resultUiScale;
	const float centerX = screenW * presentation.resultUiCenterX;
	const float baselineY = screenH * presentation.resultUiCenterY;
	const float returnStartTime = std::max(animation.resultUiTime + 0.52f, animation.readyTime - 0.48f);
	const float returnIn = GoalEaseInOut((t - returnStartTime) / 0.34f);
	const float flashIn = GoalEaseOut((t - animation.resultUiTime) / 0.04f);
	const float flashOut = 1.0f - GoalEaseOut((t - animation.resultUiTime - 0.04f) / 0.20f);
	const float flash = visible ? flashIn * flashOut : 0.0f;
	if (goalOverlayFlash_) {
		goalOverlayFlash_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
		goalOverlayFlash_->SetSize({ screenW + 8.0f, screenH + 8.0f });
		goalOverlayFlash_->SetColor({ themePrimary.x, themePrimary.y, themePrimary.z, flash * 0.075f });
		goalOverlayFlash_->Update();
	}

	static constexpr std::array<float, 10> kAdvanceWidths = {
		62.0f, 60.0f, 66.0f, 68.0f, 56.0f, 68.0f, 54.0f, 56.0f, 66.0f, 64.0f
	};
	static constexpr std::array<float, 10> kSpriteWidths = {
		88.0f, 90.0f, 96.0f, 100.0f, 84.0f, 96.0f, 82.0f, 84.0f, 96.0f, 92.0f
	};
	static constexpr std::array<float, 10> kInitialTilts = {
		-0.10f, 0.075f, -0.055f, 0.095f, -0.070f, 0.060f, -0.085f, 0.050f, -0.060f, 0.080f
	};
	constexpr float kLetterGap = 6.0f;
	constexpr float kWordGap = 28.0f;
	float totalWidth = kWordGap + kLetterGap * 9.0f;
	for (float advance : kAdvanceWidths) {
		totalWidth += advance;
	}
	float cursorX = centerX - totalWidth * 0.5f * uiScale;

	for (size_t i = 0; i < goalOverlayStageClearLetters_.size(); ++i) {
		Sprite* letter = goalOverlayStageClearLetters_[i].get();
		Sprite* splash = goalOverlaySparkles_[i].get();
		const float letterCenterX = cursorX + kAdvanceWidths[i] * 0.5f * uiScale;
		const float localTime = t - animation.resultUiTime - static_cast<float>(i) * presentation.resultLetterStagger;
		float letterY = baselineY - presentation.resultLetterDropHeight * uiScale;
		float scaleX = 0.82f;
		float scaleY = 1.18f;
		float rotation = kInitialTilts[i];
		float letterAlpha = 0.0f;

		if (localTime >= 0.0f) {
			letterAlpha = GoalEaseOut(localTime / 0.10f);
			if (localTime < presentation.resultLetterFallDuration) {
				const float fallRate = GoalClamp01(localTime / presentation.resultLetterFallDuration);
				const float acceleratedFall = fallRate * fallRate;
				letterY = baselineY - presentation.resultLetterDropHeight * (1.0f - acceleratedFall) * uiScale;
				const float approach = GoalEaseInOut(fallRate);
				scaleX = 0.82f + 0.18f * approach;
				scaleY = 1.18f - 0.18f * approach;
				rotation = kInitialTilts[i] * (1.0f - approach);
			} else {
				const float bounceTime = localTime - presentation.resultLetterFallDuration;
				const float damping = std::exp(-bounceTime * presentation.resultLetterBounceDamping);
				const float bouncePhase = bounceTime * presentation.resultLetterBounceFrequency;
				const float rebound = std::abs(std::sin(bouncePhase)) * damping;
				const float contactWave = (std::max)(0.0f, std::cos(bouncePhase));
				const float contact = std::pow(contactWave, 7.0f) * damping;
				letterY = baselineY - presentation.resultLetterBounceHeight * rebound * uiScale;
				scaleX = 1.0f + contact * 0.34f - rebound * 0.075f;
				scaleY = 1.0f - contact * 0.26f + rebound * 0.11f;
				rotation = kInitialTilts[i] * std::cos(bouncePhase) * damping * 0.70f;

				const float idleBlend = GoalEaseInOut((bounceTime - 1.15f) / 0.45f);
				const float idleWobble = std::sin(bounceTime * 2.7f + static_cast<float>(i) * 0.62f);
				scaleX += idleWobble * 0.018f * idleBlend;
				scaleY -= idleWobble * 0.014f * idleBlend;
				letterY += std::sin(bounceTime * 2.2f + static_cast<float>(i) * 0.35f) * 1.4f * idleBlend * uiScale;
			}
		}

		if (letter) {
			letter->SetPosition({ letterCenterX, letterY });
			letter->SetSize({ kSpriteWidths[i] * scaleX * uiScale, 176.0f * scaleY * uiScale });
			letter->SetRotation(rotation);
			letter->SetColor({
				themePrimary.x,
				themePrimary.y,
				themePrimary.z,
				visible ? letterAlpha * presentation.resultBackdropAlpha : 0.0f
			});
			letter->Update();
		}

		if (splash) {
			const float impactAge = localTime - presentation.resultLetterFallDuration;
			const float splashRate = GoalClamp01(impactAge / 0.30f);
			const float splashAlpha = impactAge >= 0.0f
				? std::sin(splashRate * 3.1415926535f) * presentation.resultGlowAlpha
				: 0.0f;
			splash->SetPosition({ letterCenterX, baselineY + 45.0f * uiScale });
			splash->SetSize({
				(20.0f + 48.0f * splashRate) * uiScale,
				(13.0f - 9.0f * splashRate) * uiScale
			});
			splash->SetRotation(0.0f);
			splash->SetColor({
				themeSecondary.x,
				themeSecondary.y,
				themeSecondary.z,
				visible ? splashAlpha : 0.0f
			});
			splash->Update();
		}

		cursorX += (kAdvanceWidths[i] + kLetterGap) * uiScale;
		if (i == 4) {
			cursorX += kWordGap * uiScale;
		}
	}

	if (goalOverlayReturnText_) {
		goalOverlayReturnText_->SetPosition({ centerX, baselineY + (100.0f + 8.0f * (1.0f - returnIn)) * uiScale });
		goalOverlayReturnText_->SetSize({ 278.0f * uiScale, 33.0f * uiScale });
		goalOverlayReturnText_->SetRotation(0.0f);
		goalOverlayReturnText_->SetColor({ 0.92f, 0.98f, 1.0f, visible ? returnIn * 0.90f : 0.0f });
		goalOverlayReturnText_->Update();
	}
}

void GamePlayScene::DrawGoalPresentationOverlay() {
	if (!isGoal_ || goalPresentationState_ == GoalPresentationState::Inactive) {
		return;
	}

	if (goalOverlayFlash_) {
		goalOverlayFlash_->Draw();
	}
	for (auto& sparkle : goalOverlaySparkles_) {
		if (sparkle) {
			sparkle->Draw();
		}
	}
	for (auto& letter : goalOverlayStageClearLetters_) {
		if (letter) {
			letter->Draw();
		}
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

            ImGui::Separator();
            ImGui::Text("デバッグ移動");
            const bool debugRuntimePlaying =
                GetSceneManager() && GetSceneManager()->IsPlaying();
            const bool teleportRequestPending =
                pendingDebugTeleportDestination_ != DebugTeleportDestination::None;
            const bool teleportStateReady =
                debugRuntimePlaying &&
                !player_->isDead &&
                !isGoal_ &&
                !teleportRequestPending;
            const bool hasPrismAnchor = FindDebugObjectByName(kDebugPrismArenaAnchorName) != nullptr;
            const bool hasGoalAnchor = FindDebugObjectByName(kDebugGoalEntryAnchorName) != nullptr;
            const float teleportSpacing = ImGui::GetStyle().ItemSpacing.x;
            const float teleportButtonWidth =
                (std::max)(130.0f, (ImGui::GetContentRegionAvail().x - teleportSpacing) * 0.5f);

            if (!teleportStateReady || !hasPrismAnchor) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(ICON_FA_GEM " 中ボス前へ##DebugTeleport", ImVec2(teleportButtonWidth, 32.0f))) {
                QueueDebugTeleport(DebugTeleportDestination::PrismArena);
            }
            if (!teleportStateReady || !hasPrismAnchor) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            if (!teleportStateReady || !hasGoalAnchor) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(ICON_FA_FLAG_CHECKERED " ゴール前へ##DebugTeleport", ImVec2(teleportButtonWidth, 32.0f))) {
                QueueDebugTeleport(DebugTeleportDestination::GoalApproach);
            }
            if (!teleportStateReady || !hasGoalAnchor) {
                ImGui::EndDisabled();
            }
            if (stageEntryPresentationActive_) {
                ImGui::TextDisabled("開始演出中でも、演出を安全に終了してから移動します。");
            } else {
                ImGui::TextDisabled("ステージ1専用。移動時は攻撃・運搬・変身・ロックオン状態を解除します。");
            }

            ImGui::Separator();
            ImGui::Text("デバッグ変身（時間制限なし）");
            ImGui::TextDisabled("現在: %s", GetDebugMorphDisplayName(player_->GetEnemyMorphType()));

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float buttonWidth = (std::max)(90.0f, (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f);
            auto drawMorphButton = [this, buttonWidth](const char* label, const char* enemyType) {
                if (ImGui::Button(label, ImVec2(buttonWidth, 30.0f))) {
                    ApplyDebugPlayerMorph(enemyType);
                }
            };

            drawMorphButton("ピンク##DebugMorph", "Slime");
            ImGui::SameLine();
            drawMorphButton("ボム##DebugMorph", "Bomber");
            ImGui::SameLine();
            drawMorphButton("炎##DebugMorph", "FireSlime");

            drawMorphButton("雷##DebugMorph", "ThunderSlime");
            ImGui::SameLine();
            drawMorphButton("風##DebugMorph", "WindSlime");
            ImGui::SameLine();

            const bool hasMorph = player_->IsEnemyMorphed();
            if (!hasMorph) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("通常スライムへ戻す##DebugMorph", ImVec2(-1.0f, 30.0f))) {
                ClearDebugPlayerMorph();
            }
            if (!hasMorph) {
                ImGui::EndDisabled();
            }
            ImGui::TextDisabled("吸収演出を省略し、能力実装済みのスライムは固有技まで直接確認できます。");
        }
        else {
            ImGui::TextDisabled("Player not found");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_GEM " クリスタルスライム Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        EnemyPrismSlime* prismBoss = FindPrismBossForHUD();
        if (prismBoss) {
            const float bossHp = prismBoss->GetEncounterCurrentHp();
            const float bossMaxHp = prismBoss->GetEncounterMaximumHp();
            char bossHpLabel[64]{};
            std::snprintf(bossHpLabel, sizeof(bossHpLabel), "HP %.0f / %.0f", bossHp, bossMaxHp);
            ImGui::ProgressBar(
                std::clamp(bossHp / (std::max)(bossMaxHp, 1.0f), 0.0f, 1.0f),
                ImVec2(-1.0f, 0.0f),
                bossHpLabel);

            if (ImGui::Button(ICON_FA_SKULL " 撃破演出を確認##DebugDefeatPrismSlime", ImVec2(-1.0f, 32.0f))) {
                prismBoss->TriggerDebugDefeat();
            }
            ImGui::TextDisabled("実際のHPを0にして、撃破演出からバリア解除までを通して再生します。");
        } else {
            ImGui::TextDisabled("中ボス戦の開始後に使用できます。");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_CROWN " 偽王スライム Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        EnemyFalseKingSlime* falseKing = FindFalseKingBossForHUD();
        if (falseKing) {
            const float bossHp = falseKing->GetEncounterCurrentHp();
            const float bossMaxHp = falseKing->GetEncounterMaximumHp();
            char bossHpLabel[64]{};
            std::snprintf(bossHpLabel, sizeof(bossHpLabel), "HP %.0f / %.0f", bossHp, bossMaxHp);
            ImGui::ProgressBar(
                std::clamp(bossHp / (std::max)(bossMaxHp, 1.0f), 0.0f, 1.0f),
                ImVec2(-1.0f, 0.0f),
                bossHpLabel);
            if (ImGui::Button(ICON_FA_SKULL " 撃破から王冠取得を確認##DebugDefeatFalseKing", ImVec2(-1.0f, 32.0f))) {
                falseKing->TriggerDebugDefeat();
            }
            ImGui::TextDisabled("撃破演出後に報酬王冠が出現し、接触するとクリア演出へ移ります。");
        } else {
            ImGui::TextDisabled("ステージ3のボス登場後に使用できます。");
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

#ifdef USE_IMGUI
void GamePlayScene::ApplyDebugPlayerMorph(const char* enemyType) {
    if (!player_ || !object3dCommon_ || !enemyType || enemyType[0] == '\0') {
        return;
    }
    if (player_->isDead || player_->IsCinematicLocked()) {
        DebugConsole::GetInstance()->AddLog("Debug morph skipped: player is unavailable.");
        return;
    }

    // 先にプレイヤー側の参照を解除してから、以前のデバッグ用変身元を破棄します。
    ClearDebugPlayerMorph();

    std::unique_ptr<BaseEnemy> source = EnemyFactory::GetInstance()->CreateEnemy(enemyType, object3dCommon_.get());
    if (!source || source->GetEnemyType().empty()) {
        DebugConsole::GetInstance()->AddLog(std::string("Debug morph create failed: ") + enemyType);
        return;
    }

    source->SetTarget(player_);
    source->SetTranslate(player_->GetWorldPosition());
    source->SetIsVisible(false);
    source->SetCollisionAttribute(0);
    source->SetCollisionMask(0);

    BaseEnemy* sourcePointer = source.get();
    debugPlayerMorphSource_ = std::move(source);
    player_->DebugForceEnemyMorph(sourcePointer);

    if (!player_->IsEnemyMorphed()) {
        debugPlayerMorphSource_.reset();
        DebugConsole::GetInstance()->AddLog(std::string("Debug morph start failed: ") + enemyType);
        return;
    }

    DebugConsole::GetInstance()->AddLog(std::string("Debug morph selected: ") + enemyType);
}

void GamePlayScene::ClearDebugPlayerMorph() {
    if (player_) {
        player_->DebugClearEnemyMorph();
    }
    debugPlayerMorphSource_.reset();
}

void GamePlayScene::QueueDebugTeleport(DebugTeleportDestination destination) {
    if (destination == DebugTeleportDestination::None) {
        return;
    }

    pendingDebugTeleportDestination_ = destination;
    DebugConsole::GetInstance()->AddLog("Debug teleport queued for the next gameplay update.");
}

void GamePlayScene::ProcessPendingDebugTeleport() {
    const DebugTeleportDestination destination = pendingDebugTeleportDestination_;
    pendingDebugTeleportDestination_ = DebugTeleportDestination::None;
    if (destination == DebugTeleportDestination::None) {
        return;
    }

    if (!player_ || player_->isDead || isGoal_) {
        DebugConsole::GetInstance()->AddLog("Debug teleport skipped: player is unavailable.");
        return;
    }

    // Play開始直後に押されても、入口演出が次フレームで再開して座標を戻さないよう完了扱いにします。
    if (stageEntryPresentationActive_) {
        FinishStageEntryPresentation();
    }
    stageEntryPresentationPending_ = false;
    stageEntryPresentationCompleted_ = true;
    stageEntryRuntimeWasPlaying_ = true;
    stageEntryPresentationRetryTimer_ = 0.0f;

    switch (destination) {
    case DebugTeleportDestination::PrismArena:
        TeleportPlayerToDebugTarget(
            kDebugPrismArenaAnchorName,
            { 18.0f, 0.0f, 5.0f },
            kDebugPrismSlimeName,
            "中ボス前");
        break;
    case DebugTeleportDestination::GoalApproach:
        TeleportPlayerToDebugTarget(
            kDebugGoalEntryAnchorName,
            { 0.0f, 0.0f, 0.0f },
            kDebugGoalName,
            "ゴール前");
        break;
    case DebugTeleportDestination::None:
    default:
        break;
    }
}

Object3d* GamePlayScene::FindDebugObjectByName(const char* objectName) const {
    if (!objectManager_ || !objectName || objectName[0] == '\0') {
        return nullptr;
    }

    std::vector<Object3d*> objectsToVisit;
    objectsToVisit.reserve(objectManager_->GetObjects().size());
    for (const auto& object : objectManager_->GetObjects()) {
        if (object) {
            objectsToVisit.push_back(object.get());
        }
    }

    while (!objectsToVisit.empty()) {
        Object3d* object = objectsToVisit.back();
        objectsToVisit.pop_back();
        if (object->GetName() == objectName) {
            return object;
        }
        for (Object3d* child : object->GetChildren()) {
            if (child) {
                objectsToVisit.push_back(child);
            }
        }
    }
    return nullptr;
}

bool GamePlayScene::TeleportPlayerToDebugTarget(
    const char* anchorName,
    const Vector3& anchorOffset,
    const char* facingObjectName,
    const char* destinationLabel) {
    if (!player_ || player_->isDead || isGoal_) {
        DebugConsole::GetInstance()->AddLog("Debug teleport skipped: player is unavailable.");
        return false;
    }

    Object3d* anchor = FindDebugObjectByName(anchorName);
    if (!anchor) {
        DebugConsole::GetInstance()->AddLog(
            std::string("Debug teleport anchor not found: ") + (anchorName ? anchorName : "(null)"));
        return false;
    }

    // 親子階層を含む編集直後のTransformも確定してから、コライダー上面を着地点にします。
    Object3d* anchorRoot = anchor;
    while (anchorRoot->GetParent()) {
        anchorRoot = anchorRoot->GetParent();
    }
    anchorRoot->UpdateWorldMatrix(false);

    Vector3 destination = anchor->GetWorldPosition() + anchorOffset;
    const AABB anchorBounds = anchor->GetAABB();
    if (anchorBounds.max.y > anchorBounds.min.y) {
        destination.y = anchorBounds.max.y + kDebugTeleportPlayerFloorClearance;
    }

    Vector3 facingDirection = { 1.0f, 0.0f, 0.0f };
    if (Object3d* facingObject = FindDebugObjectByName(facingObjectName)) {
        facingDirection = facingObject->GetWorldPosition() - destination;
        facingDirection.y = 0.0f;
        const float facingLength = std::sqrt(
            facingDirection.x * facingDirection.x + facingDirection.z * facingDirection.z);
        if (facingLength > 0.001f) {
            facingDirection.x /= facingLength;
            facingDirection.z /= facingLength;
        } else {
            facingDirection = { 1.0f, 0.0f, 0.0f };
        }
    }

    // デバッグ移動では進行中の移動演出も明示的に止め、次フレームの座標上書きを防ぎます。
    if (arenaBossIntroActive_) {
        FinishArenaBossIntro();
    }
    // プレイヤー内部の各コントローラーを一括解除し、次フレームの座標上書きを防ぎます。
    player_->DebugPrepareForTeleport();
    debugPlayerMorphSource_.reset();
    player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
    player_->SetGrounded(false);
    player_->SetTranslate(destination);
    player_->SetRespawnPosition(destination);
    player_->SetMoveYaw(std::atan2(facingDirection.x, facingDirection.z));
    player_->SetIsVisible(true);
    player_->SetIsControlActive(true);
    player_->UpdateLocalMatrix();
    player_->UpdateWorldMatrix();

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (lockOnSystem_) {
        lockOnSystem_->ResetLockOn(camera);
    }
    player_->SetLockOn(false);
    if (camera) {
        CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);
        camera->EndOverride(0.0f);
        camera->ClearPresentationLayers();
        CameraManager::GetInstance()->SetActiveCamera(camera);
        camera->SetInputEnabled(true);
        camera->SetFollowTarget(player_);
        camera->SetFollowMode(Camera::FollowMode::kAimable);
        camera->SetRotation({
            kDebugTeleportCameraPitch,
            std::atan2(facingDirection.x, facingDirection.z),
            0.0f });
        camera->SnapToThirdPerson(
            kDebugTeleportCameraDistance,
            kDebugTeleportCameraHeight,
            kDebugTeleportCameraPitch);
        camera->Update(0.0f);
    }

    DebugConsole::GetInstance()->AddLog(
        std::string("Debug teleport completed: ") +
        (destinationLabel ? destinationLabel : "unknown"));
    return true;
}
#endif

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
            animation.victoryLandTime = j.value("victoryLandTime", animation.victoryLandTime);
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
            goalPresentationTuning_.resultLetterStagger = j.value("letterStagger", goalPresentationTuning_.resultLetterStagger);
            goalPresentationTuning_.resultLetterFallDuration = j.value("letterFallDuration", goalPresentationTuning_.resultLetterFallDuration);
            goalPresentationTuning_.resultLetterDropHeight = j.value("letterDropHeight", goalPresentationTuning_.resultLetterDropHeight);
            goalPresentationTuning_.resultLetterBounceHeight = j.value("letterBounceHeight", goalPresentationTuning_.resultLetterBounceHeight);
            goalPresentationTuning_.resultLetterBounceDamping = j.value("letterBounceDamping", goalPresentationTuning_.resultLetterBounceDamping);
            goalPresentationTuning_.resultLetterBounceFrequency = j.value("letterBounceFrequency", goalPresentationTuning_.resultLetterBounceFrequency);
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
        {"apexTime", a.apexTime}, {"resultUiTime", a.resultUiTime},
        {"victoryLandTime", a.victoryLandTime}, {"readyTime", a.readyTime}
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
        {"glowAlpha", g.resultGlowAlpha},
        {"letterStagger", g.resultLetterStagger},
        {"letterFallDuration", g.resultLetterFallDuration},
        {"letterDropHeight", g.resultLetterDropHeight},
        {"letterBounceHeight", g.resultLetterBounceHeight},
        {"letterBounceDamping", g.resultLetterBounceDamping},
        {"letterBounceFrequency", g.resultLetterBounceFrequency}
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
    a.victoryLandTime = std::max(a.victoryLandTime, a.apexTime + 0.36f);
    a.readyTime = std::max(a.readyTime, a.victoryLandTime + 0.55f);
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
    g.resultUiCenterY = std::clamp(g.resultUiCenterY, 0.12f, 0.82f);
    g.resultUiScale = std::clamp(g.resultUiScale, 0.60f, 1.35f);
    g.resultBackdropAlpha = std::clamp(g.resultBackdropAlpha, 0.0f, 1.0f);
    g.resultGlowAlpha = std::clamp(g.resultGlowAlpha, 0.0f, 1.0f);
    g.resultLetterStagger = std::clamp(g.resultLetterStagger, 0.02f, 0.20f);
    g.resultLetterFallDuration = std::clamp(g.resultLetterFallDuration, 0.18f, 0.75f);
    g.resultLetterDropHeight = std::clamp(g.resultLetterDropHeight, 80.0f, 520.0f);
    g.resultLetterBounceHeight = std::clamp(g.resultLetterBounceHeight, 8.0f, 120.0f);
    g.resultLetterBounceDamping = std::clamp(g.resultLetterBounceDamping, 1.0f, 8.0f);
    g.resultLetterBounceFrequency = std::clamp(g.resultLetterBounceFrequency, 4.0f, 18.0f);
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
    changed |= ImGui::DragFloat("ジャンプ頂点", &a.apexTime, 0.01f, 1.60f, 6.50f, "%.2f 秒");
    changed |= ImGui::DragFloat("リザルト表示", &a.resultUiTime, 0.01f, 1.70f, 7.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("勝利着地", &a.victoryLandTime, 0.01f, 2.00f, 7.50f, "%.2f 秒");
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
    changed |= ImGui::DragFloat("表示位置 Y", &g.resultUiCenterY, 0.005f, 0.12f, 0.82f, "%.3f");
    changed |= ImGui::DragFloat("UIスケール", &g.resultUiScale, 0.005f, 0.60f, 1.35f, "%.3f");
    changed |= ImGui::DragFloat("文字不透明度", &g.resultBackdropAlpha, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::DragFloat("着地しぶき", &g.resultGlowAlpha, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::DragFloat("文字間ディレイ", &g.resultLetterStagger, 0.001f, 0.02f, 0.20f, "%.3f 秒");
    changed |= ImGui::DragFloat("落下時間", &g.resultLetterFallDuration, 0.005f, 0.18f, 0.75f, "%.3f 秒");
    changed |= ImGui::DragFloat("落下距離", &g.resultLetterDropHeight, 1.0f, 80.0f, 520.0f, "%.0f px");
    changed |= ImGui::DragFloat("跳ね返り高さ", &g.resultLetterBounceHeight, 0.5f, 8.0f, 120.0f, "%.1f px");
    changed |= ImGui::DragFloat("跳ね減衰", &g.resultLetterBounceDamping, 0.05f, 1.0f, 8.0f, "%.2f");
    changed |= ImGui::DragFloat("跳ね速度", &g.resultLetterBounceFrequency, 0.05f, 4.0f, 18.0f, "%.2f");

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
    if (!obj || !obj->GetIsVisible()) return false;
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return true;

    // モデルデータに基づいた正確なワールド空間AABBを取得
    AABB worldAabb = obj->GetModelWorldAABB();

    // 近距離では視錐台境界の数値誤差より描画の安定性を優先する。
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
