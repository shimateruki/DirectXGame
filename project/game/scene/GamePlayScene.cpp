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
		if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || (obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 22)) continue;

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
		if (obj->GetMaterialType() >= 8 && obj->GetMaterialType() <= 22) {
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

void GamePlayScene::InitializeGoalPresentationOverlay() {
	if (!spriteCommon_) {
		return;
	}

	const uint32_t whiteHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
	const uint32_t crownHandle = TextureManager::GetInstance()->Load("Resources/sprite/ui/title/crown_progress_icon.png");
	const uint32_t clearTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/ui/result/clear/stage_clear_text.png");
	const uint32_t returnTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/ui/result/clear/returning_select_text.png");

	goalOverlayBackdrop_ = std::make_unique<Sprite>();
	goalOverlayBackdrop_->Initialize(spriteCommon_.get(), whiteHandle);
	goalOverlayBackdrop_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayFlash_ = std::make_unique<Sprite>();
	goalOverlayFlash_->Initialize(spriteCommon_.get(), whiteHandle);
	goalOverlayFlash_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayPanel_ = std::make_unique<Sprite>();
	goalOverlayPanel_->Initialize(spriteCommon_.get(), whiteHandle);
	goalOverlayPanel_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayCrown_ = std::make_unique<Sprite>();
	goalOverlayCrown_->Initialize(spriteCommon_.get(), crownHandle);
	goalOverlayCrown_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayStageClearText_ = std::make_unique<Sprite>();
	goalOverlayStageClearText_->Initialize(spriteCommon_.get(), clearTextHandle);
	goalOverlayStageClearText_->SetAnchorPoint({ 0.5f, 0.5f });

	goalOverlayReturnText_ = std::make_unique<Sprite>();
	goalOverlayReturnText_->Initialize(spriteCommon_.get(), returnTextHandle);
	goalOverlayReturnText_->SetAnchorPoint({ 0.5f, 0.5f });

	UpdateGoalPresentationOverlay();
}

void GamePlayScene::UpdateGoalPresentationOverlay() {
	const bool visible = isGoal_ && goalPresentationState_ != GoalPresentationState::Inactive;
	const float screenW = static_cast<float>(WinApp::kClientWidth);
	const float screenH = static_cast<float>(WinApp::kClientHeight);
	const float t = goalPresentationTimer_;
	const GoalClearPlayerAnimator::Tuning& animation = goalClearPlayerAnimator_.GetTuning();
	const float resultIn = GoalEaseOut((t - animation.resultUiTime) / 0.36f);
	const float returnStartTime = std::max(animation.resultUiTime + 0.34f, animation.readyTime - 0.32f);
	const float returnIn = GoalEaseInOut((t - returnStartTime) / 0.30f);
	const float flashIn = GoalEaseOut((t - animation.crownLandTime) / 0.05f);
	const float flashOut = 1.0f - GoalEaseOut((t - animation.crownLandTime - 0.07f) / 0.24f);
	const float flash = visible ? flashIn * flashOut : 0.0f;
	const float panelX = screenW * 0.70f;
	const float panelY = screenH * 0.50f;
	const float pulse = 1.0f + std::sin(t * 5.8f) * 0.030f * resultIn;

	if (goalOverlayBackdrop_) {
		goalOverlayBackdrop_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
		goalOverlayBackdrop_->SetSize({ screenW + 8.0f, screenH + 8.0f });
		goalOverlayBackdrop_->SetColor({ 0.0f, 0.0f, 0.0f, visible ? 0.24f * resultIn : 0.0f });
		goalOverlayBackdrop_->Update();
	}
	if (goalOverlayFlash_) {
		goalOverlayFlash_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
		goalOverlayFlash_->SetSize({ screenW + 8.0f, screenH + 8.0f });
		goalOverlayFlash_->SetColor({ 1.0f, 0.76f, 0.28f, flash * 0.24f });
		goalOverlayFlash_->Update();
	}
	if (goalOverlayPanel_) {
		const float panelScale = 0.92f + 0.08f * GoalEaseOut((t - animation.resultUiTime) / 0.36f);
		goalOverlayPanel_->SetPosition({ panelX + 26.0f * (1.0f - resultIn), panelY });
		goalOverlayPanel_->SetSize({ 540.0f * panelScale, 318.0f * panelScale });
		goalOverlayPanel_->SetRotation(-0.035f);
		goalOverlayPanel_->SetColor({ 0.02f, 0.025f, 0.035f, visible ? 0.58f * resultIn : 0.0f });
		goalOverlayPanel_->Update();
	}
	if (goalOverlayCrown_) {
		goalOverlayCrown_->SetPosition({ panelX, panelY - 118.0f - 18.0f * (1.0f - resultIn) });
		goalOverlayCrown_->SetSize({ 94.0f * pulse, 94.0f * pulse });
		goalOverlayCrown_->SetRotation(std::sin(t * 4.4f) * 0.07f * resultIn);
		goalOverlayCrown_->SetColor({ 1.0f, 0.96f, 0.72f, visible ? resultIn : 0.0f });
		goalOverlayCrown_->Update();
	}
	if (goalOverlayStageClearText_) {
		goalOverlayStageClearText_->SetPosition({ panelX, panelY - 14.0f + 18.0f * (1.0f - resultIn) });
		goalOverlayStageClearText_->SetSize({ 620.0f * pulse, 118.0f * pulse });
		goalOverlayStageClearText_->SetRotation(-0.025f);
		goalOverlayStageClearText_->SetColor({ 1.0f, 1.0f, 1.0f, visible ? resultIn : 0.0f });
		goalOverlayStageClearText_->Update();
	}
	if (goalOverlayReturnText_) {
		goalOverlayReturnText_->SetPosition({ panelX, panelY + 112.0f + 14.0f * (1.0f - returnIn) });
		goalOverlayReturnText_->SetSize({ 398.0f, 48.0f });
		goalOverlayReturnText_->SetRotation(-0.015f);
		goalOverlayReturnText_->SetColor({ 1.0f, 1.0f, 1.0f, visible ? returnIn * 0.88f : 0.0f });
		goalOverlayReturnText_->Update();
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
	if (goalOverlayPanel_) {
		goalOverlayPanel_->Draw();
	}
	if (goalOverlayCrown_) {
		goalOverlayCrown_->Draw();
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
        {"crownFocusEndTime", g.crownFocusEndTime}, {"crownLandTime", a.crownLandTime},
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

    std::ofstream file(kGoalPresentationTuningPath, std::ios::binary);
    if (file.is_open()) {
        file << root.dump(4);
    }
}

void GamePlayScene::SanitizeGoalPresentationTuning() {
    GoalClearPlayerAnimator::Tuning a = goalClearPlayerAnimator_.GetTuning();
    GoalPresentationTuning& g = goalPresentationTuning_;
    g.crownFocusEndTime = std::clamp(g.crownFocusEndTime, 0.08f, 0.70f);
    a.crownLandTime = std::max(a.crownLandTime, g.crownFocusEndTime + 0.30f);
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
    changed |= ImGui::DragFloat("王冠フォーカス終了", &g.crownFocusEndTime, 0.01f, 0.08f, 0.70f, "%.2f 秒");
    changed |= ImGui::DragFloat("王冠着地", &a.crownLandTime, 0.01f, 0.40f, 2.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("溜め開始", &a.anticipationStartTime, 0.01f, 0.45f, 2.30f, "%.2f 秒");
    changed |= ImGui::DragFloat("大ジャンプ開始", &a.jumpStartTime, 0.01f, 0.55f, 2.60f, "%.2f 秒");
    changed |= ImGui::DragFloat("頂点・静止", &a.apexTime, 0.01f, 0.90f, 3.50f, "%.2f 秒");
    changed |= ImGui::DragFloat("リザルト表示", &a.resultUiTime, 0.01f, 1.00f, 4.00f, "%.2f 秒");
    changed |= ImGui::DragFloat("入力待ち開始", &a.readyTime, 0.01f, 1.50f, 6.00f, "%.2f 秒");

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

    if (changed) {
        SanitizeGoalPresentationTuning();
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
        SaveGoalPresentationTuning();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " JSON再読込")) {
        LoadGoalPresentationTuning();
    }
    ImGui::TextDisabled("保存先: %s", kGoalPresentationTuningPath);
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
