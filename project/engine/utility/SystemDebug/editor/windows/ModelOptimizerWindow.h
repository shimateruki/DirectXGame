#pragma once

#include "IEditable.h"
#include "json.hpp"

#include <string>
#include <vector>

class DebugEditor;
class Object3d;
struct Vector2;
struct Vector3;

/// モデルの解析、LOD生成、生成結果レビュー、シーンへのLOD設定反映を行うエディタウィンドウ。
class ModelOptimizerWindow : public IEditable {
public:
    /// 選択ObjectやシーンへLOD設定を反映するため、DebugEditor参照を保持する。
    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    void SetGameViewRegion(const Vector2& offset, const Vector2& size);
    std::string GetName() override { return "モデル最適化 (Model Optimizer)"; }

private:
    /// Resources/3DModel配下から最適化対象にできるモデル候補を収集する。
    void RefreshModelList();
    void SetModelName(const std::string& modelName);
    /// 外部LODビルダーを起動し、解析のみまたはLOD生成を非同期で実行する。
    bool RunBuilder(bool analyzeOnly);
    void UpdateBuilderProcess();
    void FinishBuilderProcess(unsigned long exitCode);
    void CloseBuilderProcessHandles();
    void UpdatePreviewCreation();
    void DrawPreviewLabels();
    bool ProjectWorldToGameView(const Vector3& world, Vector2& screenOut) const;
    bool LoadLatestReport();
    /// 生成レポートのLOD設定を現在選択中のObjectへ適用する。
    bool ApplyLodConfigToSelected();
    bool ApplyLodConfigToObject(Object3d* object);
    /// 生成したLODファイルを採用し、対象モデルのLOD manifestとして使える状態にする。
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
    int selectedBackendIndex_ = 0;
    char blenderPathBuffer_[512] = "";
    bool builderRunning_ = false;
    bool builderAnalyzeOnly_ = false;
    void* builderProcessHandle_ = nullptr;
    void* builderThreadHandle_ = nullptr;
    unsigned long builderProcessId_ = 0;
    bool forceOverwrite_ = true;
    bool autoUseEffectPreviewStage_ = true;
    bool showPreviewLabels_ = true;
    struct PendingPreviewItem {
        int level = 0;
        std::string modelName;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };
    std::vector<PendingPreviewItem> pendingPreviewItems_;
    size_t pendingPreviewIndex_ = 0;
    bool previewCreationActive_ = false;
    Object3d* firstPendingPreview_ = nullptr;
    float gameViewOffsetX_ = 0.0f;
    float gameViewOffsetY_ = 0.0f;
    float gameViewWidth_ = 1280.0f;
    float gameViewHeight_ = 720.0f;
    bool hasPendingGeneratedReview_ = false;
    Object3d* pendingApplyTarget_ = nullptr;
    nlohmann::json latestReport_;
    bool hasReport_ = false;
    std::string lastStatus_ = "モデルを選んで解析またはLOD生成を実行してください。";
};
