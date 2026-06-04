#include "InspectorWindow.h"
#include "DebugEditor.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3d.h"
#include "imgui.h"
#include "IconsFontAwesome5.h"
#include "EditorManager.h"
#include "ParticleManager.h"
#include "GPUParticleManager.h"
#include "ModelManager.h"
#include "GhostRecorder.h"
#include "DebugConsole.h"
#include "ImGuizmo.h"
#include <filesystem>
#include <algorithm>
#include <set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <PresetManager.h>
static const float PI = (float)M_PI;
static float ToRadians(float degrees) { return degrees * (PI / 180.0f); }
static float ToDegrees(float radians) { return radians * (180.0f / PI); }
namespace fs = std::filesystem;
void InspectorWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

void InspectorWindow::Draw() {
#ifdef USE_IMGUI
    if (editor_->GetSceneManager() == nullptr) return;
    BaseScene* currentScene = editor_->GetSceneManager()->GetCurrentScene();
    if (currentScene == nullptr) return;

    std::string currentJsonPath = "Resources/json/3Dobject/" + std::string(editor_->GetCurrentSceneFilenameBuffer());
    Object3d* selectedObject = editor_->GetSelectedObject();

    // ---------------------------------------------------------
    // 2. オブジェクト詳細 (Inspector本体)
    // ---------------------------------------------------------
    if (selectedObject == nullptr) {
        ImGui::TextDisabled(ICON_FA_EXCLAMATION_CIRCLE " オブジェクトが選択されていません");
        ImGui::TextDisabled("Hierarchyから選択してください");
        ImGui::Separator();
        // ゲッター経由での描画フラグ制御
        ImGui::Checkbox(ICON_FA_EYE " コライダー枠を描画", editor_->GetDrawCollidersPtr());
        ImGui::Checkbox(ICON_FA_FINGERPRINT " イベントIDを表示", editor_->GetDrawEventIDsPtr());
    }
    else {
        // --- 名前表示 ---
        char nameBuffer[256];
        std::string currentName = selectedObject->GetName();
        if (currentName.empty()) currentName = "NoName";
        strcpy_s(nameBuffer, currentName.c_str());

        if (ImGui::InputText(ICON_FA_TAG " 名前", nameBuffer, sizeof(nameBuffer))) {
            selectedObject->SetName(std::string(nameBuffer));
        }
        ImGui::Spacing();

        if (editor_->GetIsPathEditMode()) {
            // 編集モード中は目立つ色（オレンジ）にして警告を表示
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.4f, 0.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
            if (ImGui::Button(ICON_FA_CHECK_CIRCLE " パス編集を完了してロックを解除 (Exit Edit Mode)", ImVec2(-1, 45))) {
                editor_->SetIsPathEditMode(false);
                if (selectedObject->recorder_) selectedObject->recorder_->DeselectPin();
            }
            ImGui::PopStyleColor(2);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " 現在パスを編集中です。");
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "    本体の移動・選択・削除はロックされています。");
        }
        else {
            if (ImGui::Button(ICON_FA_PENCIL_ALT " パスの軌跡を編集する (Enter Path Edit Mode)", ImVec2(-1, 35))) {
                editor_->SetIsPathEditMode(true);
            }
        }
        ImGui::Separator();

        // =========================================================================
        // 🚨 ここから下は「パス編集中」はロック（操作不能）にするエリア
        // =========================================================================
        ImGui::BeginDisabled(editor_->GetIsPathEditMode());
        ImGui::Spacing();

        if (ImGui::Button(ICON_FA_COPY " 複製 (Duplicate)")) {
            editor_->DuplicateSelected();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_DOWNLOAD " 単体保存 (JSON更新)")) {
            editor_->SaveSingleObject();
        }
        ImGui::Spacing();

        // --- クラス名表示 ---
        ImGui::TextDisabled(ICON_FA_CUBES " クラス: %s", selectedObject->GetClassName().c_str());
        const char* saveCategories[] = { "Object", "Player", "Enemy" };
        std::string currentCat = selectedObject->GetSaveCategory();
        int catIndex = 0;
        if (currentCat == "Player") catIndex = 1;
        else if (currentCat == "Enemy") catIndex = 2;

        if (ImGui::Combo(ICON_FA_FOLDER " 保存先カテゴリ", &catIndex, saveCategories, IM_ARRAYSIZE(saveCategories))) {
            selectedObject->SetSaveCategory(saveCategories[catIndex]);
        }

        // --- 親の名前表示 ---
        if (selectedObject->GetParent()) {
            ImGui::TextDisabled(ICON_FA_SITEMAP " 親: %s", selectedObject->GetParent()->GetName().c_str());
            if (ImGui::Button(ICON_FA_UNLINK " 親を解除 (Unparent)")) {
                selectedObject->SetParent(nullptr);
            }
        }
        else {
            ImGui::TextDisabled(ICON_FA_SITEMAP " 親: なし");
        }

        // --- Model Asset (InvisibleBoxでない場合のみ表示) ---
        if (selectedObject->GetClassName() != "InvisibleBox") {
            ImGui::Separator();
            ImGui::Text(ICON_FA_CUBE " モデルアセット: %s", selectedObject->GetModelName().c_str());
            ImGui::Button(ICON_FA_BOX_OPEN " [ ここにモデルをドロップして変更 ] ", ImVec2(-1, 30));

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                    const char* modelName = (const char*)payload->Data;
                    ModelManager::GetInstance()->LoadModel(modelName);
                    selectedObject->SetModel(modelName);
                    DebugConsole::GetInstance()->AddLog("Switched model to: " + std::string(modelName));
                }
                
                // プリセットデータのドロップ受付
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
                    const char* presetName = (const char*)payload->Data;
                    PresetManager::GetInstance()->ApplyPresetToObject(presetName, selectedObject);
                    DebugConsole::GetInstance()->AddLog("Applied preset: " + std::string(presetName));
                }
                ImGui::EndDragDropTarget();
            }
        }

        // --- 可視性設定 ---
        ImGui::Separator();
        bool isVisible = selectedObject->GetIsVisible();
        if (ImGui::Checkbox(ICON_FA_EYE " 表示 (ゲーム内)", &isVisible)) {
            selectedObject->SetIsVisible(isVisible);
        }

        // --- Transform編集 ---
        ImGui::Separator();
        ImGui::Text(ICON_FA_ARROWS_ALT " トランスフォーム (Transform)");
        Transform* transform = selectedObject->GetTransform();
        bool isTransformChanged = false;

        if (ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 座標 (Pos)", &transform->translate.x, 0.1f)) isTransformChanged = true;

        Vector3 rotDeg = { ToDegrees(transform->rotate.x), ToDegrees(transform->rotate.y), ToDegrees(transform->rotate.z) };
        if (ImGui::DragFloat3(ICON_FA_SYNC " 回転 (Rot)", &rotDeg.x, 1.0f, -360.0f, 360.0f)) {
            transform->rotate = { ToRadians(rotDeg.x), ToRadians(rotDeg.y), ToRadians(rotDeg.z) };
            transform->isQuaternionMaster = false;
            isTransformChanged = true;
        }
        if (ImGui::DragFloat3(ICON_FA_EXPAND_ARROWS_ALT " スケール (Scale)", &transform->scale.x, 0.05f)) isTransformChanged = true;


        // --- コライダー設定 ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader(ICON_FA_SHIELD_ALT " コリジョン設定 (Collision)", ImGuiTreeNodeFlags_DefaultOpen)) {
            Object3d::ColliderConfig colConfig = selectedObject->GetColliderConfig();
            bool isColChanged = false;

            const char* typeNames[] = { "なし (None)", "球 (Sphere)", "箱 (AABB)", "回転箱 (OBB)", "円柱 (Cylinder)", "リング (Ring)" };
            int currentTypeIndex = (int)colConfig.type;
            if (ImGui::Combo(ICON_FA_SHAPES " 形状タイプ", &currentTypeIndex, typeNames, IM_ARRAYSIZE(typeNames))) {
                colConfig.type = (ColliderType)currentTypeIndex;
                if ((colConfig.type == ColliderType::kOBB || colConfig.type == ColliderType::kRing) && colConfig.size.x == 0.0f) {
                    colConfig.size = { 1.0f, 1.0f, 1.0f };
                }
                isColChanged = true;
            }

            if (colConfig.type != ColliderType::kNone) {
                if (ImGui::DragFloat3("中心オフセット", &colConfig.center.x, 0.05f)) isColChanged = true;

                if (colConfig.type == ColliderType::kSphere) {
                    if (ImGui::DragFloat("半径 (Radius)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) {
                        colConfig.size.y = colConfig.size.z = colConfig.size.x;
                        isColChanged = true;
                    }
                }
                else if (colConfig.type == ColliderType::kRing) {
                    if (ImGui::DragFloat("外径 (Outer Radius)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                    if (ImGui::DragFloat("内径 (Inner Radius)", &colConfig.size.z, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                    // 厚み(Y)
                    if (ImGui::DragFloat("厚み (Thickness)", &colConfig.size.y, 0.05f, 0.0f, 10.0f)) isColChanged = true;
                }
                else {
                    if (ImGui::DragFloat3("サイズ (Size)", &colConfig.size.x, 0.05f, 0.0f, 100.0f)) isColChanged = true;
                }

                if (colConfig.type == ColliderType::kOBB || colConfig.type == ColliderType::kRing) {
                    Vector3 rotDegObj = { ToDegrees(colConfig.rotation.x), ToDegrees(colConfig.rotation.y), ToDegrees(colConfig.rotation.z) };
                    if (ImGui::DragFloat3("回転 (Rotation)", &rotDegObj.x, 1.0f, -360.0f, 360.0f)) {
                        colConfig.rotation = { ToRadians(rotDegObj.x), ToRadians(rotDegObj.y), ToRadians(rotDegObj.z) };
                        isColChanged = true;
                    }
                }
                if (isColChanged) {
                    selectedObject->SetColliderConfig(colConfig);
                }
            }
            else {
                // なしの時も一応反映
                if (isColChanged) selectedObject->SetColliderConfig(colConfig);
            }
            ImGui::Separator();
            if (ImGui::CollapsingHeader(ICON_FA_PALETTE " グラフィックス (Material)", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool isGraphicsChanged = false;
                const char* matTypes[] = {
                                         "通常 (Standard)", "ガラス (Glass)", "氷・宝石 (Ice/Crystal)",
                                         "ホログラム (Hologram)", "消滅 (Dissolve)", "旧マグマ (Emissive)",
                                         "トゥーン調 (Cel Shaded)", "ローカルフォグ (Local Fog)",
                                         "水 (Water)", "新マグマ (Magma)", "分厚い氷 (Ice)", "炎 (Fire)"
                };
                int currentMatType = selectedObject->GetMaterialType();
                if (currentMatType < 0) currentMatType = 0;
                if (currentMatType > 11) currentMatType = 0; 
                if (ImGui::Combo(ICON_FA_PAINT_BRUSH " 質感 (Material Type)", &currentMatType, matTypes, IM_ARRAYSIZE(matTypes))) {
                    selectedObject->SetMaterialType(currentMatType);
                    isGraphicsChanged = true;
                }

                if (currentMatType == 0) {
                    float metallic = selectedObject->GetMetallic();
                    if (ImGui::SliderFloat("金属度 (Metallic)", &metallic, 0.0f, 1.0f)) {
                        selectedObject->SetMetallic(metallic); isGraphicsChanged = true;
                    }
                    float roughness = selectedObject->GetRoughness();
                    if (ImGui::SliderFloat("粗さ (Roughness)", &roughness, 0.0f, 1.0f)) {
                        selectedObject->SetRoughness(roughness); isGraphicsChanged = true;
                    }
                }

                if (currentMatType == 7) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), ICON_FA_SMOG " --- Local Fog Settings ---");
                    auto* fogData = selectedObject->GetLocalFogData();
                    if (fogData) {
                        ImGui::ColorEdit4("Fog Color (霧の色)", &fogData->fogColor.x);
                        ImGui::DragFloat("Density (濃さ)", &fogData->fogDensity, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("Edge Fade (境界線のボケ)", &fogData->edgeFade, 0.01f, 0.0f, 1.5f);
                        ImGui::DragFloat("Noise Speed (揺らぐ速さ)", &fogData->noiseSpeed, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("Noise Scale (模様の細かさ)", &fogData->noiseScale, 0.01f, 0.0f, 10.0f);
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), ICON_FA_SUN " --- Light Scattering ---");
                        ImGui::DragFloat("Scattering G (光の芯の強さ)", &fogData->scatteringG, 0.01f, 0.0f, 0.99f);
                        ImGui::DragFloat("Light Intensity (光の明るさ)", &fogData->scatteringIntensity, 0.01f, 0.0f, 5.0f);
                    }
                }
                if (currentMatType >= 8 && currentMatType <= 11) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), ICON_FA_TINT " --- Water Settings ---");

                    if (selectedObject->GetMeshRenderer() && selectedObject->GetMeshRenderer()->GetWaterParamData()) {
                        auto* waterData = selectedObject->GetMeshRenderer()->GetWaterParamData();
                        ImGui::DragFloat("Wave Speed (波の速さ)", &waterData->waveSpeed, 0.05f, 0.0f, 10.0f);
                        ImGui::DragFloat("Wave Height (波の高さ)", &waterData->waveHeight, 0.05f, 0.0f, 10.0f);
                        ImGui::DragFloat("Wave Frequency (波の細かさ)", &waterData->waveFrequency, 0.05f, 0.0f, 20.0f);
                        ImGui::Separator();
                        ImGui::Text(ICON_FA_WIND " --- Flow Settings ---");
                        ImGui::DragFloat("Flow Speed X", &waterData->flowSpeedX, 0.01f, -50.0f, 50.0f);
                        ImGui::DragFloat("Flow Speed Y", &waterData->flowSpeedY, 0.01f, -50.0f, 50.0f);
                    }
                }
                ImGui::Separator();
                bool enableNormal = selectedObject->GetEnableNormalMap();
                if (ImGui::Checkbox(ICON_FA_MAP " 法線マップ (Normal Map) 有効化", &enableNormal)) {
                    selectedObject->SetEnableNormalMap(enableNormal); isGraphicsChanged = true;
                }

                ImGui::Separator();
                bool enableEnv = selectedObject->GetEnableEnvMap();
                if (ImGui::Checkbox(ICON_FA_GLOBE " 環境マップ (IBL) 有効化", &enableEnv)) {
                    selectedObject->SetEnableEnvMap(enableEnv); isGraphicsChanged = true;
                }
                if (enableEnv) {
                    float envIntensity = selectedObject->GetEnvIntensity();
                    if (ImGui::SliderFloat("環境マップ強度", &envIntensity, 0.0f, 5.0f)) {
                        selectedObject->SetEnvIntensity(envIntensity); isGraphicsChanged = true;
                    }
                }
                if (enableNormal) {
                    static std::vector<std::string> albedoPaths;
                    static std::vector<std::string> normalPaths;
                    static std::vector<std::string> armPaths;
                    static bool isListInitialized = false;

                    if (!isListInitialized) {
                        albedoPaths.clear();
                        normalPaths.clear();
                        armPaths.clear();
                        std::string targetDir = "Resources/texture/PBR/";
                        if (std::filesystem::exists(targetDir)) {
                            // まず全ファイルを走査して、DDSが存在するパスを特定する
                            std::vector<std::string> allFiles;
                            std::set<std::string> ddsBaseNames; // 拡張子を除いたパスの集合

                            for (const auto& entry : std::filesystem::recursive_directory_iterator(targetDir)) {
                                if (entry.is_regular_file()) {
                                    std::string pathString = entry.path().string();
                                    std::replace(pathString.begin(), pathString.end(), '\\', '/');
                                    allFiles.push_back(pathString);

                                    if (entry.path().extension() == ".dds") {
                                        std::string base = entry.path().parent_path().string() + "/" + entry.path().stem().string();
                                        std::replace(base.begin(), base.end(), '\\', '/');
                                        ddsBaseNames.insert(base);
                                    }
                                }
                            }

                            // フィルタリングしながらリストに追加
                            for (const std::string& pathString : allFiles) {
                                std::filesystem::path p(pathString);
                                std::string ext = p.extension().string();
                                std::string base = p.parent_path().string() + "/" + p.stem().string();
                                std::replace(base.begin(), base.end(), '\\', '/');

                                // もし拡張子が .dds でない（png/jpg等）かつ、同じ名前の .dds が既に存在するならスキップ
                                if (ext != ".dds" && ddsBaseNames.count(base)) {
                                    continue;
                                }

                                if (ext == ".png" || ext == ".jpg" || ext == ".dds") {
                                    if (pathString.find("/Albedo/") != std::string::npos) albedoPaths.push_back(pathString);
                                    else if (pathString.find("/Normal/") != std::string::npos) normalPaths.push_back(pathString);
                                    else if (pathString.find("/ARM/") != std::string::npos) armPaths.push_back(pathString);
                                }
                            }
                        }
                        isListInitialized = true;
                    }

                    std::string currentPath = selectedObject->GetNormalMapPath();
                    const char* previewValue = currentPath.empty() ? "未設定 (クリックで選択)" : currentPath.c_str();

                    if (ImGui::BeginCombo(ICON_FA_IMAGE " ノーマル画像", previewValue)) {
                        for (const std::string& path : normalPaths) {
                            bool isSelected = (currentPath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetNormalMap(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("なし (クリア)", currentPath.empty())) {
                            selectedObject->SetNormalMap(""); isGraphicsChanged = true;
                        }
                        ImGui::EndCombo();
                    }

                    std::string currentOrmPath = selectedObject->GetOrmMapPath();
                    const char* previewOrmValue = currentOrmPath.empty() ? "未設定 (クリックで選択)" : currentOrmPath.c_str();

                    if (ImGui::BeginCombo(ICON_FA_IMAGE " ORMマップ (AO/粗さ/金属)", previewOrmValue)) {
                        for (const std::string& path : armPaths) {
                            bool isSelected = (currentOrmPath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetOrmMap(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("なし (クリア)", currentOrmPath.empty())) {
                            selectedObject->SetOrmMap(""); isGraphicsChanged = true;
                        }
                        ImGui::EndCombo();
                    }
                    std::string currentTexturePath = selectedObject->GetTexturePath();
                    const char* previewTextureValue = currentTexturePath.empty() ? "デフォルト (モデル固有)" : currentTexturePath.c_str();

                    if (ImGui::BeginCombo(ICON_FA_IMAGE " 基本画像 (Diffuse)", previewTextureValue)) {
                        for (const std::string& path : albedoPaths) {
                            bool isSelected = (currentTexturePath == path);
                            if (ImGui::Selectable(path.c_str(), isSelected)) {
                                selectedObject->SetTexture(path); isGraphicsChanged = true;
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::Separator();
                        if (ImGui::Selectable("デフォルトに戻す", currentTexturePath.empty())) {
                            selectedObject->SetTexture(""); isGraphicsChanged = true;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_SYNC_ALT " 更新")) {
                        isListInitialized = false;
                    }
                }
                ImGui::Separator();
                const char* blendModes[] = { "なし (None)", "通常 (Normal)", "加算 (Add)", "減算 (Subtract)", "乗算 (Multiply)", "スクリーン (Screen)" };
                int currentBlend = static_cast<int>(selectedObject->GetBlendMode());

                if (ImGui::Combo(ICON_FA_ADJUST " 合成 (Blend Mode)", &currentBlend, blendModes, IM_ARRAYSIZE(blendModes))) {
                    selectedObject->SetBlendMode(static_cast<BlendMode>(currentBlend)); isGraphicsChanged = true;
                }

                Vector4 color = selectedObject->GetColor();
                if (ImGui::ColorEdit4(ICON_FA_FILL_DRIP " 色 (Color)", &color.x)) {
                    selectedObject->SetColor(color); isGraphicsChanged = true;
                }

                ImGui::Spacing();
                float emissive = selectedObject->GetEmissive();
                // 1.0 で光なし。HDR空間で限界突破させるため最大値は大きめ(50.0等)にしておく
                if (ImGui::DragFloat(ICON_FA_SUN " 発光強度 (Emissive)", &emissive, 0.1f, 1.0f, 50.0f, "%.1f")) {
                    selectedObject->SetEmissive(emissive);
                    isGraphicsChanged = true;
                }
            }

            ImGui::Separator();
            if (ImGui::CollapsingHeader(ICON_FA_FIRE " パーティクル")) {
                // --- CPU Particle (Old) ---
                const auto& cpuParamsMap = ParticleManager::GetInstance()->GetParamsMap();
                std::vector<const char*> cpuItemNames;
                int currentCpuIndex = 0; int cpuIdx = 0;
                cpuItemNames.push_back("None (CPU)");

                std::string currentCpuName = selectedObject->GetParticleName();
                if (currentCpuName.empty()) currentCpuIndex = 0;

                for (const auto& [name, param] : cpuParamsMap) {
                    cpuItemNames.push_back(name.c_str());
                    if (name == currentCpuName) currentCpuIndex = cpuIdx + 1;
                    cpuIdx++;
                }

                if (ImGui::Combo("CPU Effect", &currentCpuIndex, cpuItemNames.data(), (int)cpuItemNames.size())) {
                    if (currentCpuIndex == 0) selectedObject->SetParticleName("");
                    else selectedObject->SetParticleName(cpuItemNames[currentCpuIndex]);
                }

                ImGui::Separator();

                // --- GPU Particle (New) ---
                const auto& gpuPresets = GPUParticleManager::GetInstance()->GetPresets();
                std::vector<const char*> gpuItemNames;
                int currentGpuIndex = 0; int gpuIdx = 0;
                gpuItemNames.push_back("None (GPU)");

                std::string currentGpuName = selectedObject->GetGPUParticleName();
                if (currentGpuName.empty()) currentGpuIndex = 0;

                for (const auto& [name, config] : gpuPresets) {
                    gpuItemNames.push_back(name.c_str());
                    if (name == currentGpuName) currentGpuIndex = gpuIdx + 1;
                    gpuIdx++;
                }

                if (ImGui::Combo("GPU Effect", &currentGpuIndex, gpuItemNames.data(), (int)gpuItemNames.size())) {
                    if (currentGpuIndex == 0) selectedObject->SetGPUParticleName("");
                    else selectedObject->SetGPUParticleName(gpuItemNames[currentGpuIndex]);
                }

                if (!currentGpuName.empty() && gpuPresets.find(currentGpuName) == gpuPresets.end()) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Warning: GPU JSON not found!");
                }
            }

            ImGui::Separator();
            if (ImGui::CollapsingHeader(ICON_FA_MAGIC " メッシュエフェクト (Mesh Effect)")) {
                std::vector<std::string> effectPaths;
                std::vector<std::string> effectDisplayNames;
                
                effectPaths.push_back(""); 
                effectDisplayNames.push_back("None");

                std::string effectDir = "Resources/json/effect";
                if (fs::exists(effectDir) && fs::is_directory(effectDir)) {
                    for (const auto& entry : fs::directory_iterator(effectDir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".json") {
                            effectPaths.push_back(entry.path().generic_string());
                            effectDisplayNames.push_back(entry.path().filename().generic_string());
                        }
                    }
                }

                std::vector<const char*> itemNames;
                for (const auto& displayName : effectDisplayNames) {
                    itemNames.push_back(displayName.c_str());
                }

                // --- スロット1 ---
                std::string currentEff1 = selectedObject->GetMeshEffect1Name();
                int selectIdx1 = 0;
                for (size_t i = 0; i < effectPaths.size(); ++i) {
                    if (effectPaths[i] == currentEff1) {
                        selectIdx1 = static_cast<int>(i);
                        break;
                    }
                }

                if (ImGui::Combo("Slot 1 (Floor etc.)", &selectIdx1, itemNames.data(), (int)itemNames.size())) {
                    selectedObject->SetMeshEffect1Name(effectPaths[selectIdx1]);
                }

                // --- スロット2 ---
                std::string currentEff2 = selectedObject->GetMeshEffect2Name();
                int selectIdx2 = 0;
                for (size_t i = 0; i < effectPaths.size(); ++i) {
                    if (effectPaths[i] == currentEff2) {
                        selectIdx2 = static_cast<int>(i);
                        break;
                    }
                }

                if (ImGui::Combo("Slot 2 (Pillar etc.)", &selectIdx2, itemNames.data(), (int)itemNames.size())) {
                    selectedObject->SetMeshEffect2Name(effectPaths[selectIdx2]);
                }
            }

            ImGui::Separator();
            uint32_t currentAttr = selectedObject->GetCollisionAttribute();
            DrawAttributeSelector(ICON_FA_TAGS " 自分の属性 (Attribute)", &currentAttr);
            if (currentAttr != selectedObject->GetCollisionAttribute()) selectedObject->SetCollisionAttribute(currentAttr);

            uint32_t currentMask = selectedObject->GetCollisionMask();
            DrawAttributeSelector(ICON_FA_TAGS " 衝突対象 (Mask)", &currentMask);
            if (currentMask != selectedObject->GetCollisionMask()) selectedObject->SetCollisionMask(currentMask);
        }

        // --- Gimmick (ID設定) ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader(ICON_FA_LINK " ギミック設定 (Link IDs)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("イベント連携ID:");
            int tID = selectedObject->GetTargetID();
            if (ImGui::InputInt("送信先ID (Target)", &tID)) selectedObject->SetTargetID(tID);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("トリガーを作動させたい相手のIDを指定してください");

            int eID = selectedObject->GetEventID();
            if (ImGui::InputInt("自分ID (Event)", &eID)) selectedObject->SetEventID(eID);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("ギミック等から起動されるための、自分のIDを指定してください");
        }

        // ==========================================
        // 1. ボーンアニメーション設定
        // ==========================================
        ImGui::Separator();
        ImGui::Text(ICON_FA_BONE " 【ボーンアニメーション】");
        char animNameBuf[64];
        strncpy_s(animNameBuf, selectedObject->animName_.c_str(), sizeof(animNameBuf));
        if (ImGui::InputText("アニメ名##BoneAnim", animNameBuf, sizeof(animNameBuf))) {
            selectedObject->animName_ = animNameBuf;
        }
        ImGui::Checkbox("ループ再生##BoneAnim", &selectedObject->isAnimLoop_);

        // ==========================================
        // 2. パス移動 (GhostRecorder) 設定
        // ==========================================
        ImGui::Separator();
        ImGui::Text(ICON_FA_GHOST " 【パス移動 (GhostRecorder)】");
        std::string currentRecordPreview = selectedObject->recordPathName_.empty() ? "(なし)" : selectedObject->recordPathName_;

        if (ImGui::BeginCombo("パスデータ", currentRecordPreview.c_str())) {
            bool isNoneSelected = selectedObject->recordPathName_.empty();
            if (ImGui::Selectable("(なし)", isNoneSelected)) {
                selectedObject->recordPathName_ = "";
                if (selectedObject->recorder_) selectedObject->recorder_->Stop();
            }
            if (isNoneSelected) ImGui::SetItemDefaultFocus();

            std::string dirPath = "Resources/json/animation/";
            if (fs::exists(dirPath) && fs::is_directory(dirPath)) {
                for (const auto& entry : fs::directory_iterator(dirPath)) {
                    if (entry.path().extension() == ".json") {
                        std::string fileName = entry.path().stem().string();
                        bool isSelected = (selectedObject->recordPathName_ == fileName);

                        if (ImGui::Selectable(fileName.c_str(), isSelected)) {
                            selectedObject->recordPathName_ = fileName;
                            if (selectedObject->recorder_) {
                                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
                            }
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Checkbox("ループ再生##Record", &selectedObject->isRecordLoop_)) {
            if (selectedObject->recorder_ && !selectedObject->recordPathName_.empty()) {
                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
            }
        }
        if (ImGui::Checkbox("相対座標モード##Record", &selectedObject->isRecordRelative_)) {
            if (selectedObject->recorder_ && !selectedObject->recordPathName_.empty()) {
                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
            }
        }
        if (ImGui::Button("テスト再生##Record")) {
            if (selectedObject->recorder_ && !selectedObject->recordPathName_.empty()) {
                bool isCinematic = (selectedObject->GetClassName() == "CinematicCamera");
                selectedObject->recorder_->Play(selectedObject->recordPathName_, selectedObject->isRecordLoop_, selectedObject->isRecordRelative_, isCinematic);
            }
        }

        // --- Game Data (Stats) ---
        ImGui::Separator();
        if (ImGui::CollapsingHeader(ICON_FA_GAMEPAD " ゲームデータ (Stats)", ImGuiTreeNodeFlags_DefaultOpen)) {
            EventType currentType = selectedObject->GetEventType();
            int currentItemIndex = static_cast<int>(currentType);
            const char* eventNames[] = { "なし", "ダメージ", "ワープ", "映像演出 (橋落ち)", "中間地点 (Checkpoint)", "ゴール", "ステージセレクト", "スターコイン (StarCoin)" };
            if (ImGui::Combo(ICON_FA_FLAG " イベント種類", &currentItemIndex, eventNames, IM_ARRAYSIZE(eventNames))) {
                selectedObject->SetEventType(static_cast<EventType>(currentItemIndex));
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "--- Object Type Settings ---");
            const char* classItems[] = { "Model", "Spawner", "Player", "Enemy", "Gimmick", "Item", "InvisibleBox", "Block" };
            std::string currentClass = selectedObject->GetClassName();
            int currentClassIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(classItems); i++) {
                if (currentClass == classItems[i]) { currentClassIndex = i; break; }
            }

            if (ImGui::Combo(ICON_FA_CUBES " Class Type", &currentClassIndex, classItems, IM_ARRAYSIZE(classItems))) {
                selectedObject->SetClassName(classItems[currentClassIndex]);
                if (std::string(classItems[currentClassIndex]) == "Spawner") {
                    if (selectedObject->GetName().find("Object") != std::string::npos) selectedObject->SetName("Spawner_New");
                    if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                }
                else if (std::string(classItems[currentClassIndex]) == "Item") {
                    if (selectedObject->GetName().find("Object") != std::string::npos) selectedObject->SetName("Item_Heal");
                    if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                    selectedObject->SetItemType("Heal");
                    selectedObject->param_->itemType = "Heal";
                    selectedObject->param_->healAmount = 1.0f;
                    selectedObject->SetModel("Item/heart.gltf");
                    selectedObject->SetColor({ 1.0f, 0.15f, 0.35f, 1.0f });
                    selectedObject->SetEmissive(1.8f);
                    selectedObject->SetScale({ 0.8f, 0.8f, 0.8f });
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
                    selectedObject->SetStatic(false);

                    Object3d::ColliderConfig colConfig;
                    colConfig.type = ColliderType::kSphere;
                    colConfig.size = { 1.2f, 1.2f, 1.2f };
                    selectedObject->SetColliderConfig(colConfig);
                    selectedObject->SetCollisionRadius(1.2f);
                }
            }

            if (selectedObject->GetClassName() == "Spawner") DrawSpawnerSettings();

            ImGui::Spacing();
            if (selectedObject->GetClassName() == "Enemy") {
                ImGui::Indent(); DrawEnemyTypeSelector(); ImGui::Unindent();
            }
            if (selectedObject->GetClassName() == "Gimmick") {
                ImGui::Indent(); DrawGimmickTypeSelector(); ImGui::Unindent();
            }
            if (selectedObject->GetClassName() == "Item") {
                ImGui::Indent(); DrawItemTypeSelector(); ImGui::Unindent();
            }

            if (selectedObject->GetClassName() == "Enemy" || selectedObject->GetClassName() == "Player") {
                if (!selectedObject->param_.has_value()) {
                    if (ImGui::Button(ICON_FA_PLUS_CIRCLE " ステータスを追加", ImVec2(-1, 0))) selectedObject->param_.emplace();
                }
                else {
                    auto& p = selectedObject->param_.value();
                    ImGui::Text("キャラクター・ステータス:");
                    ImGui::Indent();
                    ImGui::DragFloat(ICON_FA_HEART " HP (体力)", &p.hp, 1.0f, 0.0f, 9999.0f);
                    ImGui::DragFloat(ICON_FA_HEARTBEAT " Max HP", &p.maxHp, 1.0f, 1.0f, 9999.0f);
                    ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 速度 (Speed)", &p.speed, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat(ICON_FA_ARROW_DOWN " 重力 (Gravity)", &p.gravity, 0.01f, -10.0f, 10.0f);
                    ImGui::DragFloat(ICON_FA_ARROW_UP " ジャンプ力", &p.jumpPower, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat(ICON_FA_SEARCH " 検知範囲 (Detection)", &p.detectionRange, 0.5f, 0.0f, 500.0f);
                    ImGui::Unindent();

                    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
                    if (ImGui::Button(ICON_FA_TRASH_ALT " ステータスを削除", ImVec2(-1, 0))) selectedObject->param_ = std::nullopt;
                    ImGui::PopStyleColor();
                }
            }
            else if (selectedObject->GetClassName() == "Gimmick") {
                if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                auto& p = selectedObject->param_.value();
                
                ImGui::Text(ICON_FA_TOOLS " ギミック設定:");
                ImGui::Indent();
                
                std::string gType = selectedObject->GetGimmickType();
                if (gType == "Trampoline") {
                    ImGui::DragFloat(ICON_FA_ARROW_UP " ジャンプ力 (Jump Power)", &p.jumpPower, 1.0f, 0.0f, 100.0f);
                }
                else if (gType == "MovingFloor") {
                    ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 移動速度 (Speed)", &p.speed, 0.1f, 0.0f, 100.0f);
                }
                else if (gType == "ChikuwaBlock") {
                    ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 震え時間 (Shake)", &p.shakeDuration, 0.1f, 0.0f, 10.0f, "%.1f s");
                    ImGui::DragFloat(ICON_FA_ARROW_DOWN " 落下時間 (Fall)", &p.fallDuration, 0.1f, 0.0f, 10.0f, "%.1f s");
                    ImGui::DragFloat(ICON_FA_RECYCLE " リスポーン間隔", &p.interval, 0.1f, 0.0f, 10.0f, "%.1f s");
                    ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 落下速度 (Gravity)", &p.gravity, 1.0f, 0.0f, 200.0f);
                }
                else if (gType == "BlinkBlock") {
                    ImGui::Text(ICON_FA_PALETTE " ブロックの色設定:");
                    ImGui::RadioButton("青 (Blue: Jump Even)", &p.colorType, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("赤 (Red: Jump Odd)", &p.colorType, 1);
                }
                else if (gType == "Switch") {
                    const char* switchModes[] = { "押している間だけ", "押すたび切替", "一定時間だけ" };
                    ImGui::Combo("スイッチ方式", &p.switchMode, switchModes, IM_ARRAYSIZE(switchModes));
                    if (p.switchMode == 2) {
                        ImGui::DragFloat(ICON_FA_CLOCK " 有効時間", &p.interval, 0.1f, 0.1f, 60.0f, "%.1f s");
                    }
                    ImGui::TextDisabled("Target ID と受信側の My Event ID を合わせてください");
                }
                else if (gType == "EventReceiver") {
                    const char* actionModes[] = { "出現", "Y方向に移動", "X方向に移動", "Z方向に移動", "有効化", "無効化" };
                    ImGui::Combo("動作モード", &p.actionMode, actionModes, IM_ARRAYSIZE(actionModes));

                    if (p.actionMode >= 1 && p.actionMode <= 3) {
                        ImGui::DragFloat(ICON_FA_ARROWS_ALT " 移動量", &p.moveAmount, 0.1f, -500.0f, 500.0f);
                        ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 移動速度", &p.moveSpeed, 0.1f, 0.1f, 60.0f);
                    }

                    ImGui::Checkbox("開始時に有効", &p.startActive);
                    ImGui::Checkbox("OFFで元に戻す", &p.returnOnOff);
                    ImGui::TextDisabled("My Event ID とスイッチの Target ID を合わせてください");
                }
                else if (gType == "HookPullBlock") {
                    ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 引っ張り速度", &p.speed, 0.5f, 1.0f, 120.0f);
                    ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 重力", &p.gravity, 1.0f, 0.0f, 200.0f);
                    ImGui::TextDisabled("フックを当てるとプレイヤー側へ引き寄せます");
                }
                else if (gType == "OneWayFloor") {
                    ImGui::TextDisabled("上から着地した時だけ足場になります");
                }
                else if (gType == "LiquidLevel") {
                    const char* liquidTypes[] = { "水", "マグマ" };
                    ImGui::Combo("液体の種類", &p.colorType, liquidTypes, IM_ARRAYSIZE(liquidTypes));
                    ImGui::DragFloat(ICON_FA_ARROWS_ALT_V " 上下量", &p.moveAmount, 0.1f, -500.0f, 500.0f);
                    ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 上下速度", &p.moveSpeed, 0.1f, 0.1f, 60.0f);
                    ImGui::Checkbox("開始時に上昇", &p.startActive);
                    ImGui::Checkbox("OFFで元に戻す", &p.returnOnOff);
                    ImGui::TextDisabled("スイッチの Target ID とこの My Event ID を合わせてください");
                }
                else if (gType == "ChainCollapseFloor") {
                    ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 揺れ時間", &p.shakeDuration, 0.05f, 0.0f, 10.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_LINK " 連鎖までの時間", &p.interval, 0.01f, 0.0f, 10.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_ARROW_DOWN " 落下時間", &p.fallDuration, 0.05f, 0.1f, 10.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 重力", &p.gravity, 1.0f, 0.0f, 200.0f);
                    ImGui::TextDisabled("Target ID に次の床の My Event ID を入れると連鎖します");
                }
                else if (gType == "RotatingFloor" || gType == "RotatingPillar") {
                    const char* axes[] = { "X", "Y", "Z" };
                    ImGui::Combo("回転軸", &p.actionMode, axes, IM_ARRAYSIZE(axes));
                    ImGui::DragFloat(ICON_FA_SYNC_ALT " 回転速度 (度/秒)", &p.speed, 1.0f, -720.0f, 720.0f);
                    ImGui::Checkbox("開始時に回転", &p.startActive);
                    ImGui::Checkbox("OFFで停止", &p.returnOnOff);
                    ImGui::TextDisabled("スイッチ連動で回転の開始/停止ができます");
                }
                else if (gType == "PhaseFlipFloor") {
                    int floorNumber = p.colorType + 1;
                    int phaseCount = (std::max)(1, p.maxCount);
                    ImGui::DragInt("床番号", &floorNumber, 1, 1, phaseCount);
                    p.colorType = (std::clamp)(floorNumber, 1, phaseCount) - 1;

                    ImGui::DragInt("全体の床数", &p.maxCount, 1, 1, 16);
                    if (p.colorType >= p.maxCount) p.colorType = p.maxCount - 1;

                    ImGui::DragFloat(ICON_FA_CLOCK " 1フェーズの時間", &p.interval, 0.05f, 0.1f, 30.0f, "%.2f s");
                    ImGui::Checkbox("正方向に回転", &p.startActive);
                    ImGui::TextDisabled("床番号 1 -> 2 -> 3 ... の順に、当たり判定を残したまま180度回転します");
                }
                else if (gType == "LaserEmitter") {
                    ImGui::DragFloat(ICON_FA_BOLT " ダメージ量", &p.speed, 0.5f, 0.0f, 100.0f);
                    ImGui::DragFloat(ICON_FA_CLOCK " ダメージ間隔", &p.interval, 0.05f, 0.05f, 10.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_ARROWS_ALT_H " レーザーの太さ", &p.moveAmount, 0.01f, 0.03f, 5.0f);
                    ImGui::Checkbox("開始時に有効", &p.startActive);
                    ImGui::Checkbox("OFFで停止", &p.returnOnOff);
                    ImGui::TextDisabled("Target ID に終点ノードの My Event ID を入れると接続します");
                }
                else if (gType == "LaserNode") {
                    ImGui::DragFloat(ICON_FA_BOLT " ダメージ量", &p.speed, 0.5f, 0.0f, 100.0f);
                    ImGui::DragFloat(ICON_FA_CLOCK " ダメージ間隔", &p.interval, 0.05f, 0.05f, 10.0f, "%.2f s");
                    ImGui::DragFloat(ICON_FA_ARROWS_ALT_H " レーザーの太さ", &p.moveAmount, 0.01f, 0.03f, 5.0f);
                    ImGui::Checkbox("開始時に有効", &p.startActive);
                    ImGui::Checkbox("OFFで停止", &p.returnOnOff);
                    ImGui::TextDisabled("Target ID に次の LaserNode の My Event ID を入れると、その間にレーザーが出ます");
                }
                else {
                    ImGui::TextDisabled("(この種類には個別設定がありません)");
                }
                ImGui::Unindent();
            }
            else if (selectedObject->GetClassName() == "Item") {
                if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
                auto& p = selectedObject->param_.value();

                ImGui::Text(ICON_FA_HEART " アイテム設定:");
                ImGui::Indent();

                std::string itemType = selectedObject->GetItemType();
                if (itemType == "Heal") {
                    ImGui::DragFloat(ICON_FA_HEARTBEAT " 回復量", &p.healAmount, 0.1f, 0.0f, 999.0f);
                    ImGui::TextDisabled("プレイヤーが触れるとHPを回復して消えます");
                }
                else {
                    ImGui::TextDisabled("(この種類には個別設定がありません)");
                }

                ImGui::Unindent();
            }
        }

        ImGui::EndDisabled();
        ImGui::Separator();

        if (ImGui::Button(ICON_FA_TRASH_ALT " オブジェクト削除", ImVec2(-1, 0))) {
            editor_->DeleteSelected();
            EditorManager::GetInstance()->ClearSelection();
        }

        // --- Gizmo 操作切替 ---
        ImGui::Separator();
        ImGui::Text(ICON_FA_HAND_POINTER " ギズモ操作モード:");
        static ImGuizmo::OPERATION currentOperation = ImGuizmo::TRANSLATE;
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
            if (ImGui::IsKeyPressed(ImGuiKey_T)) currentOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) currentOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_S)) currentOperation = ImGuizmo::SCALE;
        }
        if (ImGui::RadioButton("移動", currentOperation == ImGuizmo::TRANSLATE)) currentOperation = ImGuizmo::TRANSLATE; ImGui::SameLine();
        if (ImGui::RadioButton("回転", currentOperation == ImGuizmo::ROTATE)) currentOperation = ImGuizmo::ROTATE; ImGui::SameLine();
        if (ImGui::RadioButton("拡大縮小", currentOperation == ImGuizmo::SCALE)) currentOperation = ImGuizmo::SCALE;
    }
#endif
}

