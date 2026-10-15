#include "PresetEditor.h"

#include "ModelManager.h"
#include "imgui.h"
#include "IconsFontAwesome5.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <utility>

// Singletonの寿命はEditor全体と同じで、外部から所有権を受け取りません。
PresetEditor* PresetEditor::GetInstance() {
    static PresetEditor instance;
    return &instance;
}

std::string PresetEditor::GetName() {
    // ツール一覧に表示する名称は内部プリセット名と分ける。
    return "プリセットエディタ (Preset Editor)";
}

#ifdef USE_IMGUI

// パレットの表示、選択、ドラッグ配置を担当します。
void PresetEditor::Initialize() {
    // カタログとモデル候補を先に読み込み、初回表示から配置候補を選べるようにする。
    PresetManager::GetInstance()->Initialize();
    RefreshModelList();
    if (std::strcmp(newName_, "NewPreset") == 0) std::snprintf(newName_, sizeof(newName_), "%s", "新しいプリセット");
}

void PresetEditor::SetPlacePresetCallback(std::function<void(const std::string&)> callback) {
    // 配置操作の実行はシーン編集側へ委ね、パレットは選択名だけを渡す。
    placePresetCallback_ = std::move(callback);
}

void PresetEditor::SetBrushPresetCallback(std::function<void(const std::string&)> callback) {
    // ブラシ配置の開始はシーン編集側へ委ねる。
    brushPresetCallback_ = std::move(callback);
}

void PresetEditor::SetThumbnailProvider(std::function<uint64_t(const std::string&)> provider) {
    // サムネイルの描画資源は外部が所有し、ここでは取得手段だけ保持する。
    thumbnailProvider_ = std::move(provider);
}

void PresetEditor::SetPreviewProvider(std::function<uint64_t(const std::string&, float, float, float)> provider) {
    previewProvider_ = std::move(provider);
}

void PresetEditor::SetSelectionCallback(std::function<void()> callback) {
    // 選択先を切り替えたときの Inspector 連携を呼び出し元へ通知する。
    selectionCallback_ = std::move(callback);
}

void PresetEditor::SetThumbnailInvalidator(std::function<void(const std::string&)> callback) {
    // 材質変更後のサムネイル再撮影を描画側へ依頼する。
    thumbnailInvalidator_ = std::move(callback);
}

std::string PresetEditor::GetDisplayName(const std::string& key, const json& data) const {
    // 表示名が無い旧データは元名、さらに無い場合は内部キーへ戻す。
    return ReadString(data, "presetDisplayName", ReadString(data, "name", key));
}

void PresetEditor::SelectPreset(const std::string& name, bool notify) {
    // 登録済みだけ選択し、Project から来た選択だけ Inspector へ通知する。
    if (!PresetManager::GetInstance()->HasPreset(name)) return;
    selectedName_ = name;
    selectionFromProject_ = notify;
    if (notify && selectionCallback_) selectionCallback_();
}

void PresetEditor::NotifyPresetUsed(const std::string& name) {
    // 最近使った一覧では同名を重複させず、上限を設ける。
    recentPresets_.erase(std::remove(recentPresets_.begin(), recentPresets_.end(), name), recentPresets_.end());
    recentPresets_.insert(recentPresets_.begin(), name);
    if (recentPresets_.size() > 24) recentPresets_.resize(24);
}

void PresetEditor::DrawImGui() {
    // 選択中は詳細設定を表示し、未選択時は配置ライブラリを表示する。
    HandleDeferredDelete();
    ImGui::TextColored(ImVec4(0.35f, 0.9f, 1.0f, 1.0f), "配置プリセット");
    if (selectedName_.empty()) {
        DrawLibrary();
    } else {
        if (ImGui::Button("下部の一覧を広げる")) { requestLibraryDockExpand_ = true; if (selectionCallback_) selectionCallback_(); }
        ImGui::SameLine();
        if (ImGui::Button("選択を解除")) selectedName_.clear();
        DrawSettingsPane();
    }
}

void PresetEditor::DrawPalettePane() {
    // パレットタブからも同じライブラリを開く。
    DrawLibrary();
}

