#pragma once

#include "IEditable.h"
#include "json.hpp"

#include <string>

class DebugEditor;

/// Resources/jsonの変更を外部ツールでバックアップし、直近履歴を確認するためのウィンドウ。
class JsonBackupWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "JSONバックアップ (Json Backup)"; }

private:
    /// 現在のJSON状態を手動で1回バックアップする。
    bool RunBackupOnce();
    /// JSON変更監視プロセスを起動し、自動バックアップを開始する。
    bool StartWatcher(bool quiet);
    /// バックアップツールの最新レポートを読み込んでUI表示へ反映する。
    bool LoadLatestReport();
    void DrawSummary();
    /// 直近に作成されたバックアップ一覧を表示し、復旧判断をしやすくする。
    void DrawRecentBackups();

private:
    DebugEditor* editor_ = nullptr;
    nlohmann::json latestReport_;
    bool hasReport_ = false;
    bool watcherStartAttempted_ = false;
    std::string lastStatus_ = "Resources/json の変更を外部ツールでバックアップします。";
};
