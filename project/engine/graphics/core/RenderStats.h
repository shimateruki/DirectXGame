#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace engine {

/// <summary>描画命令を 1 フレーム内の用途別に分類します。</summary>
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

/// <summary>1 種類の描画パスで GPU へ送った処理量です。</summary>
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

/// <summary>Editor へ公開する 1 フレーム分の描画統計です。</summary>
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

/// <summary>実際に発行した Draw/Dispatch をフレーム単位で集計します。</summary>
/// 描画内容には干渉せず、軽量化前後を同じ指標で比較するために使います。
class RenderStats {
public:
    /// <summary>エンジンで共有する統計インスタンスを取得します。</summary>
    static RenderStats* GetInstance();

    /// <summary>集計中の統計を空にして新しいフレームを開始します。</summary>
    void BeginFrame();

    /// <summary>集計結果を Editor 用の完了フレームとして公開します。</summary>
    void EndFrame();

    /// <summary>以降の描画命令を分類するパスを設定します。</summary>
    void SetCurrentPass(RenderPass pass);

    /// <summary>現在の描画パスを取得します。</summary>
    RenderPass GetCurrentPass() const { return currentPass_; }

    /// <summary>インデックス付き Draw の回数と投入量を記録します。</summary>
    /// <param name="indexCount">1 インスタンスあたりのインデックス数。</param>
    /// <param name="instanceCount">描画したインスタンス数。</param>
    /// <param name="trianglesPerInstance">分かる場合の三角形数。0 なら indexCount / 3 で推定します。</param>
    void RecordIndexedDraw(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t trianglesPerInstance = 0);

    /// <summary>インデックスを使わない Draw の回数と投入量を記録します。</summary>
    void RecordNonIndexedDraw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t trianglesPerInstance = 0);

    /// <summary>インデックスを使わない間接 Draw を記録します。</summary>
    void RecordNonIndexedIndirectDraw();

    /// <summary>Compute Dispatch の回数とスレッドグループ数を記録します。</summary>
    void RecordComputeDispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1);

    /// <summary>描画へ提出したオブジェクトを 1 個記録します。</summary>
    void RecordObjectDraw();

    /// <summary>描画を省いたオブジェクトを 1 個記録します。</summary>
    void RecordCulledObject();

    /// <summary>稼働中の GPU パーティクルシステムと収容数を記録します。</summary>
    void RecordGpuParticleSystem(uint32_t capacity);

    /// <summary>稼働中の CPU パーティクル数を加算します。</summary>
    void RecordCpuParticles(uint32_t activeCount);

    /// <summary>PostProcess パス中の後処理を 1 件記録します。</summary>
    void RecordPostProcessPass();

    /// <summary>このフレームの有効な点光源とスポットライト数を設定します。</summary>
    void SetActiveLightCounts(uint32_t pointLights, uint32_t spotLights);

    /// <summary>最後に完了したフレームの統計を取得します。</summary>
    const RenderFrameStats& GetLastCompletedFrame() const { return completedFrame_; }

    /// <summary>描画パスの Editor 表示名を取得します。</summary>
    static const char* GetPassDisplayName(RenderPass pass);

private:
    RenderStats() = default;

    // フレーム外の記録を無視するため、集計中のパスだけを返します。
    RenderPassStats* GetCurrentPassStats();

    // 集計中と Editor 向けに公開済みのフレームを分離します。
    RenderFrameStats currentFrame_{};
    RenderFrameStats completedFrame_{};
    RenderPass currentPass_ = RenderPass::Other;
    uint64_t nextFrameNumber_ = 1;
    bool frameActive_ = false;
};

/// <summary>スコープを抜けたときに、元の描画パスへ戻します。</summary>
class ScopedRenderPass {
public:
    /// <summary>現在の描画パスを保存し、一時的な分類を設定します。</summary>
    explicit ScopedRenderPass(RenderPass pass);

    /// <summary>保存した描画パスへ戻します。</summary>
    ~ScopedRenderPass();

    ScopedRenderPass(const ScopedRenderPass&) = delete;
    ScopedRenderPass& operator=(const ScopedRenderPass&) = delete;

private:
    RenderPass previousPass_ = RenderPass::Other;
};

} // namespace engine
