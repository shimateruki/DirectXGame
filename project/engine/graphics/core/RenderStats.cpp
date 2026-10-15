#include "RenderStats.h"

#include <algorithm>

namespace engine {

RenderStats* RenderStats::GetInstance() {
    static RenderStats instance;
    return &instance;
}

void RenderStats::BeginFrame() {
    // 完了済みの集計とは別に、次のフレームを空の集計領域から始める。
    currentFrame_ = {};
    currentFrame_.frameNumber = nextFrameNumber_++;
    currentPass_ = RenderPass::Other;
    frameActive_ = true;
}

void RenderStats::EndFrame() {
    if (!frameActive_) {
        return;
    }

    // Editor が描画中の半端な値を読まないよう、完了フレームを別領域に保持します。
    completedFrame_ = currentFrame_;
    currentPass_ = RenderPass::Other;
    frameActive_ = false;
}

void RenderStats::SetCurrentPass(RenderPass pass) {
    currentPass_ = pass < RenderPass::Count ? pass : RenderPass::Other;
}

void RenderStats::RecordIndexedDraw(uint32_t indexCount, uint32_t instanceCount, uint32_t trianglesPerInstance) {
    // 描画呼び出しと推定三角形数を、現在の計測区間へ加算する。
    RenderPassStats* stats = GetCurrentPassStats();
    if (!stats || indexCount == 0 || instanceCount == 0) {
        return;
    }

    const uint64_t instances = instanceCount;
    const uint64_t triangles = trianglesPerInstance != 0 ? trianglesPerInstance : indexCount / 3u;
    ++stats->drawCalls;
    ++stats->indexedDrawCalls;
    stats->submittedIndices += static_cast<uint64_t>(indexCount) * instances;
    stats->submittedTriangles += triangles * instances;
    stats->submittedInstances += instances;
}

void RenderStats::RecordNonIndexedDraw(uint32_t vertexCount, uint32_t instanceCount, uint32_t trianglesPerInstance) {
    // インデックスを使わない描画も同じ計測区間へ加算する。
    RenderPassStats* stats = GetCurrentPassStats();
    if (!stats || vertexCount == 0 || instanceCount == 0) {
        return;
    }

    const uint64_t instances = instanceCount;
    const uint64_t triangles = trianglesPerInstance != 0 ? trianglesPerInstance : vertexCount / 3u;
    ++stats->drawCalls;
    stats->submittedVertices += static_cast<uint64_t>(vertexCount) * instances;
    stats->submittedTriangles += triangles * instances;
    stats->submittedInstances += instances;
}

void RenderStats::RecordNonIndexedIndirectDraw() {
    // 間接描画は頂点数をここで取得できないため、呼び出し回数だけ記録する。
    if (RenderPassStats* stats = GetCurrentPassStats()) {
        ++stats->drawCalls;
        ++stats->indirectDrawCalls;
    }
}

void RenderStats::RecordComputeDispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    // 空のディスパッチは実際のGPU仕事量に含めない。
    RenderPassStats* stats = GetCurrentPassStats();
    if (!stats || groupCountX == 0 || groupCountY == 0 || groupCountZ == 0) {
        return;
    }

    ++stats->computeDispatches;
    stats->computeThreadGroups +=
        static_cast<uint64_t>(groupCountX) * groupCountY * groupCountZ;
}

void RenderStats::RecordObjectDraw() {
    if (RenderPassStats* stats = GetCurrentPassStats()) {
        ++stats->submittedObjects;
    }
}

void RenderStats::RecordCulledObject() {
    if (RenderPassStats* stats = GetCurrentPassStats()) {
        ++stats->culledObjects;
    }
}

void RenderStats::RecordGpuParticleSystem(uint32_t capacity) {
    // フレーム外の登録は前後の計測結果へ混ぜない。
    if (!frameActive_) {
        return;
    }
    ++currentFrame_.gpuParticleSystems;
    currentFrame_.gpuParticleCapacity += capacity;
}

void RenderStats::RecordCpuParticles(uint32_t activeCount) {
    if (frameActive_) {
        currentFrame_.cpuParticleCount += activeCount;
    }
}

void RenderStats::RecordPostProcessPass() {
    if (frameActive_ && currentPass_ == RenderPass::PostProcess) {
        ++currentFrame_.postProcessPasses;
    }
}

void RenderStats::SetActiveLightCounts(uint32_t pointLights, uint32_t spotLights) {
    // 描画中に適用したライト数だけを当該フレームの値として残す。
    if (!frameActive_) {
        return;
    }
    currentFrame_.activePointLights = pointLights;
    currentFrame_.activeSpotLights = spotLights;
}

const char* RenderStats::GetPassDisplayName(RenderPass pass) {
    // 計測した描画区間の種別をデバッグ画面へ表示する名称へ変換する。
    switch (pass) {
    case RenderPass::MainScene: return "メイン3D";
    case RenderPass::Shadow: return "影";
    case RenderPass::CameraPreview: return "カメラPreview";
    case RenderPass::GameUI: return "ゲームUI";
    case RenderPass::PostProcess: return "後処理";
    case RenderPass::EditorOverlay: return "Editor補助";
    case RenderPass::Other: return "その他";
    default: return "不明";
    }
}

RenderPassStats* RenderStats::GetCurrentPassStats() {
    // フレーム外で呼ばれた計測 API は無視し、次フレームへの混入を防ぎます。
    if (!frameActive_) {
        return nullptr;
    }

    const size_t passIndex = static_cast<size_t>(currentPass_);
    if (passIndex >= currentFrame_.passes.size()) {
        return nullptr;
    }
    return &currentFrame_.passes[passIndex];
}

ScopedRenderPass::ScopedRenderPass(RenderPass pass) {
    RenderStats* stats = RenderStats::GetInstance();
    previousPass_ = stats->GetCurrentPass();
    stats->SetCurrentPass(pass);
}

ScopedRenderPass::~ScopedRenderPass() {
    RenderStats::GetInstance()->SetCurrentPass(previousPass_);
}

} // namespace engine
