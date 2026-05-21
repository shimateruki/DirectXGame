#include "DebugConsole.h"
#include <Windows.h> 

DebugConsole* DebugConsole::GetInstance() {
    static DebugConsole instance;
    return &instance;
}

void DebugConsole::Initialize() {
#ifdef USE_IMGUI
    std::lock_guard<std::mutex> lock(logMutex_);
    //logs_.clear();
    scrollToBottom_ = true;
#endif
}

void DebugConsole::Finalize() {
#ifdef USE_IMGUI
    std::lock_guard<std::mutex> lock(logMutex_);
    logs_.clear();
#endif
}

void DebugConsole::AddLog(const std::string& log) {
#ifdef USE_IMGUI
    LogLevel level = LogLevel::Info;
    if (log.find("[ERROR]") != std::string::npos) level = LogLevel::Error;
    else if (log.find("[WARN]") != std::string::npos) level = LogLevel::Warning;

    AddLog(level, log);
#endif
}

void DebugConsole::AddLog(LogLevel level, const std::string& log) {
#ifdef USE_IMGUI
    std::lock_guard<std::mutex> lock(logMutex_);

    const size_t maxLogs = 200;

    while (logs_.size() >= maxLogs) {
        logs_.pop_front();
    }

    logs_.push_back({ log, level });

    if (autoScroll_) {
        scrollToBottom_ = true;
    }

    std::string outStr = log;
    if (outStr.empty() || outStr.back() != '\n') outStr += "\n";
    OutputDebugStringA(outStr.c_str());
#endif
}

void DebugConsole::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debug Console")) {
        ImGui::End();
        return;
    }

    // --- ツールバー ---
    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(logMutex_);
        logs_.clear();
    }
    ImGui::SameLine();
    bool copy = ImGui::Button("Copy");
    ImGui::SameLine();

    // フィルタの幅を少し調整
    filter_.Draw("Filter", -200.0f);

    //  ログレベル・トグルボタン
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo_); ImGui::SameLine();
    ImGui::Checkbox("Warn", &showWarn_); ImGui::SameLine();
    ImGui::Checkbox("Error", &showError_);

    ImGui::Separator();

    // --- スクロール領域 ---
    ImGui::BeginChild("LogScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lock(logMutex_);

        // ★追加: コピー処理の完全版（表示中のログだけを結合してコピー）
        if (copy) {
            std::string clipboardText;
            for (const auto& entry : logs_) {
                if (!filter_.PassFilter(entry.message.c_str())) continue;
                if (entry.level == LogLevel::Info && !showInfo_) continue;
                if (entry.level == LogLevel::Warning && !showWarn_) continue;
                if (entry.level == LogLevel::Error && !showError_) continue;

                clipboardText += entry.message + "\n";
            }
            ImGui::SetClipboardText(clipboardText.c_str());
        }

        // ログ描画ループ
        for (const auto& entry : logs_) {
            // フィルタ適用
            if (!filter_.PassFilter(entry.message.c_str())) continue;

            // ★追加: チェックが外れているレベルのログはスキップ！
            if (entry.level == LogLevel::Info && !showInfo_) continue;
            if (entry.level == LogLevel::Warning && !showWarn_) continue;
            if (entry.level == LogLevel::Error && !showError_) continue;

            // 色設定
            bool hasColor = true;
            ImVec4 color;
            switch (entry.level) {
            case LogLevel::Error:   color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break; // 赤
            case LogLevel::Warning: color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); break; // 黄
            default:                hasColor = false; break; // 白
            }

            if (hasColor) ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(entry.message.c_str());
            if (hasColor) ImGui::PopStyleColor();
        }

        // スクロール処理
        if (scrollToBottom_) {
            ImGui::SetScrollHereY(1.0f);
            scrollToBottom_ = false;
        }
    }
    ImGui::EndChild();

    // --- フッター ---
    ImGui::Separator();
    ImGui::Checkbox("Auto-scroll", &autoScroll_);

    ImGui::End();
#endif
}