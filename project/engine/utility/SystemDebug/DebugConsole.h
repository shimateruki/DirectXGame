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

class DebugConsole {
public:
    static DebugConsole* GetInstance();
    void Initialize();
    void Finalize();

    void AddLog(const std::string& log);
    void AddLog(LogLevel level, const std::string& log);
    void DrawImGui();
    uint64_t GetErrorCount() const { return errorCount_.load(std::memory_order_relaxed); }

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
    std::atomic<uint64_t> errorCount_{ 0 };
    bool autoScroll_ = true;
#ifdef USE_IMGUI
    ImGuiTextFilter filter_;

    // Log Levelごとの表示Filter。
    bool showInfo_ = true;
    bool showWarn_ = true;
    bool showError_ = true;
#endif
};
