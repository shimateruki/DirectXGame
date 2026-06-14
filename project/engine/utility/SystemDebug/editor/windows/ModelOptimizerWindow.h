#pragma once

#include "IEditable.h"
#include "json.hpp"

#include <string>
#include <vector>

class DebugEditor;
class Object3d;

class ModelOptimizerWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "モデル最適化 (Model Optimizer)"; }

private:
    void RefreshModelList();
    void SetModelName(const std::string& modelName);
    bool RunBuilder(bool analyzeOnly);
    bool LoadLatestReport();
    bool ApplyLodConfigToSelected();
    bool ApplyLodConfigToObject(Object3d* object);
    bool AcceptGeneratedLods();
    bool RejectGeneratedLods();
    int DeleteGeneratedLodFilesFromReport();
    void CreatePreviewObjects();
    void RemovePreviewObjects();
    std::string BuildReportSummary() const;
    std::string GetLodModelName(int lodLevel) const;
    int CountPreviewObjects() const;

private:
    DebugEditor* editor_ = nullptr;
    std::vector<std::string> modelCandidates_;
    int selectedModelIndex_ = -1;
    char modelNameBuffer_[256] = "";
    float lodRatios_[2] = { 0.55f, 0.25f };
    float lodDistances_[2] = { 35.0f, 70.0f };
    bool forceOverwrite_ = true;
    bool autoUseEffectPreviewStage_ = true;
    bool hasPendingGeneratedReview_ = false;
    Object3d* pendingApplyTarget_ = nullptr;
    nlohmann::json latestReport_;
    bool hasReport_ = false;
    std::string lastStatus_ = "モデルを選んで解析またはLOD生成を実行してください。";
};
