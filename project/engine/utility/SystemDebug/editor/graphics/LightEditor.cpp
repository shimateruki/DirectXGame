#include "LightEditor.h"
#include "imgui.h"
#include "json.hpp"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "imgui_internal.h"
#include "CameraManager.h"
#include "Camera.h"
#include "SceneManager.h" 
#include "IconsFontAwesome5.h"
#include "DebugConsole.h"
using json = nlohmann::json;

#ifdef USE_IMGUI
static ImVec2 WorldToScreen(const Vector3& worldPos, const Matrix4x4& mat) {
    float w = worldPos.x * mat.m[0][3] + worldPos.y * mat.m[1][3] + worldPos.z * mat.m[2][3] + mat.m[3][3];
    if (w < 0.001f) return ImVec2(-10000.0f, -10000.0f);

    float x = (worldPos.x * mat.m[0][0] + worldPos.y * mat.m[1][0] + worldPos.z * mat.m[2][0] + mat.m[3][0]) / w;
    float y = (worldPos.x * mat.m[0][1] + worldPos.y * mat.m[1][1] + worldPos.z * mat.m[2][1] + mat.m[3][1]) / w;

    ImGuiIO& io = ImGui::GetIO();
    float screenX = (x + 1.0f) * 0.5f * io.DisplaySize.x;
    float screenY = (1.0f - y) * 0.5f * io.DisplaySize.y;
    return ImVec2(screenX, screenY);
}
#endif

LightEditor* LightEditor::GetInstance() {
    static LightEditor instance;
    return &instance;
}

void LightEditor::SetStatusMessage(const std::string& message, bool success) {
    statusMessage_ = message;
    statusSuccess_ = success;
#ifdef USE_IMGUI
    statusVisibleUntil_ = ImGui::GetTime() + 3.0;
#else
    statusVisibleUntil_ = 0.0;
#endif
}

std::string LightEditor::BuildFullPathFromFileName() const {
    return "Resources/json/light/" + std::string(currentFileName_);
}

void LightEditor::SyncCurrentFileNameFromManager() {
    if (!lightManager_) {
        return;
    }

    const std::string& currentPath = lightManager_->GetCurrentStateFile();
    if (currentPath.empty() || currentPath == syncedLightPath_) {
        return;
    }

    std::filesystem::path path(currentPath);
    std::string fileName = path.filename().string();
    if (fileName.empty()) {
        fileName = currentPath;
    }

    strncpy_s(currentFileName_, sizeof(currentFileName_), fileName.c_str(), _TRUNCATE);
    syncedLightPath_ = currentPath;
}

void LightEditor::DrawLightFileList() {
#ifdef USE_IMGUI
    SyncCurrentFileNameFromManager();

    const std::string currentPath = lightManager_ ? lightManager_->GetCurrentStateFile() : "";
    ImGui::Text(ICON_FA_FOLDER_OPEN " 現在のライト設定");
    ImGui::SameLine();
    ImGui::TextColored(
        lightManager_ && lightManager_->WasLastLoadSuccessful() ? ImVec4(0.45f, 1.0f, 0.65f, 1.0f) : ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
        "%s",
        currentPath.empty() ? "(未設定)" : currentPath.c_str()
    );
    if (lightManager_ && !lightManager_->WasLastLoadSuccessful()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " ファイルが未作成です。保存するとこの名前で作成されます。");
    }

    std::vector<std::string> fileNames;
    const std::filesystem::path lightDir("Resources/json/light");
    if (std::filesystem::exists(lightDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(lightDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }
            fileNames.push_back(entry.path().filename().string());
        }
    }
    std::sort(fileNames.begin(), fileNames.end());

    ImGui::BeginChild("##LightFileList", ImVec2(0.0f, 92.0f), true);
    if (fileNames.empty()) {
        ImGui::TextDisabled("ライト設定ファイルがありません");
    }
    for (const auto& name : fileNames) {
        const bool selected = (name == currentFileName_);
        if (ImGui::Selectable(name.c_str(), selected)) {
            strncpy_s(currentFileName_, sizeof(currentFileName_), name.c_str(), _TRUNCATE);
            syncedLightPath_ = currentPath;
        }
    }
    ImGui::EndChild();
