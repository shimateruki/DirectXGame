#include "LightEditor.h"
#include "imgui.h"
#include "json.hpp"
#include <fstream>
#include <cmath> // std::acos, std::cos
#include "DirectXCommon.h" // 必要なら
#include <ModelManager.h>

using json = nlohmann::json;
LightEditor* LightEditor::GetInstance() {
    static LightEditor instance;
    return &instance;
}

void LightEditor::Initialize() {
    lightManager_ = LightManager::GetInstance();

}

//  シーンからcommonをもらって、ギズモを準備する
void LightEditor::SetObject3dCommon(Object3dCommon* common) {
    common_ = common;


    pointLightGizmos_.clear();
    spotLightGizmos_.clear();
}



void LightEditor::Update() {
    if (!lightManager_ || !common_) return;

    // ==========================================================
    // 1. 点光源 (Point Lights) の同期
    // ==========================================================
    auto& pointLights = lightManager_->GetPointLights();

    // ライトの数に合わせてObject3dの数を調整
    if (pointLightGizmos_.size() != pointLights.size()) {
        pointLightGizmos_.resize(pointLights.size());
        for (size_t i = 0; i < pointLightGizmos_.size(); ++i) {
            if (!pointLightGizmos_[i]) {
                pointLightGizmos_[i] = std::make_unique<Object3d>();
                pointLightGizmos_[i]->Initialize(common_);

            
                pointLightGizmos_[i]->SetModel("block");

                // ライトと分かるように少し小さくする
                pointLightGizmos_[i]->SetScale({ 0.5f, 0.5f, 0.5f });
            }
        }
    }

    // 位置情報をライトからコピー
    for (size_t i = 0; i < pointLights.size(); ++i) {
        if (pointLightGizmos_[i]) {
            pointLightGizmos_[i]->SetTranslate(pointLights[i].position);
            // 色も反映させると分かりやすい (Object3dにSetColorがあれば)
            // pointLightGizmos_[i]->SetColor(pointLights[i].color);

            pointLightGizmos_[i]->Update(0.0f); // 行列更新
			pointLightGizmos_[i]->UpdateLocalMatrix();
			pointLightGizmos_[i]->UpdateWorldMatrix();
        }
    }

    // ==========================================================
    // 2. スポットライト (Spot Lights) の同期
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
            spotLightGizmos_[i]->SetTranslate(spotLights[i].position);
            spotLightGizmos_[i]->Update(0.0f);
			spotLightGizmos_[i]->UpdateLocalMatrix();
			spotLightGizmos_[i]->UpdateWorldMatrix();
        }
    }
}

void LightEditor::Draw3D() {
    if (!isVisibleGizmos_ || !lightManager_) return;

    // LightManagerからリソースを取得
    ID3D12Resource* plRes = lightManager_->GetPointLightResource();
    ID3D12Resource* slRes = lightManager_->GetSpotLightResource();

    // 点光源ギズモ描画
    for (auto& gizmo : pointLightGizmos_) {
        if (gizmo) {
            // ここでリソースを渡す！
            gizmo->Draw(plRes, slRes);
        }
    }

    // スポットライトギズモ描画
    for (auto& gizmo : spotLightGizmos_) {
        if (gizmo) {
            gizmo->Draw(plRes, slRes);
        }
    }
}


void LightEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (!lightManager_) return;

    if (ImGui::Begin("ライト編集")) {
        ImGui::Checkbox("ライト位置を表示 (Gizmos)", &isVisibleGizmos_);
        ImGui::Separator();

        // ==========================================================
        // 1. ファイル保存・読み込みエリア
        // ==========================================================
        ImGui::Text("ファイル設定");

        // ファイル名入力 
        ImGui::InputText("ファイル名 (.json)", currentFileName_, sizeof(currentFileName_));

        // フルパスを作成
        std::string fullPath = "Resources/json/light/" + std::string(currentFileName_);

        // 保存・読み込みボタン (Managerに委譲)
        if (ImGui::Button("セーブ")) {
            lightManager_->SaveState(fullPath);
        }
        ImGui::SameLine();
        if (ImGui::Button("ロード")) {
            lightManager_->LoadState(fullPath);
        }

        // 現在のパスを表示（確認用）
        ImGui::TextDisabled("ターゲットパス: %s", fullPath.c_str());

        ImGui::Separator();

        // ==========================================================
        // 2. 点光源 (Point Lights) リスト
        // ==========================================================
        if (ImGui::CollapsingHeader("点光源")) {
            if (ImGui::Button("点光源追加")) {
                lightManager_->AddPointLight();
            }
            ImGui::SameLine();
            if (ImGui::Button("点光源削除")) {
                lightManager_->GetPointLights().clear();
            }

            auto& pointLights = lightManager_->GetPointLights();
            for (int i = 0; i < pointLights.size(); ++i) {
                ImGui::PushID(i); // ID被り防止
                if (ImGui::TreeNode("点光源", "点光源 %d", i)) {
                    // 削除ボタン
                    if (ImGui::Button("削除")) {
                        pointLights.erase(pointLights.begin() + i);
                        ImGui::TreePop();
                        ImGui::PopID();
                        continue; // 要素削除後はループを抜ける
                    }

                    // パラメータ調整
                    ImGui::DragFloat3("位置", &pointLights[i].position.x, 0.1f);
                    ImGui::ColorEdit4("色", &pointLights[i].color.x);
                    ImGui::DragFloat("明るさ", &pointLights[i].intensity, 0.01f, 0.0f, 10.0f);
                    ImGui::DragFloat("半径", &pointLights[i].radius, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat("減衰", &pointLights[i].decay, 0.01f, 0.0f, 10.0f);

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        // ==========================================================
        // 3. スポットライト (Spot Lights) リスト
        // ==========================================================
        if (ImGui::CollapsingHeader("スポットライト")) {
            if (ImGui::Button("スポットライト追加")) {
                auto l = lightManager_->AddSpotLight();
                // 追加時のデフォルト角度設定
                if (l) {
                    l->cosAngle = std::cos(45.0f * 3.141592f / 180.0f);
                    l->cosFalloffStart = std::cos(30.0f * 3.141592f / 180.0f);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("スポットライト削除")) {
                lightManager_->GetSpotLights().clear();
            }

            auto& spotLights = lightManager_->GetSpotLights();
            for (int i = 0; i < spotLights.size(); ++i) {
                ImGui::PushID(i + 1000); // PointLightとIDが被らないようにオフセット
                if (ImGui::TreeNode("スポットライト", "スポットライト %d", i)) {
                    // 削除ボタン
                    if (ImGui::Button("削除")) {
                        spotLights.erase(spotLights.begin() + i);
                        ImGui::TreePop();
                        ImGui::PopID();
                        continue;
                    }

                    // パラメータ調整
                    ImGui::DragFloat3("位置", &spotLights[i].position.x, 0.1f);
                    ImGui::DragFloat3("向き", &spotLights[i].direction.x, 0.01f, -1.0f, 1.0f);
                    ImGui::ColorEdit4("色", &spotLights[i].color.x);
                    ImGui::DragFloat("強度", &spotLights[i].intensity, 0.01f, 0.0f, 10.0f);
                    ImGui::DragFloat("距離", &spotLights[i].distance, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat("減衰", &spotLights[i].decay, 0.01f, 0.0f, 10.0f);

                    // 角度操作 (Radian/Cos <-> Degree 変換)
                    float currentAngleDeg = std::acos(spotLights[i].cosAngle) * 180.0f / 3.141592f;
                    float currentFalloffDeg = std::acos(spotLights[i].cosFalloffStart) * 180.0f / 3.141592f;

                    bool changed = false;
                    if (ImGui::DragFloat("角度", &currentAngleDeg, 1.0f, 0.1f, 179.0f)) changed = true;
                    if (ImGui::DragFloat("減衰角", &currentFalloffDeg, 1.0f, 0.1f, 179.0f)) changed = true;

                    if (changed) {
                        // FalloffがAngleより大きくならないように補正
                        if (currentFalloffDeg > currentAngleDeg) currentFalloffDeg = currentAngleDeg;
                        // 計算してCos値に戻す
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