void PresetEditor::DrawLibrary(bool fromProject) {
    // ステージ・用途・検索結果を絞ってから必要な行だけ描画する。
    HandleDeferredDelete();
    ImGui::PushID(fromProject ? "ProjectPresetLibrary" : "InspectorPresetLibrary");
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth((std::max)(110.0f, width - 185.0f));
    ImGui::InputTextWithHint("##PresetSearch", "名前・用途・元の名前で検索...", searchBuffer_, sizeof(searchBuffer_));
    ImGui::SameLine();
    if (ImGui::Button("下部を広げる")) { requestLibraryDockExpand_ = true; if (selectionCallback_) selectionCallback_(); }
    ImGui::SameLine();
    if (ImGui::Button("管理")) ImGui::OpenPopup("LibraryManagement");
    if (ImGui::BeginPopup("LibraryManagement")) {
        ImGui::PushID("CreatePreset");
        DrawCreateButtons();
        ImGui::PopID();
        if (ImGui::Button("プリセット再読込")) PresetManager::GetInstance()->Initialize();
        if (ImGui::Button("モデル一覧更新")) RefreshModelList();
        ImGui::EndPopup();
    }
    const ImVec4 dangerColor(0.90f, 0.27f, 0.30f, 1.0f);
    bool openBulkDeleteConfirmation = false;
    if (!bulkDeleteMode_) {
        ImGui::PushStyleColor(ImGuiCol_Text, dangerColor);
        if (ImGui::Button(ICON_FA_TRASH_ALT " まとめて削除")) {
            bulkDeleteMode_ = true;
            bulkDeleteSelection_.clear();
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::TextColored(dangerColor, "削除モード: %d件選択", static_cast<int>(bulkDeleteSelection_.size()));
        ImGui::SameLine();
        ImGui::BeginDisabled(bulkDeleteSelection_.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.18f, 1.0f));
        if (ImGui::Button(ICON_FA_TRASH_ALT " 選択したプリセットを削除...")) openBulkDeleteConfirmation = true;
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (width < 660.0f) ImGui::NewLine(); else ImGui::SameLine();
        if (ImGui::Button("選択解除")) bulkDeleteSelection_.clear();
        ImGui::SameLine();
        if (ImGui::Button("終了")) { bulkDeleteSelection_.clear(); bulkDeleteMode_ = false; }
    }
    const char* collections[] = {"", "Tutorial", "Stage1", "Stage2", "Stage3", "Stage4", "Stage5", "StageSelect"};
    const char* labels[] = {"全ステージ", "チュートリアル", "ステージ1", "ステージ2", "ステージ3", "ステージ4", "ステージ5", "ステージ選択"};
    int stage = 0;
    for (int i = 0; i < IM_ARRAYSIZE(collections); ++i) if (stageCollection_ == collections[i]) stage = i;
    ImGui::SetNextItemWidth(width < 600.0f ? 140.0f : 170.0f);
    if (ImGui::Combo("##LibraryStage", &stage, labels, IM_ARRAYSIZE(labels))) stageCollection_ = collections[stage];
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width < 600.0f ? 110.0f : 160.0f);
    ImGui::SliderInt("サイズ", &thumbnailSize_, 96, 192, "%d px");
    if (width < 600.0f) ImGui::NewLine(); else ImGui::SameLine();
    if (ImGui::Button(listView_ ? "グリッド表示" : "リスト表示")) listView_ = !listView_;
    ImGui::Separator();
    const char* categories[] = {"すべて", "お気に入り", "最近使ったもの", "地形・足場", "装飾", "ギミック", "ゲート", "アイテム", "当たり判定", "エフェクト", "敵"};
    const bool sidebar = width >= 650.0f;
    if (sidebar) {
        ImGui::BeginChild("LibraryCategories", ImVec2(155.0f, 0.0f), true);
        for (int i = 0; i < IM_ARRAYSIZE(categories); ++i) {
            if (i == 3) ImGui::Separator();
            if (ImGui::Selectable(categories[i], libraryCategory_ == i)) libraryCategory_ = i;
        }
        ImGui::EndChild();
        ImGui::SameLine();
    } else {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("用途", &libraryCategory_, categories, IM_ARRAYSIZE(categories));
    }
    ImGui::BeginChild("LibraryGallery", ImVec2(0.0f, 0.0f), false);
    const auto& presets = PresetManager::GetInstance()->GetPresets();
    std::vector<std::string> entries;
    for (const auto& [key, data] : presets) {
        if (!stageCollection_.empty() && ReadString(data, "presetCollection", "") != stageCollection_) continue;
        if (!MatchesSearch(key, data)) continue;
        if (libraryCategory_ == 1 && !ReadBool(data, "presetFavorite", false)) continue;
        if (libraryCategory_ == 2 && std::find(recentPresets_.begin(), recentPresets_.end(), key) == recentPresets_.end()) continue;
        if (libraryCategory_ >= 3 && ReadString(data, "presetUsage", "装飾") != categories[libraryCategory_]) continue;
        entries.push_back(key);
    }
    std::stable_sort(entries.begin(), entries.end(), [&](const std::string& a, const std::string& b) {
        if (libraryCategory_ == 2) return std::find(recentPresets_.begin(), recentPresets_.end(), a) < std::find(recentPresets_.begin(), recentPresets_.end(), b);
        const json& left = presets.at(a); const json& right = presets.at(b);
        const bool l = ReadBool(left, "presetRecommended", false), r = ReadBool(right, "presetRecommended", false);
        if (l != r) return l;
        const bool lc = ReadString(left, "presetUsage", "") == "当たり判定", rc = ReadString(right, "presetUsage", "") == "当たり判定";
        if (lc != rc) return !lc;
        return GetDisplayName(a, left) < GetDisplayName(b, right);
    });
    // 絞り込みを跨いで候補を保持するが、一括選択は現在表示中の結果だけを対象にする。
    bulkDeleteSelection_.erase(std::remove_if(bulkDeleteSelection_.begin(), bulkDeleteSelection_.end(),
        [&](const std::string& key) { return presets.find(key) == presets.end(); }), bulkDeleteSelection_.end());
    ImGui::TextDisabled("表示中 %d件 / 全%d件", static_cast<int>(entries.size()), static_cast<int>(presets.size()));
    ImGui::SameLine();
    ImGui::TextDisabled(bulkDeleteMode_ ? "クリックで削除候補を選択" : "クリックで詳細 / ドラッグで配置");
    if (bulkDeleteMode_) {
        if (ImGui::Button("表示中をすべて選択")) {
            for (const std::string& key : entries) {
                if (std::find(bulkDeleteSelection_.begin(), bulkDeleteSelection_.end(), key) == bulkDeleteSelection_.end())
                    bulkDeleteSelection_.push_back(key);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("表示中を選択解除")) {
            for (const std::string& key : entries) {
                bulkDeleteSelection_.erase(std::remove(bulkDeleteSelection_.begin(), bulkDeleteSelection_.end(), key), bulkDeleteSelection_.end());
            }
        }
    }
    if (entries.empty()) ImGui::TextWrapped("条件に合うプリセットがありません。検索やステージ・用途の条件を変更してください。");
    const float tile = static_cast<float>(thumbnailSize_);
    const int columns = listView_ ? 1 : (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (tile + 18.0f)));
    const float rowHeight = tile + ImGui::GetTextLineHeightWithSpacing() * (listView_ ? 1.0f : 3.0f) + 14.0f;
    bool openLibraryDeleteConfirmation = false;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>((entries.size() + columns - 1) / columns), rowHeight);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const ImVec2 origin = ImGui::GetCursorPos();
            for (int col = 0; col < columns; ++col) {
                const size_t index = static_cast<size_t>(row * columns + col);
                if (index >= entries.size()) break;
                const std::string& key = entries[index]; const json& data = presets.at(key);
                const std::string display = GetDisplayName(key, data);
                ImGui::SetCursorPos(ImVec2(origin.x + col * (tile + 18.0f), origin.y));
                ImGui::PushID(key.c_str());
                ImGui::BeginGroup();
                const bool selected = bulkDeleteMode_
                    ? std::find(bulkDeleteSelection_.begin(), bulkDeleteSelection_.end(), key) != bulkDeleteSelection_.end()
                    : selectedName_ == key;
                const ImVec4 selectionColor = bulkDeleteMode_ ? ImVec4(0.58f,0.16f,0.19f,1.0f) : ImVec4(0.18f,0.38f,0.60f,1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, selected ? selectionColor : ImVec4(0.14f,0.15f,0.18f,1.0f));
                DrawThumbnailButton(key, GetCategoryColor(DetectCategory(data)), tile);
                ImGui::PopStyleColor();
                if (selected) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                    bulkDeleteMode_ ? IM_COL32(255,75,82,255) : IM_COL32(80,180,255,255), 2.0f, 0, 2.0f);
                if (ImGui::IsItemClicked()) {
                    if (bulkDeleteMode_) {
                        const auto it = std::find(bulkDeleteSelection_.begin(), bulkDeleteSelection_.end(), key);
                        if (it == bulkDeleteSelection_.end()) bulkDeleteSelection_.push_back(key);
                        else bulkDeleteSelection_.erase(it);
                    } else SelectPreset(key, fromProject);
                }
                if (!bulkDeleteMode_ && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && placePresetCallback_) {
                    NotifyPresetUsed(key); placePresetCallback_(key);
                }
                if (!bulkDeleteMode_ && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("PRESET_ASSET", key.c_str(), key.size() + 1);
                    ImGui::TextUnformatted(display.c_str());
                    NotifyPresetUsed(key);
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(display.c_str());
                    ImGui::TextUnformatted(ReadString(data, "presetDescription", "").c_str());
                    const engine::Vector3 scale = ReadVector3(data, "scale", {1,1,1});
                    ImGui::Text("スケール: %.2f / %.2f / %.2f", scale.x,scale.y,scale.z);
                    ImGui::TextDisabled("元の名前: %s", ReadString(data,"name",key).c_str());
                    ImGui::TextDisabled("%s", key.c_str());
                    ImGui::EndTooltip();
                }
                if (ImGui::BeginPopupContextItem("LibraryPresetMenu")) {
                    if (bulkDeleteMode_) {
                        if (ImGui::MenuItem(selected ? "削除候補から外す" : "削除候補に追加")) {
                            const auto it = std::find(bulkDeleteSelection_.begin(), bulkDeleteSelection_.end(), key);
                            if (it == bulkDeleteSelection_.end()) bulkDeleteSelection_.push_back(key);
                            else bulkDeleteSelection_.erase(it);
                        }
                    } else {
                        if (ImGui::MenuItem("詳細・表示名を編集")) SelectPreset(key, fromProject);
                        if (ImGui::MenuItem(ReadBool(data,"presetFavorite",false) ? "お気に入りから外す" : "お気に入りに追加")) {
                            PresetManager::GetInstance()->GetPreset(key)["presetFavorite"] = !ReadBool(data,"presetFavorite",false);
                            PresetManager::GetInstance()->SaveAll();
                        }
                        if (ImGui::MenuItem("配置プレビュー") && placePresetCallback_) { NotifyPresetUsed(key); placePresetCallback_(key); }
                        if (ImGui::MenuItem("ブラシ配置") && brushPresetCallback_) { NotifyPresetUsed(key); brushPresetCallback_(key); }
                        if (ImGui::MenuItem("サムネイルを再撮影") && thumbnailInvalidator_) thumbnailInvalidator_(key);
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, dangerColor);
                    if (ImGui::MenuItem(ICON_FA_TRASH_ALT " このプリセットを削除...")) {
                        pendingLibraryDeleteName_ = key;
                        openLibraryDeleteConfirmation = true;
                    }
                    ImGui::PopStyleColor();
                    ImGui::EndPopup();
                }
                if (listView_) ImGui::SameLine();
                ImGui::BeginGroup();
                const float textWidth = listView_ ? (std::max)(80.0f, ImGui::GetContentRegionAvail().x) : tile;
                // UTF-8の文字境界で省略し、内部キーの長さで一覧の高さが変わらないようにします。
                std::string caption = display;
                while (caption.size() > 3 && ImGui::CalcTextSize(caption.c_str()).x > textWidth) {
                    size_t cut = caption.size() - 1;
                    while (cut > 0 && (static_cast<unsigned char>(caption[cut]) & 0xc0) == 0x80) --cut;
                    caption.resize(cut);
                }
                ImGui::TextUnformatted(caption.c_str());
                ImGui::TextColored(GetCategoryColor(DetectCategory(data)), "%s%s", ReadBool(data,"presetFavorite",false) ? "★ " : "", ReadString(data,"presetUsage",GetCategoryLabel(DetectCategory(data))).c_str());
                if (listView_) ImGui::TextWrapped("%s", ReadString(data,"presetDescription","").c_str());
                ImGui::EndGroup();
                ImGui::EndGroup();
                ImGui::PopID();
            }
            ImGui::SetCursorPos(ImVec2(origin.x, origin.y + rowHeight - 1.0f));
            ImGui::Dummy(ImVec2(0.0f, 1.0f));
        }
    }
    ImGui::EndChild();
    if (openBulkDeleteConfirmation) ImGui::OpenPopup("選択したプリセットをまとめて削除");
    if (ImGui::BeginPopupModal("選択したプリセットをまとめて削除", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(dangerColor, "次の%d件を登録一覧から削除します。", static_cast<int>(bulkDeleteSelection_.size()));
        ImGui::TextDisabled("配置済みのObjectとモデルファイルは残ります。");
        ImGui::BeginChild("削除対象一覧", ImVec2(440.0f, 200.0f), true);
        for (const std::string& key : bulkDeleteSelection_) {
            const auto it = presets.find(key);
            if (it != presets.end()) ImGui::TextUnformatted(GetDisplayName(key, it->second).c_str());
        }
        ImGui::EndChild();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.18f, 1.0f));
        if (ImGui::Button(ICON_FA_TRASH_ALT " まとめて削除する")) {
            const bool selectedPresetRemoved = std::find(bulkDeleteSelection_.begin(), bulkDeleteSelection_.end(), selectedName_) != bulkDeleteSelection_.end();
            PresetManager::GetInstance()->RemovePresets(bulkDeleteSelection_);
            if (selectedPresetRemoved) selectedName_.clear();
            for (const std::string& key : bulkDeleteSelection_) {
                recentPresets_.erase(std::remove(recentPresets_.begin(), recentPresets_.end(), key), recentPresets_.end());
            }
            bulkDeleteSelection_.clear();
            bulkDeleteMode_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // 反復中にプリセット集合を変更すると一覧の参照が無効になるため、削除は一覧の後で確定する。
    if (openLibraryDeleteConfirmation) ImGui::OpenPopup("一覧からプリセットを削除");
    if (ImGui::BeginPopupModal("一覧からプリセットを削除", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("「%s」を登録一覧から削除します。", pendingLibraryDeleteName_.c_str());
        ImGui::TextDisabled("配置済みのObjectとモデルファイルは残ります。");
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.15f, 0.18f, 1.0f));
        if (ImGui::Button(ICON_FA_TRASH_ALT " 削除する")) {
            PresetManager::GetInstance()->RemovePreset(pendingLibraryDeleteName_);
            if (selectedName_ == pendingLibraryDeleteName_) selectedName_.clear();
            recentPresets_.erase(std::remove(recentPresets_.begin(), recentPresets_.end(), pendingLibraryDeleteName_), recentPresets_.end());
            pendingLibraryDeleteName_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("キャンセル")) {
            pendingLibraryDeleteName_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void PresetEditor::DrawCategoryButton(const char* label, Category category) {
    // 選択中の用途だけ強調し、クリック後の一覧フィルターへ反映する。
    bool selected = activeCategory_ == category;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.38f, 0.72f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.44f, 0.82f, 1.0f));
    }

    if (ImGui::Button(label)) {
        activeCategory_ = category;
    }

    if (selected) {
        ImGui::PopStyleColor(2);
    }
}

