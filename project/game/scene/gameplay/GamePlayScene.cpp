#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
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
#include "MeshEffectManager.h"
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
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include <BaseEnemy.h>
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include <game/actor/enemy/spawn/EnemyFactory.h>
#include <game/actor/enemy/spawn/EnemySpawner.h>
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
#include "game/actor/player/state/PlayerState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// シーンの寿命は本体が管理し、演出ファイルは既存メンバーを借用する。
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
    if (!crownFinale_.active && !crownDefeat_.active && !crownDefeat_.completed && !isFirstPerson && player_ && camera) {
        Vector3 pPos = player_->GetWorldPosition();
        pPos.y += 1.0f; // プレイヤーの胸の高さを基準にする
        Vector3 cPos = camera->GetEye();
        Vector3 toCam = {cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z};
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
        if (!IsVisible(obj.get()))
            continue;

        // プレイヤー本体および「子パーツ（緑のブロック等）」かどうかの判定
        bool isPlayerPart = false;
        if (isFirstPerson) {
            Object3d* current = obj.get();
            while (current) {
                if (current == player_) {
                    isPlayerPart = true;
                    break;
                }
                current = current->GetParent();
            }
        }

        // プレイヤーの一部なら描画をスキップ
        if (isPlayerPart) {
            continue;
        }

        // 半透明マテリアル(1)、ローカルフォグ(7)、特殊描画マテリアル(8〜22)はここでは描画しない
        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || IsSpecialMaterialType(obj->GetMaterialType()))
            continue;

        obj->Draw(pointLightRes, spotLightRes);
    }

    // フックマーカーの描画（プレイヤーがカメラ外に判定されて非表示になってもマーカーだけは描画する）
    DrawCrownFinaleWorld();
    DrawCrownDefeatWorld();
    if (player_ && player_->GetHookMarker()) {
        player_->GetHookMarker()->Draw(pointLightRes, spotLightRes);
    }

    if (animatedCube_) {
        animatedCube_->Draw(pointLightRes, spotLightRes);
    }

    // =======================================================
    // 3. 中間描画 (弾・デバッグUIなど)
    // =======================================================
    DrawEnemyTelegraphs(pointLightRes, spotLightRes);
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
                if (current == player_) {
                    isPlayerPart = true;
                    break;
                }
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
                        if (current == player_) {
                            isPlayerPart = true;
                            break;
                        }
                        current = current->GetParent();
                    }
                }
                if (isPlayerPart)
                    continue; // 流体の場合は分身になることはないので単純スキップ

                int matType = obj->GetMaterialType();
                if (matType == 8) {
                    obj->DrawWater(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 9) {
                    obj->DrawMagma(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 10) {
                    obj->DrawIce(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 11) {
                    obj->DrawFire(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 12) {
                    obj->DrawLaser(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 13) {
                    obj->DrawSlimeGel(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 14) {
                    obj->DrawShockwave(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 15) {
                    obj->DrawLiquidContact(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 16) {
                    obj->DrawDamageCrack(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 17) {
                    obj->DrawUpdraft(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 18) {
                    obj->DrawStunBind(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 19) {
                    obj->DrawCrownUnlock(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 20) {
                    obj->DrawPoisonSpore(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 21) {
                    obj->DrawCloud(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 22) {
                    obj->DrawGatePortal(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                } else if (matType == 26) {
                    obj->DrawWindOrb(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
                }
                obj->DrawOwnedSpecialMaterialVisuals(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
            }
            BulletManager::GetInstance()->DrawSpecial(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
        }

        if (hasGPUParticles) {
            gpuParticleManager->Draw(dxCommon_->GetCommandList(), camera->GetViewMatrix(),
                                     camera->GetProjectionMatrix(), gpuParticleTexHandle_,
                                     dxCommon_->GetDepthSrvHandle());
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
                if (!obj->GetIsVisible())
                    continue;
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
    if (crownDefeat_.active) return;
    if (arenaBossIntroActive_ || crownFinale_.active) {
        DrawCrownFinaleUI();
        return;
    }
    for (auto& sprite : sprites_) {
        if (sprite && !IsGameplayHUDSprite(sprite.get())) {
            sprite->Draw();
        }
    }
    if (player_) {
        player_->DrawUI();
    }
    // プレイヤーの照準UIに埋もれないよう、選択対象のマーカーを後から描きます。
    if (isDrawLockOn_ && lockOnSprite_) {
        lockOnSprite_->Draw();
    }
    DrawGameplayHUD();
    DrawCrownFinaleUI();
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

void GamePlayScene::DrawShadow() {
    if (objectManager_) {

        objectManager_->DrawShadow();
    }
}

void GamePlayScene::StartBridgeDropMovie() {}
bool GamePlayScene::IsVisible(Object3d* obj) {
    if (!obj || !obj->GetIsVisible())
        return false;
    // 所有する槍・封印は本体のAABBの外にも存在します。
    if (dynamic_cast<EnemyFalseKingSlime*>(obj) ||
        (obj == player_ && player_->GetEnemyMorphType() == Player::EnemyMorphType::Crown))
        return true;
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera)
        return true;

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
