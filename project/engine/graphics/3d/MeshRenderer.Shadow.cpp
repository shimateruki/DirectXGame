#include "MeshRenderer.h"
#include "FoliageInteraction.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "LightManager.h"
#include "TextureManager.h"
#include "DebugConsole.h"
#include <cassert>
#include <SrvManager.h>

#include <algorithm>
#include <cmath>


// ========================================================================
// MeshRenderer 影とローカルフォグ描画
// ------------------------------------------------------------------------
// シャドウマップ用描画と、深度を使ったローカルフォグ描画を担当する。
// 通常描画や特殊マテリアルとはパスが違うため、専用ファイルとして分ける。
// ========================================================================
void MeshRenderer::DrawShadow() {
    DrawShadowOnly();
}

void MeshRenderer::SetShadowCommonState() {
    if (!common_)
        return;
    common_->SetShadowGraphicsCommand();
    common_->SetShadowPipelineState();
}

void MeshRenderer::DrawShadowOnly() {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !shadowWvpResource_ || !materialResource_ || !materialData_)
        return;
    drawModel->PrepareComputeSkinning();

    // Compute実行でルートシグネチャとPSOが変わるため、影描画の状態を復元します。
    common_->SetShadowGraphicsCommand();
    const bool foliage = materialData_->materialType == 30 || materialData_->materialType == 31;
    common_->SetShadowPipelineState(foliage);
    auto* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRoot32BitConstants(3, 32, FoliageInteraction::samples.data(), 0);
    drawModel->DrawShadow(shadowWvpResource_.Get(), meshDrawIndex_);
}

void MeshRenderer::DrawLocalFog(uint32_t depthSrvHandle) {
    Model* drawModel = ResolveDrawModel();
    if (!drawModel || !common_ || !HasRequiredBuffers() || !localFogResource_)
        return;

    common_->SetLocalFogGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();


    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, depthSrvHandle);


    commandList->SetGraphicsRootConstantBufferView(3, localFogResource_->GetGPUVirtualAddress());

    // [0] にWVP、[1] にボーンが自動セットされる
    drawModel->DrawShadow(wvpResource_.Get(), meshDrawIndex_);
}