void PresetEditor::DrawCreateButtons() {
    // 用途ごとに空のテンプレートを作り、後から詳細を編集できるようにする。
    ImGui::TextDisabled("新規テンプレート");
    ImGui::PushItemWidth(-1.0f);
    ImGui::InputText("##NewPresetName", newName_, sizeof(newName_));
    ImGui::PopItemWidth();

    if (ImGui::Button("敵", ImVec2(72.0f, 0.0f))) AddBlankPreset(Category::Enemy);
    ImGui::SameLine();
    if (ImGui::Button("ギミック", ImVec2(92.0f, 0.0f))) AddBlankPreset(Category::Gimmick);
    ImGui::SameLine();
    if (ImGui::Button("アイテム", ImVec2(92.0f, 0.0f))) AddBlankPreset(Category::Item);
    ImGui::SameLine();
    if (ImGui::Button("モデル", ImVec2(72.0f, 0.0f))) AddBlankPreset(Category::Model);
}

void PresetEditor::DrawPresetList() {
    const auto& presets = PresetManager::GetInstance()->GetPresets();
    ImVec2 listSize = ImGui::GetContentRegionAvail();

    if (!ImGui::BeginChild("PresetAssetList", listSize, true)) {
        ImGui::EndChild();
        return;
    }

    // 一覧の並べ替え用に元の名前と JSON を借用する。元コンテナーの要素が有効な間だけ使う。
    struct PresetListEntry {
        const std::string* name = nullptr;
        const json* data = nullptr;
    };
    std::vector<PresetListEntry> entries;
    entries.reserve(presets.size());
    for (const auto& [name, data] : presets) {
        if (!MatchesCategory(data, activeCategory_) || !MatchesSearch(name, data)) {
            continue;
        }
        entries.push_back({ &name, &data });
    }

    std::stable_sort(entries.begin(), entries.end(), [this](const PresetListEntry& lhs, const PresetListEntry& rhs) {
        const bool lhsStage1 = IsStageLibraryPreset(*lhs.data);
        const bool rhsStage1 = IsStageLibraryPreset(*rhs.data);
        if (lhsStage1 != rhsStage1) {
            return lhsStage1;
        }

        const int lhsSection = ReadInt(*lhs.data, "presetSectionOrder", 10000);
        const int rhsSection = ReadInt(*rhs.data, "presetSectionOrder", 10000);
        if (lhsSection != rhsSection) {
            return lhsSection < rhsSection;
        }

        const int lhsOrder = ReadInt(*lhs.data, "presetOrder", 10000);
        const int rhsOrder = ReadInt(*rhs.data, "presetOrder", 10000);
        if (lhsOrder != rhsOrder) {
            return lhsOrder < rhsOrder;
        }
        return *lhs.name < *rhs.name;
    });

    ImGui::TextDisabled("表示中: %d / 登録: %d", static_cast<int>(entries.size()), static_cast<int>(presets.size()));
    std::string currentSection;
    for (const PresetListEntry& entry : entries) {
        if (activeCategory_ == Category::Stage1) {
            const std::string section = ReadString(*entry.data, "presetSection", "その他");
            if (section != currentSection) {
                if (!currentSection.empty()) {
                    ImGui::Spacing();
                }
                currentSection = section;
                const ImVec4 sectionColor = GetStage1SectionColor(ReadInt(*entry.data, "presetSectionOrder", 0));
                ImGui::TextColored(sectionColor, "%s", currentSection.c_str());
                ImGui::Separator();
            }
        }
        DrawPresetListItem(*entry.name, *entry.data);
    }

    ImGui::EndChild();
}

