#include "GPUParticleEditor.h"
#include <imgui.h>
#include <fstream>
#include <json.hpp> // nlohmann/json
#include "DebugConsole.h"

using json = nlohmann::json;

void GPUParticleEditor::Initialize() {
    emitTimer_ = 0.0f;
}

void GPUParticleEditor::Update(float deltaTime) {
    GPUParticleManager::GetInstance()->SetEnvironmentParams(envGravity_, envDrag_, envWind_, envTurbulence_);
    if (isLooping_) {
        emitTimer_ += deltaTime;
        if (emitTimer_ >= emitInterval_) {
            GPUParticleManager::GetInstance()->Emit(
                emitPos_, emitArea_, emitVelocity_, emitCount_,
                emitLife_, velocityVariance_, baseColor_
            );
            emitTimer_ = 0.0f;
        }
    }
}

void GPUParticleEditor::DrawImGui() {
    ImGui::Text("--- GPU Particle Editor ---");

    if (ImGui::CollapsingHeader("Emit Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &emitPos_.x, 0.1f);
        ImGui::DragFloat3("Emit Area", &emitArea_.x, 0.1f);
        ImGui::DragFloat3("Velocity", &emitVelocity_.x, 0.1f);
        ImGui::DragInt("Emit Count", &emitCount_, 10, 1, GPUParticleManager::kMaxParticles);
        ImGui::DragFloat("Life Time", &emitLife_, 0.05f, 0.1f, 10.0f);
        ImGui::DragFloat("Velocity Variance", &velocityVariance_, 0.1f, 0.0f, 50.0f);
        ImGui::ColorEdit4("Base Color", &baseColor_.x);

    }
    if (ImGui::CollapsingHeader("Environment (Real-time)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Gravity", &envGravity_.x, 0.01f);
        ImGui::DragFloat("Air Drag", &envDrag_, 0.001f, 0.8f, 1.0f);
        ImGui::DragFloat3("Wind", &envWind_.x, 0.1f);
        ImGui::DragFloat("Turbulence (ノイズのうねり)", &envTurbulence_, 0.1f, 0.0f, 100.0f);
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Editor Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Emit Once (1回発生)", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
            GPUParticleManager::GetInstance()->Emit(
                emitPos_, emitArea_, emitVelocity_, emitCount_,
                emitLife_, velocityVariance_, baseColor_
            );
        }

        ImGui::Checkbox("Loop Emit (連続発生テスト)", &isLooping_);
        if (isLooping_) {
            ImGui::Indent();
            ImGui::DragFloat("Emit Interval", &emitInterval_, 0.01f, 0.01f, 2.0f);
            ImGui::Unindent();
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Save & Load (JSON)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Preset Name", presetName_, sizeof(presetName_));

        if (ImGui::Button("Save Preset", ImVec2(120, 0))) {
            Save(presetName_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Preset", ImVec2(120, 0))) {
            Load(presetName_);
        }
    }
}

void GPUParticleEditor::Save(const std::string& presetName) {
    json j;
    j["emitPos"] = { emitPos_.x, emitPos_.y, emitPos_.z };
    j["emitVelocity"] = { emitVelocity_.x, emitVelocity_.y, emitVelocity_.z };
    j["emitCount"] = emitCount_;
    j["emitLife"] = emitLife_;
    j["velocityVariance"] = velocityVariance_;
    j["baseColor"] = { baseColor_.x, baseColor_.y, baseColor_.z, baseColor_.w };
    j["envGravity"] = { envGravity_.x, envGravity_.y, envGravity_.z };
    j["envDrag"] = envDrag_;
    j["envWind"] = { envWind_.x, envWind_.y, envWind_.z };
    j["envTurbulence"] = envTurbulence_;
    std::string filepath = "Resources/json/gpu_particles/" + presetName + ".json";
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Saved GPU Particle Preset: " + presetName);
        }
    }
}

void GPUParticleEditor::Load(const std::string& presetName) {
    std::string filepath = "Resources/json/gpu_particles/" + presetName + ".json";
    std::ifstream file(filepath);
    if (file.is_open()) {
        json j;
        file >> j;
        file.close();

        if (j.contains("emitPos")) {
            emitPos_.x = j["emitPos"][0]; emitPos_.y = j["emitPos"][1]; emitPos_.z = j["emitPos"][2];
        }
        if (j.contains("emitVelocity")) {
            emitVelocity_.x = j["emitVelocity"][0]; emitVelocity_.y = j["emitVelocity"][1]; emitVelocity_.z = j["emitVelocity"][2];
        }
        if (j.contains("emitCount")) emitCount_ = j["emitCount"];
        if (j.contains("emitLife")) emitLife_ = j["emitLife"];
        if (j.contains("envGravity")) {
            envGravity_.x = j["envGravity"][0]; envGravity_.y = j["envGravity"][1]; envGravity_.z = j["envGravity"][2];
        }
        if (j.contains("envDrag")) envDrag_ = j["envDrag"];
        if (j.contains("envWind")) {
            envWind_.x = j["envWind"][0]; envWind_.y = j["envWind"][1]; envWind_.z = j["envWind"][2];
        }
        if (j.contains("envTurbulence")) envTurbulence_ = j["envTurbulence"];
        if (j.contains("velocityVariance")) velocityVariance_ = j["velocityVariance"];
        if (j.contains("baseColor")) {
            baseColor_.x = j["baseColor"][0]; baseColor_.y = j["baseColor"][1];
            baseColor_.z = j["baseColor"][2]; baseColor_.w = j["baseColor"][3];
        }
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Loaded GPU Particle Preset: " + presetName);
        }
    }
}