// -------------------------------------------------------------
// UIヘルパー関数群
// -------------------------------------------------------------
void InspectorWindow::DrawSpawnerSettings() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    ImGui::Separator();
    ImGui::Indent();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[ Spawner Config ]");

    if (!selectedObject->param_.has_value()) {
        selectedObject->param_.emplace();
    }
    auto& p = selectedObject->param_.value();

    static char typeBuf[64] = "";
    if (typeBuf[0] == '\0') {
        strcpy_s(typeBuf, sizeof(typeBuf), p.enemyType.c_str());
    }

    const char* enemyTypes[] = { "Slime", "Bomb", "Bomber", "Mushroom", "GiantSlime", "Bat", "BeamDrone" };
    int currentTypeIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
        if (p.enemyType == enemyTypes[i]) currentTypeIndex = i;
    }

    if (ImGui::Combo("Spawn Type", &currentTypeIndex, enemyTypes, IM_ARRAYSIZE(enemyTypes))) {
        p.enemyType = enemyTypes[currentTypeIndex];
        strcpy_s(typeBuf, sizeof(typeBuf), p.enemyType.c_str());
    }

    ImGui::DragFloat("Interval (sec)", &p.interval, 0.1f, 0.1f, 60.0f, "%.1f s");
    ImGui::InputInt("Max Count", &p.maxCount);

    ImGui::Unindent();