void PresetEditor::DrawPresetListItem(const std::string& name, const json& data) {
    // 一覧行にサムネイルと用途を置き、ドラッグでシーン配置へ渡す。
    ImGui::PushID(name.c_str());

    bool selected = selectedName_ == name;
    Category category = DetectCategory(data);
    ImVec4 accent = GetCategoryColor(category);
    const bool stage1Preset = IsStageLibraryPreset(data);
    const float rowHeight = stage1Preset ? 128.0f : 96.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, selected ? ImVec4(0.18f, 0.25f, 0.38f, 1.0f) : ImVec4(0.11f, 0.12f, 0.14f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(0.70f, 0.86f, 1.0f, 1.0f) : ImVec4(0.25f, 0.26f, 0.30f, 1.0f));

    ImGui::BeginChild("PresetRow", ImVec2(0.0f, rowHeight), true);
    DrawThumbnailButton(name, accent);

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("PRESET_ASSET", name.c_str(), name.size() + 1);
        ImGui::Text("配置: %s", name.c_str());
        ImGui::TextDisabled("%s", ReadString(data, "modelName", "(なし)").c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::IsItemClicked()) {
        selectedName_ = name;
    }

    ImGui::SameLine();
    ImGui::BeginGroup();
    std::string displayName = GetDisplayName(name, data);
    if (ImGui::Selectable(displayName.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 22.0f))) {
        selectedName_ = name;
        if (ImGui::IsMouseDoubleClicked(0) && placePresetCallback_) {
            placePresetCallback_(selectedName_);
        }
    }
    ImGui::TextColored(accent, "%s", GetCategoryLabel(category));
    if (stage1Preset && ReadBool(data, "presetRecommended", false)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.76f, 0.24f, 1.0f), "よく使う");
    }
    ImGui::TextDisabled("Model: %s", ShortModelName(ReadString(data, "modelName", "(なし)")).c_str());
    if (stage1Preset) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextDisabled("%s", ReadString(data, "presetDescription", "").c_str());
        ImGui::PopTextWrapPos();
    } else {
        engine::Vector3 scale = ReadVector3(data, "scale", { 1.0f, 1.0f, 1.0f });
        ImGui::TextDisabled("Scale: %.2f / %.2f / %.2f", scale.x, scale.y, scale.z);
    }
    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