#endif
}

void LightEditor::DrawSkyboxTextureList() {
#ifdef USE_IMGUI
    if (!lightManager_) {
        return;
    }

    std::vector<std::string> texturePaths;
    const std::filesystem::path resourceDir("Resources");
    if (std::filesystem::exists(resourceDir)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(resourceDir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".dds") {
                continue;
            }
            texturePaths.push_back(entry.path().generic_string());
        }
    }
    std::sort(texturePaths.begin(), texturePaths.end());

    bool enabled = lightManager_->IsSkyboxEnabled();
    if (ImGui::Checkbox(ICON_FA_CLOUD_SUN " スカイボックスを表示", &enabled)) {
        lightManager_->SetSkyboxEnabled(enabled);
    }

    const std::string& currentTexture = lightManager_->GetSkyboxTexturePath();
    if (ImGui::BeginCombo(ICON_FA_IMAGE " スカイボックスDDS", currentTexture.c_str())) {
        for (const auto& path : texturePaths) {
            const bool selected = (path == currentTexture);
            if (ImGui::Selectable(path.c_str(), selected)) {
                const bool applied = lightManager_->SetSkyboxTexturePath(path);
                SetStatusMessage(applied ? "スカイボックスを差し替えました" : "スカイボックスの差し替えに失敗しました", applied);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::TextDisabled("ライト設定JSONにON/OFFとDDSパスを保存します。");
#endif
}

void LightEditor::Initialize() {
    lightManager_ = LightManager::GetInstance();
}

void LightEditor::SetObject3dCommon(Object3dCommon* common) {
    common_ = common;
    pointLightGizmos_.clear();
    spotLightGizmos_.clear();
}

void LightEditor::Update() {
    if (!lightManager_ || !common_) return;

    // ==========================================================
    // 1. 点光源 (Point Lights) のギズモ同期
    // ==========================================================
    auto& pointLights = lightManager_->GetPointLights(); // 今や vector<Instance>

    // ギズモの数を合わせる
    if (pointLightGizmos_.size() != pointLights.size()) {
        pointLightGizmos_.resize(pointLights.size());
        for (size_t i = 0; i < pointLightGizmos_.size(); ++i) {
            if (!pointLightGizmos_[i]) {
                pointLightGizmos_[i] = std::make_unique<Object3d>();
                pointLightGizmos_[i]->Initialize(common_);
                pointLightGizmos_[i]->SetModel("Primitives/sphere");
                pointLightGizmos_[i]->SetMaterialType(0);
            }
        }
    }

    // 位置情報をコピー
    for (size_t i = 0; i < pointLights.size(); ++i) {
        if (pointLightGizmos_[i]) {

            pointLightGizmos_[i]->SetTranslate(pointLights[i].data.position);
            pointLightGizmos_[i]->SetScale({ gizmoScale_, gizmoScale_, gizmoScale_ });

            Vector4 color = pointLights[i].data.color;
            color.x = color.x < 0.35f ? 0.35f : color.x;
            color.y = color.y < 0.35f ? 0.35f : color.y;
            color.z = color.z < 0.1f ? 0.1f : color.z;
            color.w = 0.9f;
            pointLightGizmos_[i]->SetColor(color);
            pointLightGizmos_[i]->SetEmissive(2.5f);

            pointLightGizmos_[i]->Update(0.0f);
            pointLightGizmos_[i]->UpdateLocalMatrix();
            pointLightGizmos_[i]->UpdateWorldMatrix();
        }
    }

    // ==========================================================
    // 2. スポットライト (Spot Lights) のギズモ同期
    // ==========================================================
    auto& spotLights = lightManager_->GetSpotLights();

    if (spotLightGizmos_.size() != spotLights.size()) {
        spotLightGizmos_.resize(spotLights.size());
        for (size_t i = 0; i < spotLightGizmos_.size(); ++i) {
            if (!spotLightGizmos_[i]) {
                spotLightGizmos_[i] = std::make_unique<Object3d>();
                spotLightGizmos_[i]->Initialize(common_);
                spotLightGizmos_[i]->SetModel("Primitives/sphere");
                spotLightGizmos_[i]->SetMaterialType(0);
            }
        }
    }

    for (size_t i = 0; i < spotLights.size(); ++i) {
        if (spotLightGizmos_[i]) {
            // スポットライト位置をギズモに反映
            spotLightGizmos_[i]->SetTranslate(spotLights[i].data.position);
            spotLightGizmos_[i]->SetScale({ gizmoScale_ * 0.85f, gizmoScale_ * 1.35f, gizmoScale_ * 0.85f });
            Vector4 color = spotLights[i].data.color;
            color.x = color.x < 0.15f ? 0.15f : color.x;
            color.y = color.y < 0.55f ? 0.55f : color.y;
            color.z = color.z < 0.85f ? 0.85f : color.z;
            color.w = 0.9f;
            spotLightGizmos_[i]->SetColor(color);
            spotLightGizmos_[i]->SetEmissive(2.5f);

            // 向きに合わせて回転させると完璧だが、今回は省略
            spotLightGizmos_[i]->Update(0.0f);
            spotLightGizmos_[i]->UpdateLocalMatrix();
            spotLightGizmos_[i]->UpdateWorldMatrix();
        }
    }
}

void LightEditor::Draw3D() {
    if (!isVisibleGizmos_ || !lightManager_) return;

    ID3D12Resource* plRes = lightManager_->GetPointLightResource();
    ID3D12Resource* slRes = lightManager_->GetSpotLightResource();

    for (auto& gizmo : pointLightGizmos_) {
        if (gizmo) gizmo->Draw(plRes, slRes);
    }
    for (auto& gizmo : spotLightGizmos_) {
        if (gizmo) gizmo->Draw(plRes, slRes);
    }
}

void LightEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (!lightManager_) return;

    ImGui::Checkbox(ICON_FA_EYE " ライト位置を表示 (Gizmos)", &isVisibleGizmos_);
    ImGui::DragFloat(ICON_FA_EXPAND_ARROWS_ALT " ギズモサイズ", &gizmoScale_, 0.01f, 0.2f, 2.0f);
    ImGui::Separator();
    ImGui::Spacing();

    Vector4& clearColor = lightManager_->GetSceneClearColor();
    if (ImGui::ColorEdit4(ICON_FA_PALETTE " 背景色 (Clear Color)", &clearColor.x)) {
        DirectXCommon::GetInstance()->SetRenderClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
    }
    ImGui::Spacing();

    if (ImGui::CollapsingHeader(ICON_FA_CLOUD_SUN " 環境設定 (Environment)", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawSkyboxTextureList();
    }
    ImGui::Spacing();

    // -------------------------------------------------------------
    // 1. ファイル操作
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " ファイル管理 (File I/O)", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawLightFileList();
        ImGui::Spacing();

        ImGui::InputText(ICON_FA_FILE_CODE " ファイル名 (.json)", currentFileName_, sizeof(currentFileName_));
        std::string fullPath = BuildFullPathFromFileName();

        if (ImGui::Button(ICON_FA_DOWNLOAD " セーブ (Save)")) {
            const bool saved = lightManager_->SaveState(fullPath);
            syncedLightPath_.clear();
            SetStatusMessage(saved ? "ライト設定を保存しました" : "ライト設定の保存に失敗しました", saved);
            DebugConsole::GetInstance()->AddLog(saved ? "Light Editor: saved " + fullPath : "Light Editor: save failed " + fullPath);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " ロード (Load)")) {
            const bool loaded = lightManager_->LoadState(fullPath);
            syncedLightPath_.clear();
            SetStatusMessage(loaded ? "ライト設定をロードしました" : "ライト設定のロードに失敗しました", loaded);
            DebugConsole::GetInstance()->AddLog(loaded ? "Light Editor: loaded " + fullPath : "Light Editor: load failed " + fullPath);
        }
        ImGui::TextDisabled("ターゲットパス: %s", fullPath.c_str());
        if (!statusMessage_.empty() && ImGui::GetTime() < statusVisibleUntil_) {
            const ImVec4 statusColor = statusSuccess_
                ? ImVec4(0.35f, 1.0f, 0.55f, 1.0f)
                : ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
            ImGui::TextColored(statusColor, "%s", statusMessage_.c_str());
        }
    }

    ImGui::Separator();

    // -------------------------------------------------------------
    // 2. 太陽設定 (Directional Light)
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader(ICON_FA_SUN " 太陽設定 (Directional Light)", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& sun = lightManager_->GetDirectionalLight();

        ImGui::Text(ICON_FA_LOCATION_ARROW " 光の向き");
        ImGui::DragFloat3("##Dir", &sun.direction.x, 0.01f, -1.0f, 1.0f);

        ImGui::ColorEdit4(ICON_FA_PALETTE " 光の色", &sun.color.x);
        ImGui::DragFloat(ICON_FA_SUN " 輝度", &sun.intensity, 0.01f, 0.0f, 5.0f);

        ImGui::Separator();
        ImGui::ColorEdit3(ICON_FA_CLOUD " 環境光 (Ambient)", &sun.ambientColor.x);
        if (ImGui::Button(ICON_FA_MAGIC " 明るい影プリセット")) {
            sun.color = { 1.0f, 0.96f, 0.88f, 1.0f };
            sun.direction = { -0.35f, -0.82f, 0.45f };
            sun.intensity = 0.86f;
            sun.ambientColor = { 0.34f, 0.38f, 0.42f };
            sun.fogColor = { 0.66f, 0.76f, 0.86f };
            clearColor = { 0.52f, 0.68f, 0.84f, 1.0f };
            DirectXCommon::GetInstance()->SetRenderClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
            SetStatusMessage("明るい影プリセットを適用しました", true);
        }

        ImGui::Separator();
        bool isFogEnabled = (sun.enableFog != 0);
        if (ImGui::Checkbox(ICON_FA_SMOG " フォグ関連をすべて有効化 (Enable All Fog)", &isFogEnabled)) {
            sun.enableFog = isFogEnabled ? 1 : 0;
        }

        if (isFogEnabled) {
            ImGui::Text(ICON_FA_CLOUD " フォグ (Fog)");
            ImGui::DragFloat(" 開始距離", &sun.fogStart, 1.0f, 0.0f, 5000.0f);
            ImGui::DragFloat(" 終了距離", &sun.fogEnd, 1.0f, 0.0f, 5000.0f);
            ImGui::ColorEdit3(" フォグ色", &sun.fogColor.x);
            ImGui::DragFloat(" 高さの底", &sun.fogHeightMin, 0.1f, -100.0f, 100.0f);
            ImGui::DragFloat(" 晴れる高さ", &sun.fogHeightMax, 0.1f, -100.0f, 100.0f);

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), ICON_FA_MAGIC " ボリューメトリックフォグ (God Rays)");
            ImGui::DragFloat(" 強さ (Intensity)", &sun.volumetricIntensity, 0.01f, 0.0f, 5.0f);
            ImGui::DragInt(" 精密さ (Steps)", &sun.volumetricSteps, 1, 0, 64);
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), ICON_FA_GLOBE " 環境マップ (Environment Map / IBL)");
    }

    ImGui::Separator();

    // -------------------------------------------------------------
    // 3. 点光源設定 (Point Light)
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader(ICON_FA_LIGHTBULB " 点光源 (Point Light)")) {
        if (ImGui::Button(ICON_FA_PLUS " 追加##Point")) lightManager_->AddPointLight();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH_ALT " 全削除##Point")) {
            lightManager_->GetPointLights().clear();
            selectedPointLightIndex_ = -1;
        }

        auto& pointLights = lightManager_->GetPointLights();
        if (selectedPointLightIndex_ >= static_cast<int>(pointLights.size())) {
            selectedPointLightIndex_ = -1;
        }

        ImGui::TextDisabled("点光源: %d", static_cast<int>(pointLights.size()));
        ImGui::BeginChild("##PointLightList", ImVec2(0.0f, 92.0f), true);
        for (int i = 0; i < static_cast<int>(pointLights.size()); ++i) {
            const auto& data = pointLights[i].data;
            std::string label = pointLights[i].name + "  (" +
                std::to_string(static_cast<int>(data.position.x)) + ", " +
                std::to_string(static_cast<int>(data.position.y)) + ", " +
                std::to_string(static_cast<int>(data.position.z)) + ")";
            if (ImGui::Selectable(label.c_str(), selectedPointLightIndex_ == i)) {
                selectedPointLightIndex_ = i;
            }
        }
        ImGui::EndChild();

        for (int i = 0; i < pointLights.size(); ++i) {
            ImGui::PushID(i);
            auto& instance = pointLights[i];
            auto& data = instance.data;

            if (selectedPointLightIndex_ == i) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            if (ImGui::TreeNode("点光源", ICON_FA_LIGHTBULB " Point Light %d", i)) {
                char nameBuffer[128]{};
                strncpy_s(nameBuffer, sizeof(nameBuffer), instance.name.c_str(), _TRUNCATE);
                if (ImGui::InputText(ICON_FA_TAG " 名前", nameBuffer, sizeof(nameBuffer))) {
                    instance.name = nameBuffer;
                }

                if (ImGui::Button(ICON_FA_TRASH " 削除")) {
                    pointLights.erase(pointLights.begin() + i);
                    selectedPointLightIndex_ = -1;
                    ImGui::TreePop();
                    ImGui::PopID();
                    continue;
                }

                ImGui::Text(ICON_FA_COGS " --- 挙動設定 ---");
                std::string currentTargetName = instance.target ? instance.target->GetName() : "(None)";
                if (ImGui::BeginCombo(ICON_FA_LINK " 追従対象", currentTargetName.c_str())) {
                    BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene();
                    if (scene) {
                        if (ImGui::Selectable("(None)", instance.target == nullptr)) instance.target = nullptr;
                        for (auto& obj : scene->GetObjects()) {
                            bool isSelected = (instance.target == obj.get());
                            std::string name = obj->GetName().empty() ? "Object" : obj->GetName();
                            if (ImGui::Selectable(name.c_str(), isSelected)) instance.target = obj.get();
                        }
                    }
                    ImGui::EndCombo();
                }

                const char* modes[] = { "通常", "追従のみ(Follow)", "点滅(Flicker)", "明滅(Sine)" };
                int currentMode = (int)instance.mode;
                if (ImGui::Combo(ICON_FA_PLAY_CIRCLE " モード", &currentMode, modes, IM_ARRAYSIZE(modes))) {
                    instance.mode = (LightManager::LightMode)currentMode;
                }

                if (instance.target) {
                    ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 位置ズレ (Offset)", &instance.offset.x, 0.1f);
                }
                else {
                    ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 位置 (Position)", &data.position.x, 0.1f);
                }

                ImGui::DragFloat(ICON_FA_SUN " 基準の明るさ", &instance.baseIntensity, 0.01f, 0.0f, 10.0f);

                ImGui::Text(ICON_FA_ADJUST " --- 基本設定 ---");
                ImGui::ColorEdit4(" 色", &data.color.x);
                ImGui::DragFloat(" 半径", &data.radius, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat(" 減衰", &data.decay, 0.01f, 0.0f, 10.0f);

                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    ImGui::Separator();

    // -------------------------------------------------------------
    // 4. スポットライト設定 (Spot Light)
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader(ICON_FA_STREET_VIEW " スポットライト (Spot Light)")) {
        if (ImGui::Button(ICON_FA_PLUS " 追加##Spot")) lightManager_->AddSpotLight();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH_ALT " 全削除##Spot")) {
            lightManager_->GetSpotLights().clear();
            selectedSpotLightIndex_ = -1;
        }

        auto& spotLights = lightManager_->GetSpotLights();
        if (selectedSpotLightIndex_ >= static_cast<int>(spotLights.size())) {
            selectedSpotLightIndex_ = -1;
        }

        ImGui::TextDisabled("スポットライト: %d", static_cast<int>(spotLights.size()));
        ImGui::BeginChild("##SpotLightList", ImVec2(0.0f, 92.0f), true);
        for (int i = 0; i < static_cast<int>(spotLights.size()); ++i) {
            const auto& data = spotLights[i].data;
            std::string label = spotLights[i].name + "  (" +
                std::to_string(static_cast<int>(data.position.x)) + ", " +
                std::to_string(static_cast<int>(data.position.y)) + ", " +
                std::to_string(static_cast<int>(data.position.z)) + ")";
            if (ImGui::Selectable(label.c_str(), selectedSpotLightIndex_ == i)) {
                selectedSpotLightIndex_ = i;
            }
        }
        ImGui::EndChild();

        for (int i = 0; i < spotLights.size(); ++i) {
            ImGui::PushID(i + 1000);
            auto& instance = spotLights[i];
            auto& data = instance.data;

            if (selectedSpotLightIndex_ == i) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            if (ImGui::TreeNode("スポットライト", ICON_FA_STREET_VIEW " Spot Light %d", i)) {
                char nameBuffer[128]{};
                strncpy_s(nameBuffer, sizeof(nameBuffer), instance.name.c_str(), _TRUNCATE);
                if (ImGui::InputText(ICON_FA_TAG " 名前", nameBuffer, sizeof(nameBuffer))) {
                    instance.name = nameBuffer;
                }

                if (ImGui::Button(ICON_FA_TRASH " 削除")) {
                    spotLights.erase(spotLights.begin() + i);
                    selectedSpotLightIndex_ = -1;
                    ImGui::TreePop();
                    ImGui::PopID();
                    continue;
                }

                ImGui::Text(ICON_FA_COGS " --- 挙動設定 ---");
                // (PointLightと同様の追従対象Combo)
                // ... (中略) ...

                if (instance.target) {
                    ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 位置ズレ (Offset)", &instance.offset.x, 0.1f);
                }
                else {
                    ImGui::DragFloat3(ICON_FA_ARROWS_ALT " 位置", &data.position.x, 0.1f);
                    ImGui::DragFloat3(ICON_FA_LOCATION_ARROW " 向き", &data.direction.x, 0.01f, -1.0f, 1.0f);
                }

                ImGui::DragFloat(ICON_FA_SUN " 基準の明るさ", &instance.baseIntensity, 0.01f, 0.0f, 10.0f);

                ImGui::Text(ICON_FA_ADJUST " --- 基本設定 ---");
                ImGui::ColorEdit4(" 色", &data.color.x);
                ImGui::DragFloat(" 距離", &data.distance, 0.1f, 0.0f, 100.0f);

                float angleDeg = std::acos(data.cosAngle) * 180.0f / 3.141592f;
                if (ImGui::DragFloat(ICON_FA_COMPASS " 角度", &angleDeg, 1.0f, 0.1f, 179.0f)) {
                    data.cosAngle = std::cos(angleDeg * 3.141592f / 180.0f);
                }

                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
#endif
}
