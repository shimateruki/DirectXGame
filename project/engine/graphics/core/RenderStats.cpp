#include "RenderStats.h"

#include <algorithm>

RenderStats* RenderStats::GetInstance() {
    static RenderStats instance;
    return &instance;
}

void RenderStats::BeginFrame() {
    currentFrame_ = {};
    currentFrame_.frameNumber = nextFrameNumber_++;
    currentPass_ = RenderPass::Other;
    frameActive_ = true;
}

void RenderStats::EndFrame() {
    if (!frameActive_) {
        return;
    }

    completedFrame_ = currentFrame_;
    currentPass_ = RenderPass::Other;
    frameActive_ = false;
}

void RenderStats::SetCurrentPass(RenderPass pass) {
    currentPass_ = pass < RenderPass::Count ? pass : RenderPass::Other;
}

void RenderStats::RecordIndexedDraw(uint32_t indexCount, uint32_t instanceCount, uint32_t trianglesPerInstance) {
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
    if (RenderPassStats* stats = GetCurrentPassStats()) {
        ++stats->drawCalls;
        ++stats->indirectDrawCalls;
    }
}

void RenderStats::RecordComputeDispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
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
    if (!frameActive_) {
        return;
    }
    currentFrame_.activePointLights = pointLights;
    currentFrame_.activeSpotLights = spotLights;
}

const char* RenderStats::GetPassDisplayName(RenderPass pass) {
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