#endif
}

void InspectorWindow::DrawEnemyTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const char* enemyTypes[] = { "Slime", "BossCore", "Bomb", "Bomber", "Mushroom", "GiantSlime", "Bat", "BeamDrone" };
    std::string currentType = selectedObject->GetEnemyType();

    int currentIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
        if (currentType == enemyTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    const char* previewValue = (currentIndex >= 0) ? enemyTypes[currentIndex] : "(未設定)";

    if (ImGui::BeginCombo("敵の種族 (Enemy Type)", previewValue)) {
        for (int i = 0; i < IM_ARRAYSIZE(enemyTypes); i++) {
            bool isSelected = (currentIndex == i);
            if (ImGui::Selectable(enemyTypes[i], isSelected)) {
                selectedObject->SetEnemyType(enemyTypes[i]);
                selectedObject->SetName("Enemy_" + std::string(enemyTypes[i]));
                if (std::string(enemyTypes[i]) == "Bat") {
                    selectedObject->SetModel("Characters/bat");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetScale({ 0.6f, 0.6f, 0.6f });
                    selectedObject->animName_ = "ArmatureAction";
                    selectedObject->isAnimLoop_ = true;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(0.85f);
                }
                else if (std::string(enemyTypes[i]) == "BeamDrone") {
                    selectedObject->SetModel("Characters/eye");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.4f);
                    selectedObject->SetScale({ 0.85f, 0.85f, 0.85f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(1.1f);
                }
                else if (std::string(enemyTypes[i]) == "GiantSlime") {
                    selectedObject->SetModel("Characters/slime");
                    selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    selectedObject->SetEmissive(1.0f);
                    selectedObject->SetScale({ 2.8f, 2.8f, 2.8f });
                    selectedObject->animName_.clear();
                    selectedObject->isAnimLoop_ = false;
                    selectedObject->SetCollisionAttribute(CollisionAttribute::kEnemy);
                    selectedObject->SetCollisionMask(CollisionAttribute::kPlayer | CollisionAttribute::kGround | CollisionAttribute::kPlayerAttack | CollisionAttribute::kAttributePlayerBullet);
                    selectedObject->SetColliderType(ColliderType::kSphere);
                    selectedObject->SetCollisionRadius(2.2f);
                }
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成される敵クラスを指定します。\nEmptyの場合はただの箱になります。");
#endif
}

void InspectorWindow::DrawGimmickTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const char* gimmickTypes[] = { "Default", "MovingFloor", "Trampoline", "ChikuwaBlock", "BlinkBlock", "BreakableBlock", "Coin", "HookAnchor", "SinkingFloor", "SeesawFloor", "DashPanel", "IceFloor", "TimedSwitch", "AppearingFloor", "Switch", "EventReceiver", "HookPullBlock", "OneWayFloor", "LiquidLevel", "ChainCollapseFloor", "RotatingFloor", "RotatingPillar", "PhaseFlipFloor", "LaserEmitter", "LaserNode" };
    const char* gimmickTypeLabels[] = {
        "通常",
        "移動床",
        "トランポリン",
        "ちくわブロック",
        "点滅ブロック",
        "破壊ブロック",
        "コイン",
        "フックアンカー",
        "沈む床",
        "シーソー床",
        "ダッシュパネル",
        "氷の床",
        "時限スイッチ床",
        "出現床",
        "汎用スイッチ",
        "イベント受信ギミック",
        "フックで引っ張るブロック",
        "一方通行床",
        "水位・マグマ上下",
        "連鎖崩れ床",
        "回転床",
        "回転柱",
        "順番反転床",
        "レーザー発生器",
        "レーザー接続ノード"
    };
    std::string currentType = selectedObject->GetGimmickType();

    int currentIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(gimmickTypes); i++) {
        if (currentType == gimmickTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    if (ImGui::Combo("ギミックの種類", &currentIndex, gimmickTypeLabels, IM_ARRAYSIZE(gimmickTypeLabels))) {
        std::string selectedGimmickType = gimmickTypes[currentIndex];
        selectedObject->SetGimmickType(selectedGimmickType);
        
        if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
        selectedObject->param_->gimmickType = selectedGimmickType;
        
        // 各ギミックに合わせた初期状態（エディタ上のデフォルト初期値）を設定
        if (selectedGimmickType == "BreakableBlock") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_BreakableBlock");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.8f, 0.4f, 0.1f, 1.0f }); // 壊せそうな土褐色・レンガ色
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });
            
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            
            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "Coin") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_Coin");
            selectedObject->SetModel("Primitives/sphere");
            selectedObject->SetColor({ 1.0f, 0.9f, 0.0f, 1.0f }); // ゴールドイエロー
            selectedObject->SetScale({ 0.6f, 0.6f, 0.15f }); // 薄いコインの形
            
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            
            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.0f);
        }
        else if (selectedGimmickType == "HookAnchor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_HookAnchor");
            selectedObject->SetModel("Primitives/sphere");
            selectedObject->SetColor({ 0.2f, 0.85f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.2f, 1.2f, 1.2f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kHookAnchor);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 2.5f, 2.5f, 2.5f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(2.5f);
        }
        else if (selectedGimmickType == "SinkingFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_SinkingFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "SeesawFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_SeesawFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.9f, 0.75f, 0.35f, 1.0f });
            selectedObject->SetScale({ 4.0f, 0.35f, 1.4f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "DashPanel") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_DashPanel");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 1.0f, 0.55f, 0.1f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.25f, 1.2f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "IceFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_IceFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.65f, 0.9f, 1.0f, 0.9f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "TimedSwitch") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_TimedSwitch");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.5f, 0.25f, 1.5f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "AppearingFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_AppearingFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 1.0f, 0.7f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.35f, 2.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "Switch") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_Switch");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.5f, 0.25f, 1.5f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->switchMode = 0;
            selectedObject->param_->interval = 3.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "EventReceiver") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_EventReceiver");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.65f, 1.0f, 0.65f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.35f, 2.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->actionMode = 0;
            selectedObject->param_->moveAmount = 10.0f;
            selectedObject->param_->moveSpeed = 6.0f;
            selectedObject->param_->startActive = false;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "HookPullBlock") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_HookPullBlock");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 0.85f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 42.0f;
            selectedObject->param_->gravity = 50.0f;
            selectedObject->param_->maxFallSpeed = 60.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "OneWayFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_OneWayFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.85f, 0.9f, 0.65f, 0.9f });
            selectedObject->SetScale({ 2.5f, 0.22f, 2.5f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "LiquidLevel") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_LiquidLevel");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.45f, 0.85f, 1.0f, 0.65f });
            selectedObject->SetScale({ 4.0f, 0.08f, 4.0f });
            selectedObject->SetMaterialType(8);
            selectedObject->SetCollisionAttribute(0);
            selectedObject->SetCollisionMask(0);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->colorType = 0;
            selectedObject->param_->moveAmount = 6.0f;
            selectedObject->param_->moveSpeed = 3.0f;
            selectedObject->param_->startActive = false;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "ChainCollapseFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_ChainCollapseFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.75f, 0.92f, 1.0f, 0.82f });
            selectedObject->SetScale({ 2.0f, 0.25f, 2.0f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->shakeDuration = 0.45f;
            selectedObject->param_->fallDuration = 1.4f;
            selectedObject->param_->interval = 0.18f;
            selectedObject->param_->gravity = 48.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "RotatingFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_RotatingFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.95f, 0.65f, 0.35f, 1.0f });
            selectedObject->SetScale({ 3.0f, 0.3f, 1.2f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 45.0f;
            selectedObject->param_->actionMode = 1;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "RotatingPillar") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_RotatingPillar");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.95f, 0.65f, 0.35f, 1.0f });
            selectedObject->SetScale({ 0.75f, 3.0f, 0.75f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 60.0f;
            selectedObject->param_->actionMode = 1;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "PhaseFlipFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_PhaseFlipFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.35f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.25f, 2.0f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->colorType = 0;
            selectedObject->param_->maxCount = 3;
            selectedObject->param_->interval = 1.0f;
            selectedObject->param_->startActive = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "LaserEmitter") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_LaserEmitter");
            selectedObject->SetModel("Primitives/cube");
            selectedObject->SetColor({ 1.0f, 0.08f, 0.05f, 0.9f });
            selectedObject->SetBlendMode(BlendMode::kAdd);
            selectedObject->SetMaterialType(3);
            selectedObject->SetTexture("Resources/sprite/white.png");
            selectedObject->SetEmissive(6.0f);
            selectedObject->SetScale({ 0.25f, 0.25f, 1.0f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 10.0f;
            selectedObject->param_->interval = 0.7f;
            selectedObject->param_->moveAmount = 0.14f;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "LaserNode") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_LaserNode");
            selectedObject->SetModel("Primitives/sphere");
            selectedObject->SetColor({ 1.0f, 0.18f, 0.08f, 1.0f });
            selectedObject->SetBlendMode(BlendMode::kAdd);
            selectedObject->SetMaterialType(3);
            selectedObject->SetTexture("Resources/sprite/white.png");
            selectedObject->SetEmissive(3.5f);
            selectedObject->SetScale({ 0.35f, 0.35f, 0.35f });
            selectedObject->SetCollisionAttribute(0);
            selectedObject->SetCollisionMask(0);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 10.0f;
            selectedObject->param_->interval = 0.7f;
            selectedObject->param_->moveAmount = 0.14f;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.0f);
        }
        else if (selectedGimmickType == "Default") {
            selectedObject->SetClassName("Default");
            selectedObject->SetName("Cube");
        }
        else {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_" + selectedGimmickType);
        }
    }
    
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成されるギミッククラスを指定します。");
#endif
}

void InspectorWindow::DrawItemTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const char* itemTypes[] = { "Heal" };
    const char* itemTypeLabels[] = { "体力回復" };
    std::string currentType = selectedObject->GetItemType();
    if (currentType.empty()) {
        currentType = "Heal";
        selectedObject->SetItemType(currentType);
        if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
        selectedObject->param_->itemType = currentType;
        selectedObject->param_->healAmount = 1.0f;
        selectedObject->SetName("Item_Heal");
        selectedObject->SetModel("Item/heart.gltf");
        selectedObject->SetColor({ 1.0f, 0.15f, 0.35f, 1.0f });
        selectedObject->SetEmissive(1.8f);
        selectedObject->SetScale({ 0.8f, 0.8f, 0.8f });
        selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
        selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
        selectedObject->SetStatic(false);

        Object3d::ColliderConfig colConfig;
        colConfig.type = ColliderType::kSphere;
        colConfig.size = { 1.2f, 1.2f, 1.2f };
        selectedObject->SetColliderConfig(colConfig);
        selectedObject->SetCollisionRadius(1.2f);
    }

    int currentIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(itemTypes); i++) {
        if (currentType == itemTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    if (ImGui::Combo("アイテムの種類", &currentIndex, itemTypeLabels, IM_ARRAYSIZE(itemTypeLabels))) {
        std::string selectedItemType = itemTypes[currentIndex];
        selectedObject->SetClassName("Item");
        selectedObject->SetItemType(selectedItemType);

        if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
        selectedObject->param_->itemType = selectedItemType;

        if (selectedItemType == "Heal") {
            selectedObject->SetName("Item_Heal");
            selectedObject->SetModel("Item/heart.gltf");
            selectedObject->SetColor({ 1.0f, 0.15f, 0.35f, 1.0f });
            selectedObject->SetEmissive(1.8f);
            selectedObject->SetScale({ 0.8f, 0.8f, 0.8f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            selectedObject->SetStatic(false);

            selectedObject->param_->healAmount = 1.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.2f, 1.2f, 1.2f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.2f);
        }
    }

    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成されるアイテムクラスを指定します。");
#endif
}

void InspectorWindow::DrawAttributeSelector(const char* label, uint32_t* attribute) {
#ifdef USE_IMGUI
    if (ImGui::TreeNode(label)) {
        int flags = static_cast<int>(*attribute);
        ImGui::CheckboxFlags("プレイヤー (Player)", &flags, 1 << 0);
        ImGui::CheckboxFlags("敵 (Enemy)", &flags, 1 << 1);
        ImGui::CheckboxFlags("床・地形 (Ground)", &flags, 1 << 2);
        ImGui::CheckboxFlags("弾 (Bullet)", &flags, 1 << 3);
        ImGui::CheckboxFlags("トリガー (Trigger)", &flags, 1 << 4);
        ImGui::CheckboxFlags("プレイヤー攻撃 (PlayerAttack)", &flags, 1 << 6);
        ImGui::CheckboxFlags("敵攻撃 (EnemyAttack)", &flags, 1 << 7);
        *attribute = static_cast<uint32_t>(flags);
        ImGui::TreePop();
    }
#endif
}
