#pragma once
#ifdef USE_IMGUI
#include "SceneManager.h"
#include "Object3DCommon.h"
#include "DirectXCommon.h"
#include "DebrisEffectManager.h"
#include "WinApp.h"
#include <fstream>
#include <stdexcept>
#include <string>

// 実Deviceで軽量→全体の準備と、ロード画面を挟む実Scene遷移での資源再利用を調べる。
// 通常起動では何も生成・変更しない。既存のシーン遷移検証から明示的に使う。
namespace SharedRenderCommonsRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_SHARED_COMMONS_TEST", nullptr, 0) != 0; }
inline bool CheckAfterDraw(SceneManager* manager, const std::string& path) {
    if (!Enabled()) return true;
    static std::ofstream log(path + ".commons", std::ios::trunc);
    static bool initialized = false;
    static bool finished = false;
    static engine::Object3DCommon* object = nullptr;
    static engine::SpriteCommon* sprite = nullptr;
    static engine::ParticleCommon* particle = nullptr;
    static ID3D12PipelineState* skybox = nullptr;
    static uint64_t generation = 0;
    static unsigned entered = 0;
    static bool sawLoading = false;
    if (finished) return false;
    auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    try {
        auto* resources = manager->GetRenderCommonResources();
        require(resources != nullptr, "engine resources missing");
        if (!initialized) {
            // Sceneで使う実体とは別の未描画資源で、Release向け軽量準備も実Device上で検証する。
            engine::RenderCommonResources local;
            local.Initialize(DirectXCommon::GetInstance());
            local.Prepare(engine::RenderPreparation::SpritesOnly);
            auto* localSprite = local.GetSpriteCommon();
            require(localSprite && !local.GetObject3DCommon(), "loading profile initialized unnecessary 3D resources");
            local.Prepare(engine::RenderPreparation::Minimal);
            auto* localObject = local.GetObject3DCommon();
            auto* minimalRoot = localObject->GetSkyboxRootSignature();
            auto* minimalPso = localObject->GetSkyboxPipelineState();
            auto* localParticle = local.GetParticleCommon();
            require(!local.IsReady(engine::RenderPreparation::Full), "minimal was treated as full");
            unsigned steps = 0;
            while (!local.PrepareStep(engine::RenderPreparation::Full)) require(++steps < 30, "full preparation did not finish");
            require(local.GetSpriteCommon() == localSprite && local.GetObject3DCommon() == localObject &&
                    local.GetParticleCommon() == localParticle, "common instance replaced during upgrade");
            require(localObject->GetSkyboxRootSignature() == minimalRoot &&
                    localObject->GetSkyboxPipelineState() == minimalPso, "ready GPU resources recreated during upgrade");
            require(local.PrepareStep(engine::RenderPreparation::Full) &&
                    local.PrepareStep(engine::RenderPreparation::Minimal), "repeated preparation did not finish immediately");
            log << "PASS sprites-only -> minimal -> full; retained instances and skybox GPU resources" << std::endl;
            engine::RenderCommonResources interrupted;
            interrupted.Initialize(DirectXCommon::GetInstance());
            interrupted.PrepareStep(engine::RenderPreparation::Full);
            interrupted.PrepareStep(engine::RenderPreparation::Full);
            interrupted.Prepare(engine::RenderPreparation::Minimal);
            steps = 0;
            while (!interrupted.PrepareStep(engine::RenderPreparation::Full)) require(++steps < 30, "interrupted full preparation lost its state");
            log << "PASS partial full -> minimal -> full" << std::endl;
            object = resources->GetObject3DCommon();
            sprite = resources->GetSpriteCommon();
            particle = resources->GetParticleCommon();
            require(object && sprite && particle, "initial scene resources missing");
            skybox = object->GetSkyboxPipelineState();
            initialized = true;
        }
        require(resources->GetObject3DCommon() == object && resources->GetSpriteCommon() == sprite &&
                resources->GetParticleCommon() == particle, "engine common instance changed during transition");
        require(object->GetSkyboxPipelineState() == skybox, "scene transition recreated a ready PSO");
        auto* scene = manager->GetCurrentScene();
        require(scene && scene->GetSpriteCommon() == sprite, "scene does not borrow engine sprite common");
        if (scene->GetRenderPreparation() == engine::RenderPreparation::SpritesOnly) {
            require(scene->GetObject3DCommon() == nullptr, "loading scene exposes 3D render objects");
            if (!sawLoading) log << "PASS loading scene shares sprite common" << std::endl;
            sawLoading = true;
        } else {
            require(scene->GetObject3DCommon() == object && scene->GetParticleCommon() == particle,
                    "scene does not borrow engine 3D/particle commons");
            if (!manager->IsTransitioning() && generation != manager->GetSceneGeneration()) {
                generation = manager->GetSceneGeneration();
                // 同じCommonを使い回しても、前のSceneの実行中状態が残らないことを確認する。
                auto* debris = DebrisEffectManager::GetInstance();
                require(debris->GetActivePieceCount() == 0, "debris from previous scene survived");
                DebrisEffectConfig marker;
                marker.spawnCount = 1;
                marker.lifetimeMin = marker.lifetimeMax = 120.0f;
                marker.gravity = 0.0f;
                marker.speedMin = marker.speedMax = 0.0f;
                marker.collideGround = false;
                debris->SpawnFromConfig(marker, {0.0f, -1000.0f, 0.0f});
                require(debris->GetActivePieceCount() > 0, "debris lifetime fixture did not spawn");
                log << "BORROW " << manager->GetCurrentSceneName() << " generation=" << generation << std::endl;
                if (++entered == 8) {
                    require(sawLoading, "loading transition was not observed");
                    log << "PASS all scene types and direct reload retain shared commons" << std::endl;
                }
            }
        }
        return true;
    } catch (const std::exception& exception) {
        log << "FAIL " << exception.what() << std::endl;
        finished = true;
        WinApp::CloseNow();
        return false;
    }
}
} // namespace SharedRenderCommonsRuntimeProbe
#endif
