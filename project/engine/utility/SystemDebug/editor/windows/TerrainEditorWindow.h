#pragma once

#include "IEditable.h"
#include "json.hpp"

#include <cstdint>
#include <future>
#include <string>
#include <vector>

class DebugEditor;

class TerrainEditorWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "地形生成 (Terrain Builder)"; }

private:
    bool RunTerrainBuilder();
    void PollTerrainBuilder();
    bool LoadLatestReport();
    void CreateTerrainPreview();
    void DrawRealtimePreview();
    std::vector<float> BuildPreviewHeightField(int previewResolution) const;
    float SamplePreviewHeight(int x, int z, int previewResolution) const;
    uint32_t HeightToColor(float normalizedHeight) const;
    void SetNotice(const std::string& message, bool success);

private:
    DebugEditor* editor_ = nullptr;
    char terrainNameBuffer_[128] = "terrain_test";
    char heightMapPathBuffer_[260] = "";
    int resolution_ = 48;
    float sizeX_ = 48.0f;
    float sizeZ_ = 48.0f;
    float height_ = 3.0f;
    int seed_ = 1;
    float noiseScale_ = 0.12f;
    int smoothSteps_ = 2;
    float terrace_ = 0.0f;
    int materialType_ = 23;
    float heightMapStrength_ = 1.0f;
    float paintStrength_ = 0.75f;
    int paintPresetIndex_ = 0;
    bool autoPreviewAfterGenerate_ = false;
    bool livePreviewEnabled_ = true;
    bool previewWireframe_ = true;
    int previewResolution_ = 32;
    bool useTerrainHeightCollider_ = true;
    bool useSimpleAabbCollider_ = false;
    bool fitColliderToHeight_ = true;
    bool invertHeightMap_ = false;
    bool generatePaintMap_ = true;
    bool hasReport_ = false;
    bool isGenerating_ = false;
    bool previewAfterAsyncGenerate_ = false;
    std::future<uint32_t> generationFuture_;
    nlohmann::json latestReport_;
    std::string lastStatus_ = "地形パラメータを調整して、OBJ地形を生成してください。";
    float noticeTimer_ = 0.0f;
    bool noticeSuccess_ = true;
};
