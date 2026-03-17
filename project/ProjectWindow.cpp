#include "ProjectWindow.h"
#include "DebugEditor.h"
#include "imgui.h"
#include "ModelManager.h"
#include "PresetManager.h"
#include "DebugConsole.h"
#include <filesystem>
#include <algorithm> // std::transform用

namespace fs = std::filesystem;

void ProjectWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

void ProjectWindow::Draw() {
#ifdef USE_IMGUI
    // ---------------------------------------------------------
    // ウィンドウ開始
    // ---------------------------------------------------------
    ImGui::Begin("Project (Assets)");

    // レイアウト計算用の変数（共通）
    float windowVisibleX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float itemSpacing = ImGui::GetStyle().ItemSpacing.x;

    // =================================================================================
    // 1. モデルファイル一覧 (Raw Models)
    // =================================================================================
    if (ImGui::CollapsingHeader("Models (Source)", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string baseDirectory = "Resources/3DModel";

        if (fs::exists(baseDirectory) && fs::is_directory(baseDirectory)) {
            ImGui::TextDisabled("Drag & Drop to Scene to Place");
            ImGui::Separator();

            for (const auto& entry : fs::directory_iterator(baseDirectory)) {
                std::string displayModelName = ""; // ボタン表示名
                std::string payloadName = "";      // ロード用パス/名前

                if (entry.is_directory()) {
                    std::string folderName = entry.path().filename().string();
                    for (const auto& subEntry : fs::directory_iterator(entry.path())) {
                        std::string subExt = subEntry.path().extension().string();
                        std::transform(subExt.begin(), subExt.end(), subExt.begin(), ::tolower);

                        if (subExt == ".obj") {
                            displayModelName = folderName;
                            payloadName = folderName;
                            break;
                        }
                        else if (subExt == ".gltf" || subExt == ".glb") {
                            displayModelName = subEntry.path().filename().string();
                            payloadName = subEntry.path().filename().string();
                            break;
                        }
                    }
                }

                if (!displayModelName.empty()) {
                    ImGui::PushID(displayModelName.c_str());
                    ImGui::Button(displayModelName.c_str(), ImVec2(100, 0));

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        ImGui::SetDragDropPayload("MODEL_ASSET", payloadName.c_str(), payloadName.size() + 1);
                        ImGui::Text("Model: %s", displayModelName.c_str());
                        ImGui::EndDragDropSource();
                    }
                    ImGui::PopID();

                    float lastButtonX = ImGui::GetItemRectMax().x;
                    float nextButtonX = lastButtonX + itemSpacing + 100.0f;
                    if (nextButtonX < windowVisibleX) {
                        ImGui::SameLine();
                    }
                }
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Directory Not Found: %s", baseDirectory.c_str());
        }
    }

    // =================================================================================
    // 2. プリセット一覧 (Presets)
    // =================================================================================
    if (ImGui::CollapsingHeader("Presets (Configured)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "▼ Create New Preset");

        // ★ ゲッターを使って安全にアクセス！
        if (editor_->GetSelectedObject()) {
            static char presetNameBuf[64] = "NewPreset";
            ImGui::PushItemWidth(150);
            ImGui::InputText("##PresetName", presetNameBuf, 64);
            ImGui::PopItemWidth();

            ImGui::SameLine();

            if (ImGui::Button("Save Selection")) {
                if (strlen(presetNameBuf) > 0) {
       
                    PresetManager::GetInstance()->AddPresetFromObject(presetNameBuf, editor_->GetSelectedObject());
                    DebugConsole::GetInstance()->AddLog("Saved Preset: " + std::string(presetNameBuf));
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Save the currently selected object's settings as a new preset.");
            }
        }
        else {
            ImGui::TextDisabled("(Select an object in Scene to save)");
        }

        ImGui::Separator();
        ImGui::Spacing();

        const auto& presets = PresetManager::GetInstance()->GetPresets();

        if (presets.empty()) {
            ImGui::TextDisabled("(No Presets Saved)");
        }
        else {
            for (const auto& [name, data] : presets) {
                ImGui::PushID(name.c_str());
                ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3f, 0.6f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.3f, 0.7f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.3f, 0.8f, 0.8f));

                ImGui::Button(name.c_str(), ImVec2(100, 0));

                ImGui::PopStyleColor(3);

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("PRESET_ASSET", name.c_str(), name.size() + 1);
                    ImGui::Text("Preset: %s", name.c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::PopID();

                float lastButtonX = ImGui::GetItemRectMax().x;
                float nextButtonX = lastButtonX + itemSpacing + 100.0f;
                if (nextButtonX < windowVisibleX) {
                    ImGui::SameLine();
                }
            }
        }
    }

    ImGui::End();
#endif
}