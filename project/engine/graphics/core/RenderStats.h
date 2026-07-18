#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 1フレーム内の描画命令を用途別に分類します。
enum class RenderPass : uint8_t {
    MainScene,
    Shadow,
    CameraPreview,
    GameUI,
    PostProcess,
    EditorOverlay,
    Other,
    Count
};

constexpr size_t kRenderPassCount = static_cast<size_t>(RenderPass::Count);

// 1種類の描画パスでGPUへ送った処理量です。
struct RenderPassStats {
    uint64_t drawCalls = 0;
    uint64_t indexedDrawCalls = 0;
    uint64_t indirectDrawCalls = 0;
    uint64_t submittedObjects = 0;
    uint64_t culledObjects = 0;
    uint64_t submittedVertices = 0;
    uint64_t submittedIndices = 0;
    uint64_t submittedTriangles = 0;
    uint64_t submittedInstances = 0;
    uint64_t computeDispatches = 0;
    uint64_t computeThreadGroups = 0;
};

// Editorへ公開する1フレーム分の描画統計です。
struct RenderFrameStats {
    uint64_t frameNumber = 0;
    std::array<RenderPassStats, kRenderPassCount> passes{};
    uint32_t activePointLights = 0;
    uint32_t activeSpotLights = 0;
    uint32_t gpuParticleSystems = 0;
    uint64_t gpuParticleCapacity = 0;
    uint64_t cpuParticleCount = 0;
    uint32_t postProcessPasses = 0;
};

// RenderStatsは実際に発行したDraw/Dispatchをフレーム単位で集計します。
// 描画内容には干渉せず、次の軽量化前後を同じ指標で比較するために使います。
class RenderStats {
public:
    static RenderStats* GetInstance();

    void BeginFrame();
    void EndFrame();

    void SetCurrentPass(RenderPass pass);
    RenderPass GetCurrentPass() const { return currentPass_; }

    void RecordIndexedDraw(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t trianglesPerInstance = 0);
    void RecordNonIndexedDraw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t trianglesPerInstance = 0);
    void RecordNonIndexedIndirectDraw();
    void RecordComputeDispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1);
    void RecordObjectDraw();
    void RecordCulledObject();
    void RecordGpuParticleSystem(uint32_t capacity);
    void RecordCpuParticles(uint32_t activeCount);
    void RecordPostProcessPass();
    void SetActiveLightCounts(uint32_t pointLights, uint32_t spotLights);

    const RenderFrameStats& GetLastCompletedFrame() const { return completedFrame_; }
    static const char* GetPassDisplayName(RenderPass pass);

private:
    RenderStats() = default;

    RenderPassStats* GetCurrentPassStats();

    RenderFrameStats currentFrame_{};
    RenderFrameStats completedFrame_{};
    RenderPass currentPass_ = RenderPass::Other;
    uint64_t nextFrameNumber_ = 1;
    bool frameActive_ = false;
};

// 入れ子になった描画処理でも、終了時に元の分類へ戻します。
class ScopedRenderPass {
public:
    explicit ScopedRenderPass(RenderPass pass);
    ~ScopedRenderPass();

    ScopedRenderPass(const ScopedRenderPass&) = delete;
    ScopedRenderPass& operator=(const ScopedRenderPass&) = delete;

private:
    RenderPass previousPass_ = RenderPass::Other;
};
