#include "DebugConsole.h"
#include <Windows.h> 

// シングルトン
DebugConsole* DebugConsole::GetInstance() {
    static DebugConsole instance;
    return &instance;
}

void DebugConsole::Initialize() {
#ifdef USE_IMGUI
    std::lock_guard<std::mutex> lock(logMutex_);
    logs_.clear();
    scrollToBottom_ = true;
#endif

}


void DebugConsole::Finalize() {
#ifdef USE_IMGUI
    std::lock_guard<std::mutex> lock(logMutex_);
    logs_.clear();
#endif
}

// 既存互換: 文字列だけの追加
void DebugConsole::AddLog(const std::string& log) {
#ifdef USE_IMGUI
    // 文字列の中に "[ERROR]" 等が含まれていれば色を変える簡易判定を入れると便利
    LogLevel level = LogLevel::Info;
    if (log.find("[ERROR]") != std::string::npos) level = LogLevel::Error;
    else if (log.find("[WARN]") != std::string::npos) level = LogLevel::Warning;

    // オーバーロードへ委譲
    AddLog(level, log);
#endif
}

// 高機能版: レベル指定での追加
void DebugConsole::AddLog(LogLevel level, const std::string& log) {
#ifdef USE_IMGUI
    std::lock_guard<std::mutex> lock(logMutex_);

    // ログ最大数制限 (200)
    const size_t maxLogs = 200;
    if (logs_.size() > maxLogs) {
        logs_.erase(logs_.begin(), logs_.begin() + (logs_.size() - maxLogs));
    }

    // 構造体として追加
    logs_.push_back({ log, level });

    // 自動スクロールが有効ならフラグを立てる
    if (autoScroll_) {
        scrollToBottom_ = true;
    }

    // VS出力 (改行が無ければ足す)
    std::string outStr = log;
    if (outStr.empty() || outStr.back() != '\n') outStr += "\n";
    OutputDebugStringA(outStr.c_str());
#endif
}

void DebugConsole::DrawImGui() {
#ifdef USE_IMGUI
    // ウィンドウ設定
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
    // 検索フィルタ描画
    filter_.Draw("Filter", -100.0f);

    ImGui::Separator();

    // --- スクロール領域 ---
    ImGui::BeginChild("LogScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lock(logMutex_);

        // ログ描画ループ
        for (const auto& entry : logs_) {
            // フィルタ適用 (ヒットしなければスキップ)
            if (!filter_.PassFilter(entry.message.c_str())) continue;

            // 色設定
            bool hasColor = true;
            ImVec4 color;
            switch (entry.level) {
            case LogLevel::Error:   color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break; // 赤
            case LogLevel::Warning: color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); break; // 黄
            default:                hasColor = false; break; // 白(デフォルト)
            }

            if (hasColor) ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(entry.message.c_str());
            if (hasColor) ImGui::PopStyleColor();
        }

        // クリップボードコピー
        if (copy) ImGui::LogToClipboard();

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