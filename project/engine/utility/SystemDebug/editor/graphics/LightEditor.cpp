#include "LightEditor.h"
#include "imgui.h"
#include "json.hpp"
#include <fstream>
#include <cmath>
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "imgui_internal.h"
#include "CameraManager.h"
#include "Camera.h"
#include "SceneManager.h" 
#include "IconsFontAwesome5.h"
using json = nlohmann::json;

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

LightEditor* LightEditor::GetInstance() {
    static LightEditor instance;
    return &instance;
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
                pointLightGizmos_[i]->SetModel("block");
                pointLightGizmos_[i]->SetScale({ 0.5f, 0.5f, 0.5f });
            }
        }
    }

    // 位置情報をコピー
    for (size_t i = 0; i < pointLights.size(); ++i) {
        if (pointLightGizmos_[i]) {

            pointLightGizmos_[i]->SetTranslate(pointLights[i].data.position);

            // ついでに色も反映 (Object3d側に対応メソッドがあれば)
            // pointLightGizmos_[i]->SetColor(pointLights[i].data.color);

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
                spotLightGizmos_[i]->SetModel("block");
                spotLightGizmos_[i]->SetScale({ 0.5f, 0.5f, 0.5f });
            }
        }
    }

    for (size_t i = 0; i < spotLights.size(); ++i) {
        if (spotLightGizmos_[i]) {
            // ★修正: .data 経由でアクセス
            spotLightGizmos_[i]->SetTranslate(spotLights[i].data.position);

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
    ImGui::Separator();
    ImGui::Spacing();

    // -------------------------------------------------------------
    // 1. ファイル操作
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " ファイル管理 (File I/O)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText(ICON_FA_FILE_CODE " ファイル名 (.json)", currentFileName_, sizeof(currentFileName_));
        std::string fullPath = "Resources/json/light/" + std::string(currentFileName_);

        if (ImGui::Button(ICON_FA_DOWNLOAD " セーブ (Save)")) lightManager_->SaveState(fullPath);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " ロード (Load)")) lightManager_->LoadState(fullPath);
        ImGui::TextDisabled("ターゲットパス: %s", fullPath.c_str());
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
        if (ImGui::Button(ICON_FA_TRASH_ALT " 全削除##Point")) lightManager_->GetPointLights().clear();

        auto& pointLights = lightManager_->GetPointLights();
        for (int i = 0; i < pointLights.size(); ++i) {
            ImGui::PushID(i);
            auto& instance = pointLights[i];
            auto& data = instance.data;

            if (ImGui::TreeNode("点光源", ICON_FA_LIGHTBULB " Point Light %d", i)) {
                if (ImGui::Button(ICON_FA_TRASH " 削除")) {
                    pointLights.erase(pointLights.begin() + i);
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
        if (ImGui::Button(ICON_FA_TRASH_ALT " 全削除##Spot")) lightManager_->GetSpotLights().clear();

        auto& spotLights = lightManager_->GetSpotLights();
        for (int i = 0; i < spotLights.size(); ++i) {
            ImGui::PushID(i + 1000);
            auto& instance = spotLights[i];
            auto& data = instance.data;

            if (ImGui::TreeNode("スポットライト", ICON_FA_STREET_VIEW " Spot Light %d", i)) {
                if (ImGui::Button(ICON_FA_TRASH " 削除")) {
                    spotLights.erase(spotLights.begin() + i);
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