#pragma once

#include "IEditable.h"
#include "json.hpp"

#include <string>

class DebugEditor;

class JsonBackupWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "JSONバックアップ (Json Backup)"; }

private:
    bool RunBackupOnce();
    bool StartWatcher(bool quiet);
    bool LoadLatestReport();
    void DrawSummary();
    void DrawRecentBackups();

private:
    DebugEditor* editor_ = nullptr;
    nlohmann::json latestReport_;
    bool hasReport_ = false;
    bool watcherStartAttempted_ = false;
    std::string lastStatus_ = "Resources/json の変更を外部ツールでバックアップします。";
};
