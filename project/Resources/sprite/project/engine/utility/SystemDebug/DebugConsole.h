#pragma once
#include <string>
#include <deque> 
#include <mutex>
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#include "Log.h" 

class DebugConsole {
public:
    static DebugConsole* GetInstance();
    void Initialize();
    void Finalize();

    void AddLog(const std::string& log);
    void AddLog(LogLevel level, const std::string& log);
    void DrawImGui();

private:
    DebugConsole() = default;
    ~DebugConsole() = default;
    DebugConsole(const DebugConsole&) = delete;
    DebugConsole& operator=(const DebugConsole&) = delete;

    struct LogEntry {
        std::string message;
        LogLevel level;
    };

    std::deque<LogEntry> logs_;  
    std::mutex logMutex_;

    bool scrollToBottom_ = true;
    bool autoScroll_ = true;
#ifdef USE_IMGUI
    ImGuiTextFilter filter_;

    // ★追加: ログレベルの表示ON/OFFフラグ
    bool showInfo_ = true;
    bool showWarn_ = true;
    bool showError_ = true;
#endif
};