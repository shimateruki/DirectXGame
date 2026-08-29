#include "ProfilerManager.h"
#include "DirectXCommon.h"
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#include "IconsFontAwesome5.h"
#include "Object3d.h"
#include "Model.h"
#endif


ProfilerManager* ProfilerManager::GetInstance() {
    static ProfilerManager instance;
    return &instance;
}

void ProfilerManager::Initialize() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    loadDataMap_.clear();
    latestGpuTimeMap_.clear();
}

void ProfilerManager::RecordLoadTime(const std::string& category, const std::string& name, float timeMs) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    loadDataMap_[category].push_back({ name, timeMs });
}

void ProfilerManager::RecordGpuTime(const std::string& name, float timeMs) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    latestGpuTimeMap_[name] = timeMs;

    std::string displayName = name;
    if (name == "Total") displayName = "全体";

    // --- GPUサンプリング処理 (オブジェクト個別負荷など) ---
    if (isGpuSampling_) {
        gpuSampleAccum_[name] += timeMs;
        gpuSampleCount_[name]++;
    }

    // --- タイムライン（全体グラフ）に表示する項目を制限 ---
    // ここに含まれない名前（個別のオブジェクト名など）はグラフに出さない
    static const std::vector<std::string> globalNames = {
        "全体", "影描画", "メイン描画", "  3Dシーン", "  ゲームUI", "  エフェクト", "  デバッグ", "後処理", "エディタUI"
    };
    
    bool isGlobal = false;
    for(const auto& gn : globalNames) {
        if(displayName == gn) { isGlobal = true; break; }
    }
    if (!isGlobal) return; // グローバル項目以外は以下の履歴記録をスキップ

    auto& data = gpuDataMap_[displayName];
    data.current = timeMs;
    // α=0.1の指数移動平均でFrame間の計測ノイズを抑えます。
    data.smoothed = data.smoothed * 0.9f + timeMs * 0.1f;
    // 履歴リングバッファに記録
    data.history[data.historyIndex] = timeMs;
    data.historyIndex = (data.historyIndex + 1) % kHistorySize;
}

void ProfilerManager::RecordCpuTime(const std::string& name, float timeMs) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto& data = cpuDataMap_[name];
    data.current = timeMs;
    data.smoothed = data.smoothed * 0.9f + timeMs * 0.1f;
    data.history[data.historyIndex] = timeMs;
    data.historyIndex = (data.historyIndex + 1) % kHistorySize;
}

float ProfilerManager::GetLatestGpuTime(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = latestGpuTimeMap_.find(name);
    return it != latestGpuTimeMap_.end() ? it->second : 0.0f;
}

float ProfilerManager::GetLatestCpuTime(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto it = cpuDataMap_.find(name);
    return it != cpuDataMap_.end() ? it->second.current : 0.0f;
}

