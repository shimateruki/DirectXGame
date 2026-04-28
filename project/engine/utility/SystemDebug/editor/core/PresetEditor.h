#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include "PresetManager.h"
#include "imgui.h"
#include "IEditable.h"

class PresetEditor : public IEditable {
public:
    static PresetEditor* GetInstance() {
        static PresetEditor instance;
        return &instance;
    }

    inline void Initialize() {
        PresetManager::GetInstance()->Initialize();
    }

    inline void DrawImGui() override {
        // 1. 削除リクエストの処理（フレームの冒頭で行う）
        if (requestDelete_ && !selectedName_.empty()) {
            PresetManager::GetInstance()->RemovePreset(selectedName_);
            selectedName_ = "";
            requestDelete_ = false;
        }

        // 2. メイン描画
        if (ImGui::BeginTabBar("PresetTabs")) {
            if (ImGui::BeginTabItem("Enemies")) {
                DrawPresetList(0); // 0: Enemy
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Gimmicks")) {
                DrawPresetList(1); // 1: Gimmick
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    std::string GetName() override { return "Preset Editor (Master Data)"; }

private:
    PresetEditor() : requestDelete_(false) {}
    ~PresetEditor() = default;

    inline void DrawPresetList(int tabType) {
        PresetManager* pm = PresetManager::GetInstance();
        auto& allPresets = pm->GetPresets();

        ImGui::BeginChild("List", ImVec2(0, 150), true);
        for (auto& [name, data] : allPresets) {
            int currentType = -1;
            if (data.contains("type") && data["type"].is_string()) {
                std::string t = data["type"];
                if (t == "Enemy") currentType = 0;
                else if (t == "Gimmick") currentType = 1;
            }
            if (currentType == -1 && data.contains("param")) {
                if (data["param"].contains("enemyType")) currentType = 0;
                else if (data["param"].contains("gimmickType")) currentType = 1;
            }

            if (currentType == tabType) {
                if (ImGui::Selectable(name.c_str(), selectedName_ == name)) {
                    selectedName_ = name;
                }
            }
        }
        ImGui::EndChild();

        ImGui::PushItemWidth(150);
        ImGui::InputText("##New", newName_, sizeof(newName_));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Add Blank")) {
            if (newName_[0] != '\0') {
                json d;
                d["name"] = newName_;
                if (tabType == 0) {
                    d["type"] = "Enemy";
                    d["param"]["enemyType"] = newName_;
                } else {
                    d["type"] = "Gimmick";
                    d["param"]["gimmickType"] = newName_;
                }
                pm->GetPreset(newName_) = d;
                selectedName_ = newName_;
                pm->SaveAll();
                newName_[0] = '\0';
            }
        }

        ImGui::Separator();

        if (!selectedName_.empty() && pm->HasPreset(selectedName_)) {
            DrawDetails(pm->GetPreset(selectedName_));
        } else {
            ImGui::Text("Select a preset above to edit.");
        }
    }

    inline void DrawDetails(json& d) {
        bool changed = false;
        PresetManager* pm = PresetManager::GetInstance();

        ImGui::TextColored(ImVec4(0, 1, 1, 1), "Preset: %s", selectedName_.c_str());

        // 1. Model (charバッファで安全に編集)
        char modelBuf[128] = "";
        if (d.contains("modelName") && d["modelName"].is_string()) {
            std::string m = d["modelName"];
            strncpy_s(modelBuf, m.c_str(), _TRUNCATE);
        }
        if (ImGui::InputText("Model Name", modelBuf, sizeof(modelBuf))) {
            d["modelName"] = std::string(modelBuf);
            changed = true;
        }

        // 2. Stats (Param)
        if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!d.contains("param") || !d["param"].is_object()) d["param"] = json::object();
            auto& p = d["param"];
            
            auto DragFloatJson = [&](const char* label, const char* key, float def) {
                float val = def;
                if (p.contains(key) && p[key].is_number()) val = p[key];
                if (ImGui::DragFloat(label, &val, 0.1f)) {
                    p[key] = val;
                    return true;
                }
                return false;
            };

            if (DragFloatJson("HP", "hp", 100.0f)) changed = true;
            if (DragFloatJson("Max HP", "maxHp", 100.0f)) changed = true;
            if (DragFloatJson("Speed", "speed", 1.0f)) changed = true;
            if (DragFloatJson("Gravity", "gravity", 50.0f)) changed = true;
            if (DragFloatJson("Jump Power", "jumpPower", 10.0f)) changed = true;
            if (DragFloatJson("Attack Interval", "interval", 3.0f)) changed = true;
            
            int maxCount = 5;
            if (p.contains("maxCount") && p["maxCount"].is_number()) maxCount = p["maxCount"];
            if (ImGui::DragInt("Max Count", &maxCount, 1, 1, 100)) {
                p["maxCount"] = maxCount;
                changed = true;
            }

            if (DragFloatJson("Detection Range", "detectionRange", 20.0f)) changed = true;
        }

        // 3. Collider
        if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!d.contains("collider") || !d["collider"].is_object()) d["collider"] = json::object();
            auto& c = d["collider"];
            int colType = 0;
            if (c.contains("type") && c["type"].is_number()) colType = c["type"];
            
            const char* colTypes[] = { "None", "Sphere", "AABB", "OBB" };
            if (ImGui::Combo("Type", &colType, colTypes, 4)) {
                c["type"] = colType;
                changed = true;
            }

            auto DragFloat3Json = [&](const char* label, const char* key) {
                float val[3] = { 0, 0, 0 };
                if (c.contains(key) && c[key].is_array() && c[key].size() >= 3) {
                    for(int i=0; i<3; ++i) if(c[key][i].is_number()) val[i] = c[key][i];
                }
                if (ImGui::DragFloat3(label, val, 0.1f)) {
                    c[key] = { val[0], val[1], val[2] };
                    return true;
                }
                return false;
            };

            if (DragFloat3Json("Center", "center")) changed = true;
            if (DragFloat3Json("Size", "size")) changed = true;
        }

        if (changed) {
            pm->SaveAll();
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Delete This Preset")) {
            // 安全のため、次のフレームの冒頭で削除を実行するフラグを立てる
            requestDelete_ = true;
        }
    }

    bool requestDelete_;
    std::string selectedName_;
    char newName_[64] = "";
};
