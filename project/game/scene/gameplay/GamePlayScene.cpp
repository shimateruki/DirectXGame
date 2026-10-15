#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "engine/system/scene/SceneEngineIncludes.h"
#include "SceneController.h"
#include "game/system/GameSceneDrawing.h"
#include "ParticleCommon.h"
#include "Skybox.h"
#include "Text.h"
#include "game/ui/ControlsGuideOverlay.h"
#include "game/ui/PauseMenuOverlay.h"
#include "game/ui/SettingsMenuOverlay.h"
#include "game/ui/SaveIndicatorOverlay.h"
#include "imgui.h"
#include <EventManager.h>
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
GamePlayScene::GamePlayScene() {
    // シーン固有リソースの生成はInitializeへまとめる。
}
GamePlayScene::~GamePlayScene() {
    // シーン固有リソースの終了はFinalizeへまとめる。
}

void GamePlayScene::Draw() {
    // 近距離では自キャラのモデルが視界を塞ぐため、通常パス・半透明パス・特殊材質パスで
    // 同じ「プレイヤーとその子を描かない」判定を使います。王冠演出中は専用カメラなので除外します。
    bool isFirstPerson = false;
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
#ifndef _DEBUG
    if (camera->GetFollowTarget() && camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
        isFirstPerson = true;
    }
#endif

    // FollowMode以外でも壁際などでカメラが胸元へ寄るため、距離で同じ扱いへ切り替えます。
    if (!crownFinale_.active && !crownDefeat_.active && !crownDefeat_.completed && !isFirstPerson && player_ && camera) {
        engine::Vector3 pPos = player_->GetWorldPosition();
        pPos.y += 1.0f; // プレイヤーの胸の高さを基準にする
        engine::Vector3 cPos = camera->GetEye();
        engine::Vector3 toCam = {cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z};
        float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);

        // 距離が 3.0m 未満なら、プレイヤーを完全に消す判定フラグを立てる
        if (dist < 3.0f) {
            isFirstPerson = true;
        }
    }

    ID3D12Resource* pointLightRes = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes = LightManager::GetInstance()->GetSpotLightResource();

    if (lifeLostState_.IsActive()) {
        DrawLifeLostPresentationWorld(pointLightRes, spotLightRes);
        return;
    }

    if (lifeLostState_.HoldsBlack()) {
        return;
    }

    auto& objects = objectManager_->GetObjects();

    // 背景を先に描き、以降は不透明→半透明→画面色・深度を読む特殊材質の順で重ねます。
    if (skybox_ && camera && LightManager::GetInstance()->IsSkyboxEnabled()) {
        skybox_->SetTextureHandle(LightManager::GetInstance()->GetSkyboxTextureHandle());
        skybox_->Draw(camera->GetConstantBuffer());
    }

    // 不透明パスでは、別のパイプラインを必要とする霧・流体・半透明を除きます。
    object3dCommon_->SetGraphicsCommand();
    object3dCommon_->SetPipelineState(BlendMode::kNone); // 不透明設定

    DrawSceneObjectPass(objects, MaterialRenderPolicy::Pass::Opaque, pointLightRes, spotLightRes,
        player_, isFirstPerson, true, [this](engine::Object3d* object) { return IsVisible(object); });

    // フックマーカーの描画（プレイヤーがカメラ外に判定されて非表示になってもマーカーだけは描画する）
    DrawCrownFinaleWorld();
    DrawCrownDefeatWorld();
    if (player_ && player_->GetHookMarker()) {
        player_->GetHookMarker()->Draw(pointLightRes, spotLightRes);
    }

    if (animatedCube_) {
        animatedCube_->Draw(pointLightRes, spotLightRes);
    }

    // 弾と攻撃予兆は背景・地形の後、半透明の粒子より前のパスで描きます。
    DrawEnemyTelegraphs(*this, pointLightRes, spotLightRes);
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    LightEditor::GetInstance()->Draw3D();

    // 半透明モデルは不透明物の深度が決まった後に描画します。
    DrawSceneObjectPass(objects, MaterialRenderPolicy::Pass::Transparent, pointLightRes, spotLightRes,
        player_, isFirstPerson, true, {});
    particleSystem_->Draw();

    // ローカルフォグは通常モデルと分け、深度を参照する描画区間へつなぎます。
    DrawLocalFogObjects(objects, dxCommon_, player_, isFirstPerson);

    // 画面色のコピーは高コストなので、流体または歪み粒子が必要なフレームだけ行います。
    // hasFluidにはObjectが所有する追加の特殊材質と弾の特殊描画も含めます。
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
        // 水・炎・GPU粒子は深度をSRVとして読むため、この区間ではDSVを外します。
        // PostDrawLocalFogで描画先を戻すまでは、通常の3D描画を挟めません。
        dxCommon_->PreDrawLocalFog();

        if (hasFluid) {
            for (auto& obj : objects) {
                bool isPlayerPart = false;
                if (isFirstPerson) {
                    engine::Object3d* current = obj.get();
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

                // 描画順と画面色・深度の準備はシーンで管理し、材質ごとの選択は描画側に委ねる。
                obj->DrawSpecialMaterial(dxCommon_->GetDepthSrvHandle(), dxCommon_->GetGrabSrvHandle());
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

    // アタッチ済みMeshEffectの歪みはGrabTextureの更新後に描きます。
    // カメラプレビュー中は本編画面のコピーを触らないため、このパスを飛ばします。
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
    // ワールド描画の後に2Dを重ねます。早期returnする演出は通常HUDと同時表示しません。
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
    if (lifeLostState_.IsActive() || lifeLostState_.HoldsBlack()) {
        DrawGameplayHUD();
        return;
    }

    // クリア演出中は通常HUDを隠し、カメラとリザルトへ視線を集中させます。
    if (isGoal_) {
        DrawGoalPresentationOverlay();
        return;
    }
    // 黒帯を使うムービーでは通常HUD、照準、配置Sprite、案内をまとめて隠します。
    // 演出状態が解除された次のフレームから通常描画へ戻ります。
    if (stageEntryPresentationActive_ || arenaBossIntroActive_ || arenaBossRewardActive_ ||
        sealBarrageMovie_.active || crownFinale_.active || crownDefeat_.active) {
        return;
    }
    // JSON配置のSpriteとプレイヤーUIを描いた後、照準・HUD・モーダルの順に重ねます。
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
    // 設定画面はポーズより手前に置き、保存通知はモーダルの後段へ描画します。
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
    // 配置オブジェクトを影パスへ渡し、通常描画と同じ管理集合を使う。
    if (objectManager_) {
        objectManager_->DrawShadow();
    }
}

void GamePlayScene::StartBridgeDropMovie() {
    // 互換用の呼び出し口。現時点ではこの経路で演出を開始しない。
}
bool GamePlayScene::IsVisible(engine::Object3d* obj) {
    if (!obj || !obj->GetIsVisible()) {
        return false;
    }
    // 所有する槍・封印と王冠Morph中のプレイヤーは本体のAABBだけで除外しません。
    if (dynamic_cast<EnemyFalseKingSlime*>(obj) ||
        (obj == player_ && player_->GetEnemyMorphType() == Player::EnemyMorphType::Crown)) {
        return true;
    }
    return IsSceneObjectVisibleByFrustum(obj, CameraManager::GetInstance()->GetActiveCamera());
}
