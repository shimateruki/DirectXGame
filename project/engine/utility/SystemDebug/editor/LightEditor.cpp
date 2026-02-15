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

    if (ImGui::Begin("ライト編集")) {
        ImGui::Checkbox("ライト位置を表示 (Gizmos)", &isVisibleGizmos_);
        ImGui::Separator();

        // 1. ファイル操作
        ImGui::Text("ファイル設定");
        ImGui::InputText("ファイル名 (.json)", currentFileName_, sizeof(currentFileName_));
        std::string fullPath = "Resources/json/light/" + std::string(currentFileName_);

        if (ImGui::Button("セーブ")) lightManager_->SaveState(fullPath);
        ImGui::SameLine();
        if (ImGui::Button("ロード")) lightManager_->LoadState(fullPath);
        ImGui::TextDisabled("ターゲットパス: %s", fullPath.c_str());

        ImGui::Separator();

        // 2. 太陽設定
        if (ImGui::CollapsingHeader("太陽設定 (Directional Light)", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& sun = lightManager_->GetDirectionalLight();
            ImGui::Text("光の向き");
            ImGui::DragFloat3("##Dir", &sun.direction.x, 0.01f, -1.0f, 1.0f);
            if (ImGui::Button("向きを正規化")) {
                // Updateで正規化されるのでそのままでOK
            }
            ImGui::ColorEdit4("光の色", &sun.color.x);
            ImGui::DragFloat("輝度", &sun.intensity, 0.01f, 0.0f, 5.0f);

            ImGui::Separator();
            ImGui::ColorEdit3("環境光 (Ambient)", &sun.ambientColor.x);

            ImGui::Separator();
            ImGui::Text("フォグ (Fog)");
            ImGui::DragFloat("開始距離", &sun.fogStart, 1.0f, 0.0f, 5000.0f);
            ImGui::DragFloat("終了距離", &sun.fogEnd, 1.0f, 0.0f, 5000.0f);
            ImGui::ColorEdit3("フォグ色", &sun.fogColor.x);
        }

        ImGui::Separator();

        // 3. 点光源設定 (ここが大きく変わる)
        if (ImGui::CollapsingHeader("点光源")) {
            if (ImGui::Button("追加")) lightManager_->AddPointLight();
            ImGui::SameLine();
            if (ImGui::Button("全削除")) lightManager_->GetPointLights().clear();

            auto& pointLights = lightManager_->GetPointLights();
            for (int i = 0; i < pointLights.size(); ++i) {
                ImGui::PushID(i);

                // インスタンスへの参照
                auto& instance = pointLights[i];
                auto& data = instance.data; // GPUデータへのショートカット

                if (ImGui::TreeNode("点光源", "Point Light %d", i)) {
                    if (ImGui::Button("削除")) {
                        pointLights.erase(pointLights.begin() + i);
                        ImGui::TreePop();
                        ImGui::PopID();
                        continue;
                    }

                    // --- ターゲット追従設定 ---
                    ImGui::Text("--- 挙動設定 ---");

                    // 追従対象の選択
                    std::string currentTargetName = instance.target ? instance.target->GetName() : "(None)";
                    if (ImGui::BeginCombo("追従対象", currentTargetName.c_str())) {
                        BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene();
                        if (scene) {
                            if (ImGui::Selectable("(None)", instance.target == nullptr)) {
                                instance.target = nullptr;
                            }
                            // シーン内の全オブジェクトを列挙
                            for (auto& obj : scene->GetObjects()) {
                                bool isSelected = (instance.target == obj.get());
                                // 名前がないオブジェクトは "Object" と表示するなど工夫しても良い
                                std::string name = obj->GetName().empty() ? "Object" : obj->GetName();
                                if (ImGui::Selectable(name.c_str(), isSelected)) {
                                    instance.target = obj.get();
                                }
                                if (isSelected) ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    // モード選択
                    const char* modes[] = { "通常", "追従のみ(Follow)", "点滅(Flicker)", "明滅(Sine)" };
                    int currentMode = (int)instance.mode;
                    if (ImGui::Combo("モード", &currentMode, modes, IM_ARRAYSIZE(modes))) {
                        instance.mode = (LightManager::LightMode)currentMode;
                    }

                    // パラメータ分岐
                    if (instance.target) {
                        ImGui::DragFloat3("位置ズレ (Offset)", &instance.offset.x, 0.1f);
                    } else {
                        ImGui::DragFloat3("位置 (Position)", &data.position.x, 0.1f);
                    }

                    if (instance.mode == LightManager::LightMode::Flicker || instance.mode == LightManager::LightMode::SineWave) {
                        ImGui::DragFloat("アニメ速度", &instance.speed, 0.1f, 0.0f, 20.0f);
                        ImGui::DragFloat("基準の明るさ", &instance.baseIntensity, 0.01f, 0.0f, 10.0f);
                        ImGui::TextDisabled("現在はアニメーション制御されています");
                    } else {
                        // 通常時は baseIntensity を直接いじるか、data.intensity をいじるか
                        // ここでは baseIntensity をいじる形に統一すると楽
                        if (ImGui::DragFloat("輝度", &instance.baseIntensity, 0.01f, 0.0f, 10.0f)) {
                            data.intensity = instance.baseIntensity;
                        }
                    }

                    ImGui::Text("--- 基本設定 ---");
                    ImGui::ColorEdit4("色", &data.color.x);
                    ImGui::DragFloat("半径", &data.radius, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat("減衰", &data.decay, 0.01f, 0.0f, 10.0f);

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        // 4. スポットライト設定
        if (ImGui::CollapsingHeader("スポットライト")) {
            if (ImGui::Button("追加")) lightManager_->AddSpotLight();
            ImGui::SameLine();
            if (ImGui::Button("全削除")) lightManager_->GetSpotLights().clear();

            auto& spotLights = lightManager_->GetSpotLights();
            for (int i = 0; i < spotLights.size(); ++i) {
                ImGui::PushID(i + 1000);
                auto& instance = spotLights[i];
                auto& data = instance.data;

                if (ImGui::TreeNode("スポットライト", "Spot Light %d", i)) {
                    if (ImGui::Button("削除")) {
                        spotLights.erase(spotLights.begin() + i);
                        ImGui::TreePop();
                        ImGui::PopID();
                        continue;
                    }

                    // --- ターゲット追従 ---
                    ImGui::Text("--- 挙動設定 ---");
                    std::string currentTargetName = instance.target ? instance.target->GetName() : "(None)";
                    if (ImGui::BeginCombo("追従対象", currentTargetName.c_str())) {
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

                    const char* modes[] = { "通常", "追従/懐中電灯", "点滅", "明滅" };
                    int currentMode = (int)instance.mode;
                    if (ImGui::Combo("モード", &currentMode, modes, IM_ARRAYSIZE(modes))) {
                        instance.mode = (LightManager::LightMode)currentMode;
                    }

                    if (instance.target) {
                        ImGui::DragFloat3("位置ズレ (Offset)", &instance.offset.x, 0.1f);
                        ImGui::TextDisabled("向きは対象の正面にロックされています");
                    } else {
                        ImGui::DragFloat3("位置", &data.position.x, 0.1f);
                        ImGui::DragFloat3("向き", &data.direction.x, 0.01f, -1.0f, 1.0f);
                    }

                    if (instance.mode == LightManager::LightMode::Flicker || instance.mode == LightManager::LightMode::SineWave) {
                        ImGui::DragFloat("アニメ速度", &instance.speed, 0.1f, 0.0f, 20.0f);
                        ImGui::DragFloat("基準の明るさ", &instance.baseIntensity, 0.01f, 0.0f, 10.0f);
                    } else {
                        if (ImGui::DragFloat("輝度", &instance.baseIntensity, 0.01f, 0.0f, 10.0f)) {
                            data.intensity = instance.baseIntensity;
                        }
                    }

                    ImGui::Text("--- 基本設定 ---");
                    ImGui::ColorEdit4("色", &data.color.x);
                    ImGui::DragFloat("距離", &data.distance, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat("減衰", &data.decay, 0.01f, 0.0f, 10.0f);

                    float angleDeg = std::acos(data.cosAngle) * 180.0f / 3.141592f;
                    float falloffDeg = std::acos(data.cosFalloffStart) * 180.0f / 3.141592f;
                    bool changed = false;
                    if (ImGui::DragFloat("角度", &angleDeg, 1.0f, 0.1f, 179.0f)) changed = true;
                    if (ImGui::DragFloat("減衰角", &falloffDeg, 1.0f, 0.1f, 179.0f)) changed = true;
                    if (changed) {
                        if (falloffDeg > angleDeg) falloffDeg = angleDeg;
                        data.cosAngle = std::cos(angleDeg * 3.141592f / 180.0f);
                        data.cosFalloffStart = std::cos(falloffDeg * 3.141592f / 180.0f);
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