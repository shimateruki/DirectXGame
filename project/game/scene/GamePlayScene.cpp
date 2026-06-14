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

GamePlayScene::GamePlayScene() {}
GamePlayScene::~GamePlayScene() {}

void GamePlayScene::Draw() {
	// --- 一人称視点（カメラのめり込み）判定 ---
	bool isFirstPerson = false;
	Camera* camera = CameraManager::GetInstance()->GetMainCamera();
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
	if (skybox_ && camera) {
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
	bool hasFog = false;
	for (auto& obj : objects) {
		if (obj->GetMaterialType() == 7) {
			hasFog = true;
			break;
		}
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

	bool hasGPUParticles = !GPUParticleManager::GetInstance()->IsEmpty();
	bool grabUpdated = false;
	if (hasFluid || hasGPUParticles) {
		// 画面をキャプチャして背景テクスチャにする (必要な時だけ1回呼ぶ)
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
		if (hasMeshEffects) {
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
    
    ImGui::Separator();
    ImGui::TextDisabled("※この項目は GamePlayScene::DrawImGui() で編集可能です");
#endif
}

void GamePlayScene::StartBridgeDropMovie() {

}
bool GamePlayScene::IsVisible(Object3d* obj) {
    if (!obj) return false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return true;

    // モデルデータに基づいた正確なワールド空間AABBを取得
    AABB worldAabb = obj->GetModelWorldAABB();

    return Math::IntersectFrustumAABB(camera->GetFrustum(), worldAabb.min, worldAabb.max);
}
