#define NOMINMAX
#include "EditorToolSettings.h"
#ifdef USE_IMGUI
#include "EditorCommandRegistry.h"
#include "InputManager.h"
#include "KeyConfig.h"
#include "imgui.h"
#include "json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace {
constexpr const char* kPath = "Saved/Editor/editor_tools.json";
const std::vector<int>& Keys() {
    static const std::vector<int> keys = [] {
        std::vector<int> result{0};
        for (int key = 2; key <= 53; ++key) {
            if (key != 14 && key != 15 && key != 28 && key != 29 && key != 42) result.push_back(key);
        }
        for (int key = 59; key <= 68; ++key) result.push_back(key);
        result.push_back(87); result.push_back(88);
        return result;
    }();
    return keys;
}
}

EditorToolSettings& EditorToolSettings::Get() { static EditorToolSettings value; return value; }
void EditorToolSettings::Clear() {
    RestoreHidden();
    EditorCommandRegistry::GetInstance()->UnregisterOwner(this);
    tools_.clear(); visibility_.clear(); ownedVisibility_.clear(); open_ = false; status_.clear(); saved_.clear();
}
void EditorToolSettings::AddTool(const std::string& id, const std::string& name,
    const std::string& category, std::function<void()> open, std::function<bool()> available) {
    tools_.push_back({id, name, category});
    EditorCommandRegistry::GetInstance()->Register({id, name, category, "",
        "エディターを開く", {name, category}, std::move(available), std::move(open), this});
}
void EditorToolSettings::AddVisibility(const std::string& id, const std::string& name, bool* value) {
    if (value) visibility_.push_back({id, name, value});
}
void EditorToolSettings::AddVisibility(const std::string& id, const std::string& name) {
    AddVisibility(id, name, &ownedVisibility_.try_emplace(id, true).first->second);
}
bool EditorToolSettings::IsVisible(const std::string& id) const {
    if (hidden_) return false;
    for (const auto& value : visibility_) if (value.id == id) return *value.value;
    return true;
}
std::string EditorToolSettings::Label(const Tool& tool) const {
    if (!tool.key) return "未設定";
    return std::string(tool.ctrl ? "Ctrl+" : "") + (tool.shift ? "Shift+" : "") +
        (tool.alt ? "Alt+" : "") + KeyConfig::GetInstance()->GetKeyName(tool.key);
}
std::string EditorToolSettings::Conflict(const Tool& tool) const {
    if (!tool.key) return {};
    // 修飾キーを確認しない既存の編集操作とは、修飾付きでも競合させません。
    if (tool.key == DIK_E || tool.key == DIK_F || tool.key == DIK_G || tool.key == DIK_R ||
        tool.key == DIK_S || tool.key == DIK_T || tool.key == DIK_F10 ||
        (tool.ctrl && (tool.key == DIK_C || tool.key == DIK_D || tool.key == DIK_Z ||
        tool.key == DIK_Y || tool.key == DIK_P || tool.key == DIK_K ||
        (tool.key >= DIK_1 && tool.key <= DIK_9))) || (tool.alt && tool.key == DIK_F4))
        return "既存操作の予約キー（割当不可）";
    for (const Tool& other : tools_) {
        if (other.id != tool.id && other.key == tool.key && other.ctrl == tool.ctrl &&
            other.shift == tool.shift && other.alt == tool.alt) return "重複: " + other.name;
    }
    return {};
}
void EditorToolSettings::RefreshCommand(const Tool& tool) {
    auto* registry = EditorCommandRegistry::GetInstance();
    if (const auto* existing = registry->Find(tool.id)) {
        auto command = *existing;
        command.shortcut = tool.key && Conflict(tool).empty() ? Label(tool) : "";
        registry->Register(std::move(command));
    }
}
std::string EditorToolSettings::Snapshot() const {
    nlohmann::json data;
    data["version"] = 1;
    for (const auto& tool : tools_) data["shortcuts"][tool.id] = {
        {"key",tool.key}, {"ctrl",tool.ctrl}, {"shift",tool.shift}, {"alt",tool.alt}};
    for (const auto& value : visibility_) data["visibility"][value.id] = hidden_ ? value.beforeHide : *value.value;
    return data.dump(2);
}
void EditorToolSettings::Load() {
    try {
        std::ifstream file(kPath);
        if (!file) { saved_ = Snapshot(); status_ = "保存設定がないため、現在の設定を使用します。"; return; }
        const auto data = nlohmann::json::parse(file);
        if (data.at("version").get<int>() != 1) throw std::runtime_error("unsupported version");
        auto next = tools_;
        std::vector<bool> flags;
        for (auto& tool : next) {
            if (!data.contains("shortcuts") || !data["shortcuts"].contains(tool.id)) continue;
            const auto& bind = data["shortcuts"][tool.id];
            tool.key = bind.at("key").get<int>();
            if (std::find(Keys().begin(), Keys().end(), tool.key) == Keys().end()) throw std::runtime_error("invalid key");
            tool.ctrl = bind.at("ctrl").get<bool>(); tool.shift = bind.at("shift").get<bool>(); tool.alt = bind.at("alt").get<bool>();
        }
        for (const auto& value : visibility_) flags.push_back(data.contains("visibility") && data["visibility"].contains(value.id)
            ? data["visibility"][value.id].get<bool>() : (hidden_ ? value.beforeHide : *value.value));
        // すべて検証した後にまとめて反映し、不正JSONで半端な状態を残しません。
        RestoreHidden(); tools_ = std::move(next);
        for (size_t i = 0; i < flags.size(); ++i) *visibility_[i].value = flags[i];
        for (const auto& tool : tools_) RefreshCommand(tool);
        saved_ = Snapshot(); status_ = "設定を読み込みました。競合がある割当は発動しません。";
    } catch (const std::exception& e) { status_ = "読込失敗（現在の設定を維持）: " + std::string(e.what()); }
}
void EditorToolSettings::Save() {
    for (const auto& tool : tools_) if (!Conflict(tool).empty()) {
        status_ = "保存できません。予約キー・重複のある行を修正してください。"; return;
    }
    try {
        std::filesystem::create_directories(std::filesystem::path(kPath).parent_path());
        const auto contents = Snapshot();
        const std::string temporary = std::string(kPath) + ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << contents; file.close();
        if (!file || !MoveFileExW(std::filesystem::path(temporary).c_str(), std::filesystem::path(kPath).c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("write/replace failed");
        saved_ = contents; status_ = "保存しました。次回起動時にも適用されます。";
    } catch (const std::exception& e) { status_ = "保存失敗: " + std::string(e.what()); }
}
void EditorToolSettings::RestoreHidden() {
    if (!hidden_) return;
    for (auto& value : visibility_) *value.value = value.beforeHide;
    hidden_ = false;
}
void EditorToolSettings::SetAllVisible(bool visible) {
    RestoreHidden();
    for (auto& value : visibility_) *value.value = visible;
}
void EditorToolSettings::HandleShortcuts(bool editing) {
    const auto& io = ImGui::GetIO();
    if (!editing || open_ || io.WantTextInput || io.WantCaptureKeyboard || ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
        ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1) || ImGui::IsMouseDown(2)) return;
    for (const auto& tool : tools_) {
        if (tool.key && io.KeyCtrl == tool.ctrl && io.KeyShift == tool.shift && io.KeyAlt == tool.alt &&
            InputManager::GetInstance()->IsKeyTriggered(static_cast<BYTE>(tool.key)) && Conflict(tool).empty()) {
            EditorCommandRegistry::GetInstance()->Execute(tool.id); break;
        }
    }
}
void EditorToolSettings::DrawToolsMenu() {
    if (!ImGui::BeginMenu("ツール")) return;
    for (const char* category : {"戦闘", "演出", "ステージ", "素材", "検証・設定"}) {
        if (!ImGui::BeginMenu(category)) continue;
        for (const auto& tool : tools_) if (tool.category == category) {
            const auto label = tool.key ? Label(tool) : "";
            if (ImGui::MenuItem(tool.name.c_str(), label.c_str(), false, EditorCommandRegistry::GetInstance()->CanExecute(tool.id)))
                EditorCommandRegistry::GetInstance()->Execute(tool.id);
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("ショートカット・補助表示の設定...")) Open();
    ImGui::EndMenu();
}
void EditorToolSettings::DrawVisibilityMenu() {
    if (!ImGui::BeginMenu("補助表示")) return;
    if (ImGui::MenuItem("すべてON")) SetAllVisible(true);
    if (ImGui::MenuItem("すべてOFF")) SetAllVisible(false);
    ImGui::Separator();
    if (ImGui::MenuItem(hidden_ ? "一時非表示を元に戻す" : "下記の補助表示を一時的にすべて隠す")) {
        if (hidden_) RestoreHidden();
        else { for (auto& value : visibility_) { value.beforeHide = *value.value; *value.value = false; } hidden_ = true; }
    }
    ImGui::TextDisabled("有効なツール・選択対象の補助表示に適用");
    ImGui::BeginDisabled(hidden_);
    for (auto& value : visibility_) ImGui::MenuItem(value.name.c_str(), nullptr, value.value);
    ImGui::EndDisabled();
    ImGui::Separator();
    if (ImGui::MenuItem("表示設定を保存")) Save();
    if (ImGui::MenuItem("設定画面を開く...")) Open();
    if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
    ImGui::EndMenu();
}
void EditorToolSettings::DrawWindow() {
    // 個別パネルで変更されても、一時非表示中は補助表示を出さないようにします。
    if (hidden_) for (auto& value : visibility_) *value.value = false;
    if (!open_) return;
    ImGui::SetNextWindowSize(ImVec2(900, 610), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("エディター設定：ショートカット・補助表示", &open_)) { ImGui::End(); return; }
    ImGui::TextWrapped("ゲーム操作とは別の設定です。ショートカットは編集中のみ有効です。文字入力・ポップアップ・マウス操作中は発動しません。");
    if (ImGui::Button("設定を保存")) Save();
    ImGui::SameLine();
    if (ImGui::Button("保存済みに戻す")) ImGui::OpenPopup("保存済み設定へ戻す確認");
    if (ImGui::BeginPopupModal("保存済み設定へ戻す確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("未保存の設定変更を破棄して読み込みますか？");
        if (ImGui::Button("戻す")) { Load(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine(); ImGui::TextUnformatted(Snapshot() == saved_ ? "保存済み" : "未保存の変更あり");
    ImGui::TextDisabled("保存先: %s", kPath);
    if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
    ImGui::InputText("対象を検索", filter_, sizeof(filter_));
    if (ImGui::BeginTabBar("EditorSettingsTabs")) {
        if (ImGui::BeginTabItem("エディターのショートカット")) {
            ImGui::TextWrapped("キーと修飾キーを選択してください。既存操作の予約キーと重複は発動・保存を禁止します。未設定で割当解除できます。");
            if (ImGui::BeginTable("Bindings", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, -4))) {
                ImGui::TableSetupColumn("対象エディター", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("キー", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                for (const char* name : {"Ctrl", "Shift", "Alt"}) ImGui::TableSetupColumn(name, ImGuiTableColumnFlags_WidthFixed, 45.0f);
                ImGui::TableSetupColumn("状態", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                ImGui::TableSetupScrollFreeze(0,1); ImGui::TableHeadersRow();
                for (auto& tool : tools_) {
                    if (filter_[0] && tool.name.find(filter_) == std::string::npos && tool.category.find(filter_) == std::string::npos) continue;
                    ImGui::PushID(tool.id.c_str()); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", tool.name.c_str()); ImGui::TextDisabled("%s", tool.category.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
                    const auto keyName = tool.key ? KeyConfig::GetInstance()->GetKeyName(tool.key) : "未設定";
                    bool changed = false;
                    if (ImGui::BeginCombo("##key", keyName.c_str())) {
                        for (int key : Keys()) {
                            const auto name = key ? KeyConfig::GetInstance()->GetKeyName(key) : "未設定";
                            if (ImGui::Selectable(name.c_str(), tool.key == key)) { tool.key = key; changed = true; }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TableSetColumnIndex(2); changed |= ImGui::Checkbox("##ctrl", &tool.ctrl);
                    ImGui::TableSetColumnIndex(3); changed |= ImGui::Checkbox("##shift", &tool.shift);
                    ImGui::TableSetColumnIndex(4); changed |= ImGui::Checkbox("##alt", &tool.alt);
                    if (changed) for (const auto& value : tools_) RefreshCommand(value);
                    ImGui::TableSetColumnIndex(5); const auto conflict = Conflict(tool);
                    if (conflict.empty()) ImGui::TextWrapped("%s", Label(tool).c_str());
                    else ImGui::TextColored(ImVec4(1,0.45f,0.25f,1), "%s", conflict.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("補助表示")) {
            if (ImGui::Button("すべてON")) SetAllVisible(true);
            ImGui::SameLine();
            if (ImGui::Button("すべてOFF")) SetAllVisible(false);
            ImGui::TextWrapped("実際のライト・攻撃・当たり判定は無効にしません。個別エディターと共通の表示設定です。");
            ImGui::BeginDisabled(hidden_);
            for (auto& value : visibility_) if (!filter_[0] || value.name.find(filter_) != std::string::npos) ImGui::Checkbox(value.name.c_str(), value.value);
            ImGui::EndDisabled();
            if (hidden_ && ImGui::Button("一時非表示を元に戻す")) RestoreHidden();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
#endif
