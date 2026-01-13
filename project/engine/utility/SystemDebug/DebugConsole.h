#pragma once
#include <string>
#include <vector>
#include <mutex>
#include "imgui.h"

// LogLevelを使うためにインクルード (Log.hがない場合はここでenum定義してください)
#include "Log.h" 

class DebugConsole {
public:
    // シングルトン取得
    static DebugConsole* GetInstance();

    // 初期化
    void Initialize();

    // 終了処理
    void Finalize();

    // --- ログ追加 ---

    // 1. 既存互換用 (文字列のみ受け取る) -> デフォルトで Info 扱い、または文字列内のタグで色判定
    void AddLog(const std::string& log);

    // 2. 高機能版 (レベル指定あり) -> 色分け対応
    void AddLog(LogLevel level, const std::string& log);


    // ImGui描画 (名前は DrawImGui のまま)
    void DrawImGui();

private:
    DebugConsole() = default;
    ~DebugConsole() = default;
    DebugConsole(const DebugConsole&) = delete;
    DebugConsole& operator=(const DebugConsole&) = delete;

    // ログデータ構造体 (色情報を持たせるために拡張)
    struct LogEntry {
        std::string message;
        LogLevel level;
    };

    std::vector<LogEntry> logs_; // 文字列だけではなく構造体で管理
    std::mutex logMutex_;        // 排他制御用

    // UI制御用フラグ
    bool scrollToBottom_ = true;
    bool autoScroll_ = true;
    ImGuiTextFilter filter_;     // 検索フィルタ
};