void PresetEditor::DrawThumbnailButton(const std::string& name, const ImVec4& accent, float thumbnailSize) {
    const ImVec2 size(thumbnailSize, thumbnailSize);
    const auto& data = PresetManager::GetInstance()->GetPreset(name);
    const bool colliderOnly = ReadString(data, "presetUsage", "") == "当たり判定";
    const uint64_t gpuPtr = !colliderOnly && ImGui::IsRectVisible(size) && thumbnailProvider_ ? thumbnailProvider_(name) : 0;
    if (gpuPtr) {
        ImGui::ImageButton("##Thumbnail", ImTextureRef(static_cast<ImTextureID>(gpuPtr)), size);
    } else {
        ImGui::Button(colliderOnly ? "##ColliderThumbnail" : "3D##Thumbnail", size);
        if (colliderOnly) {
            // 判定専用Objectに空白画像を使わず、箱のワイヤーを識別記号として描画します。
            const ImVec2 min = ImGui::GetItemRectMin();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float t = thumbnailSize;
            ImVec2 v[8] = {{min.x+t*.22f,min.y+t*.30f},{min.x+t*.63f,min.y+t*.30f},{min.x+t*.63f,min.y+t*.72f},{min.x+t*.22f,min.y+t*.72f},
                           {min.x+t*.37f,min.y+t*.18f},{min.x+t*.78f,min.y+t*.18f},{min.x+t*.78f,min.y+t*.60f},{min.x+t*.37f,min.y+t*.60f}};
            const int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for (const auto& e : edges) draw->AddLine(v[e[0]],v[e[1]],IM_COL32(170,210,245,255),1.5f);
            draw->AddText(ImVec2(min.x+8,min.y+t-20),IM_COL32(190,215,235,255),"判定のみ");
        }
    }
}

