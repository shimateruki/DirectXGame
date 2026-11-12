#include "SceneRenderCommands.h"
#include "DirectXCommon.h"
#include "SpriteCommon.h"
#include "Camera.h"
#include "GpuParticleManager.h"

namespace engine {
SceneRenderCommands::SceneRenderCommands(DirectXCommon& dxCommon) : dxCommon_(dxCommon) {}

void SceneRenderCommands::BindSprites(SpriteCommon& sprites) {
    sprites.SetPipeline(dxCommon_.GetCommandList());
}

bool SceneRenderCommands::IsCameraPreviewRendering() const {
    return dxCommon_.IsCameraPreviewRendering();
}

void SceneRenderCommands::CopySceneColor() {
    dxCommon_.UpdateGrabTexture();
}

void SceneRenderCommands::BeginDepthRead() {
    dxCommon_.PreDrawLocalFog();
}

void SceneRenderCommands::EndDepthRead() {
    dxCommon_.PostDrawLocalFog();
}

uint32_t SceneRenderCommands::GetDepthSrvHandle() const {
    return dxCommon_.GetDepthSrvHandle();
}

uint32_t SceneRenderCommands::GetSceneColorSrvHandle() const {
    return dxCommon_.GetGrabSrvHandle();
}

void SceneRenderCommands::RecordGpuParticles(GpuParticleManager& particles, const Camera& camera, uint32_t textureHandle) {
    particles.Draw(dxCommon_.GetCommandList(), camera.GetViewMatrix(), camera.GetProjectionMatrix(),
        textureHandle, dxCommon_.GetDepthSrvHandle());
}
} // namespace engine
