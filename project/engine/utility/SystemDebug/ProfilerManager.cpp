#include "ProfilerManager.h"
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#include "IconsFontAwesome5.h"
#endif

ProfilerManager* ProfilerManager::GetInstance() {
    static ProfilerManager instance;
    return &instance;
}

void ProfilerManager::Initialize() {
    loadDataMap_.clear();
}

void ProfilerManager::RecordLoadTime(const std::string& category, const std::string& name, float timeMs) {
    loadDataMap_[category].push_back({ name, timeMs });
}

void ProfilerManager::DrawImGui() {
#ifdef USE_IMGUI
    if (!isOpen_) return;

    // ウィンドウの初期サイズを設定
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(ICON_FA_CHART_BAR " システムプロファイラ (System Profiler)", &isOpen_)) {
        
        // ==========================================================
        // 左ペイン：メニュー
        // ==========================================================
        ImGui::BeginChild("LeftPane", ImVec2(200, 0), true);
        
        const char* menuItems[] = {
            ICON_FA_DOWNLOAD " ロード時間 (Assets)",
            ICON_FA_CLOCK " 処理時間 (Timeline)"
        };

        for (int i = 0; i < IM_ARRAYSIZE(menuItems); i++) {
            if (ImGui::Selectable(menuItems[i], selectedIndex_ == i)) {
                selectedIndex_ = i;
            }
        }
        
        ImGui::EndChild();

        ImGui::SameLine();

        // ==========================================================
        // 右ペイン：詳細表示
        // ==========================================================
        ImGui::BeginChild("RightPane", ImVec2(0, 0), true);

        if (selectedIndex_ == 0) {
            // ---------------------------------------------------------
            // ロード時間表示
            // ---------------------------------------------------------
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ アセット・ロード時間 ]");
            ImGui::Separator();
            ImGui::Spacing();

            for (auto& pair : loadDataMap_) {
                const std::string& category = pair.first;
                auto& records = pair.second;

                if (ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::BeginTable(category.c_str(), 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("Asset Name", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();

                        float maxTime = 0.0f;
                        for (auto& record : records) if (record.timeMs > maxTime) maxTime = record.timeMs;

                        for (auto& record : records) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(record.name.c_str());
                            ImGui::TableSetColumnIndex(1); 
                            if (record.timeMs > 16.6f) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%.2f ms", record.timeMs);
                            else ImGui::Text("%.2f ms", record.timeMs);

                            ImGui::TableSetColumnIndex(2);
                            float fraction = (maxTime > 0) ? (record.timeMs / maxTime) : 0.0f;
                            ImVec4 color = ImVec4(0.2f, 0.7f, 0.2f, 1.0f);
                            if (record.timeMs > 10.0f) color = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
                            if (record.timeMs > 30.0f) color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);

                            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
                            ImGui::PushID(record.name.c_str());
                            ImGui::ProgressBar(fraction, ImVec2(-1, 0), "");
                            ImGui::PopID();
                            ImGui::PopStyleColor();
                        }
                        ImGui::EndTable();
                    }
                }
            }
        }
        else if (selectedIndex_ == 1) {
            // ---------------------------------------------------------
            // 処理時間表示 (今後追加予定)
            // ---------------------------------------------------------
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ 処理タイムライン ]");
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped("今後、PBR、シャドウ、ライト、物理演算などの階層的な処理時間をここに表示します。");
        }

        ImGui::EndChild();
    }
    ImGui::End();
#endif
}

