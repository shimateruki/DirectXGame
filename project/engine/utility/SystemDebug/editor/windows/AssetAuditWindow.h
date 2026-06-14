#pragma once

#include "IEditable.h"
#include "json.hpp"

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
    bool LoadLatestReport();
    void DrawSummary();
    void DrawHeavyAssets();
    void DrawUnusedAssets();
    void DrawMissingReferences();
    void DrawDeleteConfirmPopup();
    bool MatchesSearch(const nlohmann::json& item) const;
    bool MoveAssetToTrash(const std::string& relativePath, std::vector<std::string>& movedPaths, std::string& errorMessage);
    void RemoveMovedAssetsFromReport(const std::vector<std::string>& movedPaths);

private:
    DebugEditor* editor_ = nullptr;
    nlohmann::json latestReport_;
    bool hasReport_ = false;
    char searchBuffer_[256] = "";
    std::string pendingDeletePath_;
    std::string lastStatus_ = "tools/asset_audit.ps1 を実行すると、外部ツールの監査結果をここで確認できます。";
};