void ProfilerManager::DrawImGui() {
#ifdef USE_IMGUI
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!isOpen_) return;

    // ウィンドウの初期サイズを設定
    ImGui::SetNextWindowSize(ImVec2(850, 600), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(ICON_FA_CHART_BAR " システムプロファイラ (System Profiler)", &isOpen_)) {
        
        // ==========================================================
        // 左ペイン：メニュー
        // ==========================================================
        ImGui::BeginChild("LeftPane", ImVec2(200, 0), true);
        
        const char* menuItems[] = {
            ICON_FA_DOWNLOAD " ロード時間 (Assets)",
            ICON_FA_CLOCK " 処理時間 (Timeline)",
            ICON_FA_CUBE " オブジェクト負荷 (Objects)"
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
                        ImGui::TableSetupColumn("アセット名", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("時間 (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn("グラフ", ImGuiTableColumnFlags_WidthStretch);
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
            // 処理時間表示 (Timeline) - GPU + CPU
            // ---------------------------------------------------------

            // ============================
            // GPU セクション
            // ============================
            ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), ICON_FA_MICROCHIP "  GPU タイムライン");
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginTable("GpuTimeline", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("パス名", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("現在値 (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("平均 (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("履歴 (120フレーム)", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (auto& pair : gpuDataMap_) {
                    const std::string& name = pair.first;
                    auto& data = pair.second;

                    ImGui::TableNextRow();
                    
                    // Pass名
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(name.c_str());

                    // 現在値（生値）
                    ImGui::TableSetColumnIndex(1);
                    ImVec4 valColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                    if (data.current > 10.0f) valColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
                    if (data.current > 16.66f) valColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                    ImGui::TextColored(valColor, "%.3f", data.current);

                    // 平均値（スムージング後）
                    ImGui::TableSetColumnIndex(2);
                    ImVec4 avgColor = ImVec4(0.5f, 0.9f, 0.5f, 1.0f);
                    if (data.smoothed > 10.0f) avgColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
                    if (data.smoothed > 16.66f) avgColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                    ImGui::TextColored(avgColor, "%.3f", data.smoothed);

                    // 履歴グラフ（折れ線）
                    ImGui::TableSetColumnIndex(3);
                    // リングバッファを正しい順序で描画するため配列を並べ直す
                    float plotData[kHistorySize];
                    for (int i = 0; i < kHistorySize; i++) {
                        plotData[i] = data.history[(data.historyIndex + i) % kHistorySize];
                    }
                    ImGui::PushID(name.c_str());
                    ImGui::PlotLines("", plotData, kHistorySize, 0, nullptr, 0.0f, 20.0f, ImVec2(-1, 30));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // ============================
            // CPU セクション
            // ============================
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), ICON_FA_MEMORY "  CPU タイムライン");
            ImGui::Separator();
            ImGui::Spacing();

            if (!cpuDataMap_.empty()) {
                if (ImGui::BeginTable("CpuTimeline", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                    ImGui::TableSetupColumn("セクション", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    ImGui::TableSetupColumn("現在値 (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("平均 (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("履歴 (120フレーム)", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (auto& pair : cpuDataMap_) {
                        const std::string& name = pair.first;
                        auto& data = pair.second;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name.c_str());
                        
                        ImGui::TableSetColumnIndex(1);
                        ImVec4 valColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                        if (data.current > 10.0f) valColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
                        if (data.current > 16.66f) valColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                        ImGui::TextColored(valColor, "%.3f", data.current);

                        ImGui::TableSetColumnIndex(2);
                        ImVec4 avgColor = ImVec4(0.5f, 0.9f, 0.5f, 1.0f);
                        if (data.smoothed > 10.0f) avgColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
                        if (data.smoothed > 16.66f) avgColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                        ImGui::TextColored(avgColor, "%.3f", data.smoothed);

                        ImGui::TableSetColumnIndex(3);
                        float plotData[kHistorySize];
                        for (int i = 0; i < kHistorySize; i++) {
                            plotData[i] = data.history[(data.historyIndex + i) % kHistorySize];
                        }
                        ImGui::PushID(name.c_str());
                        ImGui::PlotLines("", plotData, kHistorySize, 0, nullptr, 0.0f, 20.0f, ImVec2(-1, 30));
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            } else {
                ImGui::TextDisabled("CPU計測データがありません");
            }

            ImGui::Spacing();
            ImGui::TextDisabled("(16.66ms = 60FPS基準)  現在値 = 生データ / 平均 = 移動平均");
        }
        else if (selectedIndex_ == 2) {
            // ---------------------------------------------------------
            // オブジェクト別負荷表示
            // ---------------------------------------------------------
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ オブジェクト別・GPU/CPU負荷 ]");
            ImGui::Separator();
            ImGui::Spacing();

            // --- サンプリング操作パネル ---
            if (isGpuSampling_) {
                remainingSamplingFrames_--;
                ImGui::TextColored(ImVec4(1, 1, 0, 1), ICON_FA_SYNC " GPUサンプリング中... 残り %d フレーム", remainingSamplingFrames_);
                if (remainingSamplingFrames_ <= 0) {
                    isGpuSampling_ = false;
                    // 平均計算
                    gpuSampleResult_.clear();
                    for (auto& pair : gpuSampleAccum_) {
                        gpuSampleResult_[pair.first] = pair.second / static_cast<float>(gpuSampleCount_[pair.first]);
                    }
                }
            }
            else {
                if (ImGui::Button(ICON_FA_PLAY " GPU詳細サンプリング開始 (60フレーム)", ImVec2(-1, 30))) {
                    DirectXCommon::GetInstance()->ResetGpuProfiles(); // クエリ上限対策でリセット
                    StartGpuSampling();
                }
            }
            ImGui::Spacing();

            if (!currentObjects_ || currentObjects_->empty()) {
                ImGui::TextDisabled("オブジェクトが登録されていません。");
            }
            else {
                // --- カテゴリ別の合計計算 ---
                std::map<std::string, float> catGpuTotal;
                std::map<std::string, float> catCpuTotal;
                std::map<std::string, int> catCount;
                
                for (auto& obj : *currentObjects_) {
                    std::string cat = obj->GetSaveCategory();
                    catCount[cat]++;
                    catCpuTotal[cat] += obj->GetCpuUpdateTimeMs();
                    if (gpuSampleResult_.count(obj->GetName())) {
                        catGpuTotal[cat] += gpuSampleResult_[obj->GetName()];
                    }
                }

                if (ImGui::CollapsingHeader(ICON_FA_LIST " カテゴリ別・合計負荷サマリー", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::BeginTable("CategorySummary", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("カテゴリ", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("個数", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                        ImGui::TableSetupColumn("GPU合計 (ms)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableSetupColumn("CPU合計 (ms)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableHeadersRow();

                        for (auto& pair : catCount) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%s", pair.first.c_str());
                            ImGui::TableNextColumn(); ImGui::Text("%d", pair.second);
                            ImGui::TableNextColumn(); ImGui::Text("%.3f", catGpuTotal[pair.first]);
                            ImGui::TableNextColumn(); ImGui::Text("%.3f", catCpuTotal[pair.first]);
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::Spacing();

                static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable;
                if (ImGui::BeginTable("ObjectPerfTable", 5, flags)) {
                    ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("クラス", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("カテゴリ", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("GPU (ms avg)", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("CPU (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableHeadersRow();

                    auto& objects = *currentObjects_;
                    std::vector<Object3d*> sortedList;
                    for (auto& obj : objects) sortedList.push_back(obj.get());

                    ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs();
                    if (sort_specs && sort_specs->SpecsDirty) {
                        std::sort(sortedList.begin(), sortedList.end(), [&](Object3d* a, Object3d* b) {
                            for (int n = 0; n < sort_specs->SpecsCount; n++) {
                                const ImGuiTableColumnSortSpecs* spec = &sort_specs->Specs[n];
                                
                                if (spec->ColumnIndex == 0) { // 名前
                                    if (a->GetName() != b->GetName()) return (spec->SortDirection == ImGuiSortDirection_Ascending) ? (a->GetName() < b->GetName()) : (a->GetName() > b->GetName());
                                }
                                else if (spec->ColumnIndex == 1) { // クラス
                                    if (a->GetClassName() != b->GetClassName()) return (spec->SortDirection == ImGuiSortDirection_Ascending) ? (a->GetClassName() < b->GetClassName()) : (a->GetClassName() > b->GetClassName());
                                }
                                else if (spec->ColumnIndex == 2) { // カテゴリ
                                    if (a->GetSaveCategory() != b->GetSaveCategory()) return (spec->SortDirection == ImGuiSortDirection_Ascending) ? (a->GetSaveCategory() < b->GetSaveCategory()) : (a->GetSaveCategory() > b->GetSaveCategory());
                                }
                                else if (spec->ColumnIndex == 3) { // GPU
                                    float valA = gpuSampleResult_.count(a->GetName()) ? gpuSampleResult_[a->GetName()] : 0.0f;
                                    float valB = gpuSampleResult_.count(b->GetName()) ? gpuSampleResult_[b->GetName()] : 0.0f;
                                    if (valA != valB) return (spec->SortDirection == ImGuiSortDirection_Ascending) ? (valA < valB) : (valA > valB);
                                }
                                else if (spec->ColumnIndex == 4) { // CPU
                                    float valA = a->GetCpuUpdateTimeMs();
                                    float valB = b->GetCpuUpdateTimeMs();
                                    if (valA != valB) return (spec->SortDirection == ImGuiSortDirection_Ascending) ? (valA < valB) : (valA > valB);
                                }
                            }
                            return a < b;
                        });
                        sort_specs->SpecsDirty = false;
                    }

                    for (auto* obj : sortedList) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        
                        // 名前をツリーノードにして、クリックで詳細を展開できるようにする
                        bool open = ImGui::TreeNodeEx(obj->GetName().c_str(), ImGuiTreeNodeFlags_SpanFullWidth);
                        
                        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", obj->GetClassName().c_str());
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(obj->GetSaveCategory().c_str());
                        
                        ImGui::TableNextColumn();
                        if (gpuSampleResult_.count(obj->GetName())) {
                            float gpuTime = gpuSampleResult_[obj->GetName()];
                            ImVec4 color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
                            if (gpuTime > 0.1f) color = ImVec4(1, 1, 0, 1);
                            if (gpuTime > 0.5f) color = ImVec4(1, 0.5f, 0, 1);
                            ImGui::TextColored(color, "%.3f", gpuTime);
                        } else {
                            ImGui::TextDisabled("---");
                        }

                        ImGui::TableNextColumn(); ImGui::Text("%.3f", obj->GetCpuUpdateTimeMs());

                        // 詳細展開時の表示
                        if (open) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            
                            ImGui::Indent();
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), ICON_FA_INFO_CIRCLE " 処理の内訳 (CPU)");
                            ImGui::BulletText("Animation: %.3f ms", obj->GetCpuAnimTimeMs());
                            ImGui::BulletText("Update/Matrix: %.3f ms", obj->GetCpuMatrixTimeMs());
                            
                            ImGui::Spacing();
                            
                            // モデル情報の取得
                            Model* model = obj->GetModel();
                            
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), ICON_FA_CUBE " モデルスペック (GPU負荷要因)");
                            if (model) {
                                ImGui::BulletText("ポリゴン数: %u", model->GetPolygonCount());
                                ImGui::BulletText("頂点数: %u", model->GetVertexCount());
                                ImGui::BulletText("メッシュ数: %u", model->GetMeshCount());
                            } else {
                                ImGui::BulletText("モデル未ロード");
                            }

                            ImGui::Spacing();
                            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), ICON_FA_PAINT_BRUSH " マテリアル構成");
                            ImGui::BulletText("ライティング: %s", obj->GetEnableLighting() ? "ON" : "OFF");
                            ImGui::BulletText("法線マップ: %s", obj->GetEnableNormalMap() ? "ON" : "OFF");
                            ImGui::BulletText("環境マップ: %s", obj->GetEnableEnvMap() ? "ON" : "OFF");
                            
                            ImGui::Unindent();
                            ImGui::TreePop();
                        }
                    }
                    ImGui::EndTable();
                }
            }
        }

        ImGui::EndChild();
    }
    ImGui::End();
#endif
}
