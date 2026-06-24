#pragma once

#include "IEditable.h"
#include "json.hpp"

#include <cstdint>
#include <future>
#include <map>
#include <string>
#include <vector>

class DebugEditor;

class AssetAuditWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "アセット監査 (Asset Audit)"; }

private:
    bool RunAuditTool();
    void UpdateAuditProcess();
    bool LoadLatestReport();
    void DrawSummary();
    void DrawHeavyAssets();
    void DrawUnusedAssets();
    void DrawMissingReferences();
    void DrawDeleteConfirmPopup();
    void DrawAssetPreview(const nlohmann::json& item, float size);
    bool OpenExternalPath(const std::string& relativePath);
    uint32_t GetPreviewTextureHandle(const std::string& relativePath);
    bool PlayAudioPreview(const std::string& relativePath, bool isBgm);
    void CreateModelPreview(const std::string& relativePath);
    void RemoveModelPreviews();
    int CountModelPreviews() const;
    bool MatchesSearch(const nlohmann::json& item) const;
    bool MoveAssetToTrash(const std::string& relativePath, std::vector<std::string>& movedPaths, std::string& errorMessage);
    void RemoveMovedAssetsFromReport(const std::vector<std::string>& movedPaths);

private:
    DebugEditor* editor_ = nullptr;
    nlohmann::json latestReport_;
    bool hasReport_ = false;
    char searchBuffer_[256] = "";
    std::string pendingDeletePath_;
    std::string lastStatus_ = "必要なタイミングで監査ツールを実行、または前回レポートを読み込んでください。";
    bool showPreviewThumbnails_ = false;
    bool auditRunning_ = false;
    int maxRowsToDraw_ = 200;
    std::future<std::uint32_t> auditFuture_;
    std::map<std::string, uint32_t> previewTextureHandles_;
    std::map<std::string, uint32_t> previewAudioHandles_;
};
