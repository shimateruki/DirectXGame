#include "DebugConsole.h"
#include "externals/imgui/imgui.h"
#include <windows.h> // OutputDebugStringA のため

DebugConsole* DebugConsole::GetInstance() {
    static DebugConsole instance;
    return &instance;
}

void DebugConsole::Initialize() {
    logs_.clear();
    scrollToBottom_ = true;
    AddLog("--- Debug Console Initialized ---");
}

void DebugConsole::Finalize() {
    logs_.clear();
}

void DebugConsole::AddLog(const std::string& log) {
    std::lock_guard<std::mutex> lock(logMutex_);

    // (ログの最大数を 200 に制限)
    const size_t maxLogs = 200;
    if (logs_.size() > maxLogs) {
        logs_.erase(logs_.begin(), logs_.begin() + (logs_.size() - maxLogs));
    }

    logs_.push_back(log);
    scrollToBottom_ = true; // 新しいログが来たら自動スクロール

    // ★ VS のデバッグ出力にも同時に出す
    OutputDebugStringA((log + "\n").c_str());
}

void DebugConsole::DrawImGui() {
    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(logMutex_);
        logs_.clear();
    }
    ImGui::Separator();

    // ★ スクロール領域 (古い ImGui [cite] でも動く書き方)
    ImGui::BeginChild("LogScrollingRegion", ImVec2(0, 150), true, 0);
    {
        std::lock_guard<std::mutex> lock(logMutex_);
        for (const auto& log : logs_) {
            ImGui::TextUnformatted(log.c_str());
        }

        if (scrollToBottom_) {
            ImGui::SetScrollY(ImGui::GetScrollMaxY());
            scrollToBottom_ = false;
        }
    }
    ImGui::EndChild();
}