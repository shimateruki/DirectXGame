#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <deque> 
#include <mutex>
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include "Log.h" 

// 担当：スレッド間で受け取ったログを蓄積し、ImGuiに表示する。
// 関連：Log（出力）、ImGuiManager（UI基盤）
class DebugConsole {
public:
    /// <summary>共有のデバッグコンソールを取得する。</summary>
    static DebugConsole* GetInstance();

    /// <summary>ログと表示状態を初期化する。</summary>
    void Initialize();

    /// <summary>保持中のログを破棄する。</summary>
    void Finalize();

    /// <summary>情報レベルのログを追加する。</summary>
    void AddLog(const std::string& log);

    /// <summary>レベルを指定してログを追加する。</summary>
    void AddLog(LogLevel level, const std::string& log);

    /// <summary>蓄積したログをImGuiウィンドウに描画する。</summary>
    void DrawImGui();

    /// <summary>追加されたエラーレベルのログ件数を返す。</summary>
    uint64_t GetErrorCount() const { return errorCount_.load(std::memory_order_relaxed); }

private:
    DebugConsole() = default;

    ~DebugConsole() = default;

    DebugConsole(const DebugConsole&) = delete;

    DebugConsole& operator=(const DebugConsole&) = delete;

    /// <summary>表示するメッセージと重大度の組。</summary>
    // 関連：DebugConsole（この型を使う機能の本体）
    struct LogEntry {
        std::string message;
        LogLevel level;
    };

    std::deque<LogEntry> logs_;  
    std::mutex logMutex_;

    bool scrollToBottom_ = true;
    std::atomic<uint64_t> errorCount_{ 0 };
    bool autoScroll_ = true;
#ifdef USE_IMGUI
    ImGuiTextFilter filter_;

    // ログレベルごとの表示切り替え。
    bool showInfo_ = true;
    bool showWarn_ = true;
    bool showError_ = true;
#endif
};
