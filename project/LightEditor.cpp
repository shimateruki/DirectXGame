#include "LightEditor.h"
#include "imgui.h"
#include "json.hpp"
#include <fstream>
#include <cmath> // std::acos, std::cos
#include "DirectXCommon.h" // 必要なら

using json = nlohmann::json;

void LightEditor::Initialize() {
    lightManager_ = LightManager::GetInstance();
}

void LightEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (!lightManager_) return;

    if (ImGui::Begin("Light Editor")) {

        // --- 保存・読み込みボタン ---
        if (ImGui::Button("Save to JSON")) {
            SaveLightLayout(currentSaveFile_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load from JSON")) {
            LoadLightLayout(currentSaveFile_);
        }
        ImGui::SameLine();
        ImGui::Text("File: %s", currentSaveFile_.c_str());

        ImGui::Separator();

        // --- 点光源リスト ---
        if (ImGui::CollapsingHeader("Point Lights")) {
            if (ImGui::Button("Add PointLight")) {
                lightManager_->AddPointLight();
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear PointLights")) {
                lightManager_->GetPointLights().clear();
            }

            auto& pointLights = lightManager_->GetPointLights();
            for (int i = 0; i < pointLights.size(); ++i) {
                ImGui::PushID(i);
                if (ImGui::TreeNode("PointLight", "PointLight %d", i)) {
                    // 削除ボタン
                    if (ImGui::Button("Remove")) {
                        pointLights.erase(pointLights.begin() + i);
                        ImGui::TreePop();
                        ImGui::PopID();
                        continue; // ループを抜ける
                    }

                    ImGui::DragFloat3("Position", &pointLights[i].position.x, 0.1f);
                    ImGui::ColorEdit4("Color", &pointLights[i].color.x);
                    ImGui::DragFloat("Intensity", &pointLights[i].intensity, 0.01f, 0.0f, 10.0f);
                    ImGui::DragFloat("Radius", &pointLights[i].radius, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat("Decay", &pointLights[i].decay, 0.01f, 0.0f, 10.0f);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        // --- スポットライトリスト ---
        if (ImGui::CollapsingHeader("Spot Lights")) {
            if (ImGui::Button("Add SpotLight")) {
                auto l = lightManager_->AddSpotLight();
                // デフォルト角度設定
                if (l) {
                    l->cosAngle = std::cos(45.0f * 3.141592f / 180.0f);
                    l->cosFalloffStart = std::cos(30.0f * 3.141592f / 180.0f);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear SpotLights")) {
                lightManager_->GetSpotLights().clear();
            }

            auto& spotLights = lightManager_->GetSpotLights();
            for (int i = 0; i < spotLights.size(); ++i) {
                ImGui::PushID(i + 1000);
                if (ImGui::TreeNode("SpotLight", "SpotLight %d", i)) {
                    if (ImGui::Button("Remove")) {
                        spotLights.erase(spotLights.begin() + i);
                        ImGui::TreePop();
                        ImGui::PopID();
                        continue;
                    }

                    ImGui::DragFloat3("Position", &spotLights[i].position.x, 0.1f);
                    ImGui::DragFloat3("Direction", &spotLights[i].direction.x, 0.01f, -1.0f, 1.0f);
                    ImGui::ColorEdit4("Color", &spotLights[i].color.x);
                    ImGui::DragFloat("Intensity", &spotLights[i].intensity, 0.01f, 0.0f, 10.0f);
                    ImGui::DragFloat("Distance", &spotLights[i].distance, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat("Decay", &spotLights[i].decay, 0.01f, 0.0f, 10.0f);

                    // 角度操作 (Degree <-> Cos)
                    float currentAngleDeg = std::acos(spotLights[i].cosAngle) * 180.0f / 3.141592f;
                    float currentFalloffDeg = std::acos(spotLights[i].cosFalloffStart) * 180.0f / 3.141592f;

                    bool changed = false;
                    if (ImGui::DragFloat("Angle (Deg)", &currentAngleDeg, 1.0f, 0.1f, 179.0f)) changed = true;
                    if (ImGui::DragFloat("Falloff (Deg)", &currentFalloffDeg, 1.0f, 0.1f, 179.0f)) changed = true;

                    if (changed) {
                        if (currentFalloffDeg > currentAngleDeg) currentFalloffDeg = currentAngleDeg;
                        spotLights[i].cosAngle = std::cos(currentAngleDeg * 3.141592f / 180.0f);
                        spotLights[i].cosFalloffStart = std::cos(currentFalloffDeg * 3.141592f / 180.0f);
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
#endif
}

void LightEditor::SaveLightLayout(const std::string& filename) {
    json root;

    // --- 点光源 ---
    json pArray = json::array();
    for (const auto& l : lightManager_->GetPointLights()) {
        json j;
        j["position"] = { l.position.x, l.position.y, l.position.z };
        j["color"] = { l.color.x, l.color.y, l.color.z, l.color.w };
        j["intensity"] = l.intensity;
        j["radius"] = l.radius;
        j["decay"] = l.decay;
        pArray.push_back(j);
    }
    root["pointLights"] = pArray;

    // --- スポットライト ---
    json sArray = json::array();
    for (const auto& l : lightManager_->GetSpotLights()) {
        json j;
        j["position"] = { l.position.x, l.position.y, l.position.z };
        j["direction"] = { l.direction.x, l.direction.y, l.direction.z };
        j["color"] = { l.color.x, l.color.y, l.color.z, l.color.w };
        j["intensity"] = l.intensity;
        j["distance"] = l.distance;
        j["decay"] = l.decay;
        j["cosAngle"] = l.cosAngle;
        j["cosFalloffStart"] = l.cosFalloffStart;
        sArray.push_back(j);
    }
    root["spotLights"] = sArray;

    // 書き出し
    std::ofstream file(filename);
    if (file.is_open()) {
        file << root.dump(4);
        file.close();
    }
}

void LightEditor::LoadLightLayout(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;

    json root;
    try {
        file >> root;
    }
    catch (...) {
        return;
    }

    // 既存のライトをリセット
    lightManager_->ClearAllLights();

    // --- 点光源読み込み ---
    if (root.contains("pointLights") && root["pointLights"].is_array()) {
        for (const auto& j : root["pointLights"]) {
            auto l = lightManager_->AddPointLight();
            if (l) {
                if (j.contains("position")) { l->position.x = j["position"][0]; l->position.y = j["position"][1]; l->position.z = j["position"][2]; }
                if (j.contains("color")) { l->color.x = j["color"][0]; l->color.y = j["color"][1]; l->color.z = j["color"][2]; l->color.w = j["color"][3]; }
                if (j.contains("intensity")) l->intensity = j["intensity"];
                if (j.contains("radius")) l->radius = j["radius"];
                if (j.contains("decay")) l->decay = j["decay"];
            }
        }
    }

    // --- スポットライト読み込み ---
    if (root.contains("spotLights") && root["spotLights"].is_array()) {
        for (const auto& j : root["spotLights"]) {
            auto l = lightManager_->AddSpotLight();
            if (l) {
                if (j.contains("position")) { l->position.x = j["position"][0]; l->position.y = j["position"][1]; l->position.z = j["position"][2]; }
                if (j.contains("direction")) { l->direction.x = j["direction"][0]; l->direction.y = j["direction"][1]; l->direction.z = j["direction"][2]; }
                if (j.contains("color")) { l->color.x = j["color"][0]; l->color.y = j["color"][1]; l->color.z = j["color"][2]; l->color.w = j["color"][3]; }
                if (j.contains("intensity")) l->intensity = j["intensity"];
                if (j.contains("distance")) l->distance = j["distance"];
                if (j.contains("decay")) l->decay = j["decay"];
                if (j.contains("cosAngle")) l->cosAngle = j["cosAngle"];
                if (j.contains("cosFalloffStart")) l->cosFalloffStart = j["cosFalloffStart"];
            }
        }
    }
}