void PresetEditor::DrawSettingsPane() {
    PresetManager* manager = PresetManager::GetInstance();
    if (selectedName_.empty() || !manager->HasPreset(selectedName_)) {
        ImGui::TextDisabled("左の作成パレットから編集したいプリセットを選択してください。");
        return;
    }

    json& data = manager->GetPreset(selectedName_);
    Category category = DetectCategory(data);
    bool changed = false;
    if (category == Category::Enemy) {
        changed |= RemoveManagedEnemyFields(data);
    }

    std::string displayName = GetDisplayName(selectedName_, data);
    ImGui::TextColored(GetCategoryColor(category), "%s", displayName.c_str());
    if (previewSelectionName_ != selectedName_) {
        previewSelectionName_ = selectedName_;
        previewYaw_ = 0.0f;
        previewPitch_ = 0.0f;
        previewZoom_ = 1.0f;
    }
    const float previewSize = (std::min)(300.0f, ImGui::GetContentRegionAvail().x - 12.0f);
    if (previewSize > 48.0f) {
        const bool colliderOnly = ReadString(data, "presetUsage", "") == "当たり判定";
        const uint64_t texture = !colliderOnly && previewProvider_
            ? previewProvider_(selectedName_, previewYaw_, previewPitch_, previewZoom_) : 0;
        if (texture) {
            ImGui::ImageButton("##Preset3dPreview", ImTextureRef(static_cast<ImTextureID>(texture)),
                ImVec2(previewSize, previewSize));
            // 画像上の操作は視点だけに適用し、配置済みObjectとJSONの回転を変えない。
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                previewYaw_ = std::remainder(previewYaw_ + delta.x * 0.01f, 6.2831853f);
                previewPitch_ = std::clamp(previewPitch_ + delta.y * 0.01f, -1.2f, 1.2f);
            }
            if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
                previewZoom_ = std::clamp(previewZoom_ + ImGui::GetIO().MouseWheel * 0.1f, 0.5f, 2.5f);
            }
        } else {
            ImGui::Button(colliderOnly ? "判定のみ##Preset3dPreview" : "モデルを読み込み中##Preset3dPreview",
                ImVec2(previewSize, previewSize));
        }
        if (!colliderOnly) {
            ImGui::TextDisabled("左ドラッグ: 視点を回す / ホイール: 拡大縮小");
            ImGui::SliderAngle("水平角##PresetPreview", &previewYaw_, -180.0f, 180.0f);
            ImGui::SliderAngle("上下角##PresetPreview", &previewPitch_, -68.0f, 68.0f);
            ImGui::SliderFloat("拡大率##PresetPreview", &previewZoom_, 0.5f, 2.5f, "%.1f 倍");
            if (ImGui::SmallButton("視点を初期位置へ")) {
                previewYaw_ = 0.0f;
                previewPitch_ = 0.0f;
                previewZoom_ = 1.0f;
            }
            ImGui::TextDisabled("配置時の向きは下の「回転」で編集・保存します。");
        }
    }
    ImGui::TextDisabled("%s", ReadString(data, "presetUsage", GetCategoryLabel(category)).c_str());
    bool favorite = ReadBool(data, "presetFavorite", false);
    if (ImGui::Checkbox("お気に入り", &favorite)) { data["presetFavorite"] = favorite; manager->SaveAll(); }
    if (ImGui::CollapsingHeader("用途・元データ")) {
        ImGui::TextWrapped("%s", ReadString(data,"presetDescription","").c_str());
        ImGui::TextWrapped("内部名: %s", selectedName_.c_str());
        ImGui::TextWrapped("元の名前: %s", ReadString(data,"name","").c_str());
        ImGui::TextWrapped("元の配置: %s", ReadString(data,"presetSourceObject","").c_str());
    }
    if (placePresetCallback_ && ImGui::Button("配置プレビュー")) {
        NotifyPresetUsed(selectedName_);
        placePresetCallback_(selectedName_);
    }
    ImGui::SameLine();
    if (brushPresetCallback_ && ImGui::Button("ブラシ配置")) {
        NotifyPresetUsed(selectedName_);
        brushPresetCallback_(selectedName_);
    }
    if (ImGui::Button("保存")) {
        manager->SaveAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("このプリセットを削除...")) ImGui::OpenPopup("プリセット削除の確認");
    if (ImGui::BeginPopupModal("プリセット削除の確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("「%s」を登録一覧から削除します。", displayName.c_str());
        ImGui::TextDisabled("配置済みのObjectとモデルファイルは残ります。");
        if (ImGui::Button("削除する")) {
            requestDelete_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();

    changed |= DrawStringField(data, "表示名", "presetDisplayName", displayName);
    const char* usageLabels[] = {"地形・足場", "装飾", "ギミック", "ゲート", "アイテム", "当たり判定", "エフェクト", "敵"};
    const std::string usage = ReadString(data, "presetUsage", "装飾");
    if (ImGui::BeginCombo("用途", usage.c_str())) {
        for (const char* label : usageLabels) if (ImGui::Selectable(label, usage == label)) { data["presetUsage"] = label; changed = true; }
        ImGui::EndCombo();
    }
    // 保存済みのPBR値をInspectorから見直せるよう、親と子で同じ入力欄を使う。
    const auto drawMaterialFields = [&](json& material) {
        bool fieldChanged = false;
        fieldChanged |= DrawColorField(material, "色", "color", {1.0f, 1.0f, 1.0f, 1.0f});
        fieldChanged |= DrawIntField(material, "マテリアルタイプ", "materialType", 0);
        fieldChanged |= DrawFloatField(material, "金属度", "metallic", 0.0f, 0.01f);
        fieldChanged |= DrawFloatField(material, "粗さ", "roughness", 0.5f, 0.01f);
        fieldChanged |= DrawFloatField(material, "発光", "emissive", 1.0f, 0.05f);
        const auto drawBool = [&](const char* label, const char* key, bool fallback) {
            bool value = ReadBool(material, key, fallback);
            if (ImGui::Checkbox(label, &value)) { material[key] = value; return true; }
            return false;
        };
        fieldChanged |= drawBool("ライティング", "enableLighting", true);
        fieldChanged |= drawBool("環境マップ", "enableEnvMap", false);
        fieldChanged |= DrawFloatField(material, "環境マップ強度", "envIntensity", 1.0f, 0.01f);
        fieldChanged |= DrawStringField(material, "カラーテクスチャ", "texturePath", "");
        fieldChanged |= drawBool("法線マップを使う", "enableNormalMap", false);
        fieldChanged |= DrawStringField(material, "法線マップ", "normalMapPath", "");
        fieldChanged |= DrawStringField(material, "ORMマップ", "ormMapPath", "");
        fieldChanged |= DrawStringField(material, "マテリアルアセット", "materialInstance", "");
        if (!ReadString(material, "materialInstance", "").empty()) {
            ImGui::TextDisabled("アセット参照がある場合、配置時はアセットの値が優先されます。");
        }
        return fieldChanged;
    };
    if (ImGui::CollapsingHeader("材質・モデル")) {
        if (category == Category::Enemy) {
            ImGui::TextDisabled("敵のモデルとタイプ共通スケールはステータス管理で設定します。");
        } else {
            changed |= DrawModelField(data);
        }
        changed |= drawMaterialFields(data);
    }

    const auto drawRotationDegrees = [&](json& node, const char* label) {
        constexpr float toDegrees = 57.2957795f;
        constexpr float toRadians = 0.0174532925f;
        const engine::Vector3 radians = ReadVector3(node, "rotate", {0.0f, 0.0f, 0.0f});
        float degrees[3] = {radians.x * toDegrees, radians.y * toDegrees, radians.z * toDegrees};
        if (!ImGui::DragFloat3(label, degrees, 1.0f, 0.0f, 0.0f, "%.1f°")) return false;
        // JSONと実行時のTransformはラジアンを使うため、表示単位だけ度へ変換する。
        node["rotate"] = {degrees[0] * toRadians, degrees[1] * toRadians, degrees[2] * toRadians};
        node.erase("quaternion");
        return true;
    };

    if (ImGui::CollapsingHeader("配置時Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= DrawVector3Field(data, "位置オフセット", "translate", { 0.0f, 0.0f, 0.0f }, 0.1f);
        changed |= drawRotationDegrees(data, "回転 (度)");
        if (category == Category::Enemy) {
            ImGui::TextDisabled("スケールはステータス管理の敵タイプ共通値を使用します。");
        } else {
            changed |= DrawVector3Field(data, "スケール", "scale", { 1.0f, 1.0f, 1.0f }, 0.05f);
        }
    }

    if (category == Category::Enemy) {
        changed |= DrawEnemySettings(data);
    }
    else if (category == Category::Gimmick) {
        changed |= DrawGimmickSettings(data);
    }
    else if (category == Category::Item) {
        changed |= DrawItemSettings(data);
    }

    if (ImGui::CollapsingHeader("当たり判定")) {
        changed |= DrawCollider(data);
        changed |= DrawIntField(data, "自分の属性", "collisionAttribute", 0);
        changed |= DrawIntField(data, "衝突対象", "collisionMask", 0);
    }

    if (ImGui::CollapsingHeader("イベント連携")) {
        changed |= DrawIntField(data, "自分ID", "myEventID", -1);
        changed |= DrawIntField(data, "送信先ID", "targetID", -1);
        changed |= DrawIntField(data, "イベント種別", "eventType", 0);
    }

    if (data.contains("children") && data["children"].is_array()) {
        ImGui::Separator();
        ImGui::TextDisabled("組み合わせ: 親を配置すると、以下の子も同時に配置されます。");
        // ゲートの枠・ポータルや物理床を、配置前から個別に調整できます。
        std::function<void(json&, int)> drawChildren = [&](json& node, int depth) {
            if (depth > 16 || !node.contains("children") || !node["children"].is_array()) return;
            for (size_t index = 0; index < node["children"].size(); ++index) {
                json& child = node["children"][index];
                if (!child.is_object()) continue;
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::TreeNode(GetDisplayName("子オブジェクト", child).c_str())) {
                    changed |= DrawModelField(child);
                    changed |= DrawVector3Field(child, "相対位置", "translate", {}, 0.05f);
                    changed |= drawRotationDegrees(child, "相対回転 (度)");
                    changed |= DrawVector3Field(child, "相対スケール", "scale", {1.0f, 1.0f, 1.0f}, 0.02f);
                    changed |= drawMaterialFields(child);
                    changed |= DrawCollider(child);
                    if (DetectCategory(child) == Category::Gimmick) changed |= DrawGimmickSettings(child);
                    drawChildren(child, depth + 1);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        };
        drawChildren(data, 0);
    }

    if (changed) {
        ApplyHiddenCategoryFields(data, category);
        manager->SaveAll();
        if (thumbnailInvalidator_) thumbnailInvalidator_(selectedName_);
    }
}

#else

void PresetEditor::Initialize() {}

void PresetEditor::SetPlacePresetCallback(std::function<void(const std::string&)>) {}

void PresetEditor::SetBrushPresetCallback(std::function<void(const std::string&)>) {}

void PresetEditor::SetThumbnailProvider(std::function<uint64_t(const std::string&)>) {}
void PresetEditor::SetPreviewProvider(std::function<uint64_t(const std::string&, float, float, float)>) {}

void PresetEditor::DrawImGui() {}
void PresetEditor::DrawLibrary(bool) {}
void PresetEditor::SetSelectionCallback(std::function<void()>) {}
void PresetEditor::SetThumbnailInvalidator(std::function<void(const std::string&)>) {}
void PresetEditor::SelectPreset(const std::string&, bool) {}
void PresetEditor::NotifyPresetUsed(const std::string&) {}
std::string PresetEditor::GetDisplayName(const std::string& key, const json& data) const { return data.value("presetDisplayName", data.value("name", key)); }

#endif
