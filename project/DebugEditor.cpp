// [新規作成] DebugEditor.cpp
// ★★★ windows.h の min/max マクロ競合を回避するため、必ずファイルの先頭に置く ★★★
#define NOMINMAX

#include "DebugEditor.h"
#include "engine/scene/GamePlayScene.h" // (シーンのパスに合わせてください)
#include "engine/3d/Object3d.h"
#include "externals/imgui/imgui.h"

// --- JSON (保存機能) ---
#include <fstream>
#include <string>
#include "externals/nlohmann/json.hpp" // (配置したパスに合わせてください)

// --- ImGuizmo (3Dギズモ) ---
#include "externals/ImGuizmo/ImGuizmo.h" // (配置したパスに合わせてください)
#include "engine/3d/CameraManager.h"   // カメラ行列
#include "engine/base/WinApp.h"        // ウィンドウサイズ
#include "engine/base/Math.h"          // 行列計算

// --- 角度変換ヘルパー ---
#include <cmath> // M_PI が定義されていない場合のため
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const float PI = (float)M_PI;

/// <summary>
/// 度をラジアンに変換
/// </summary>
float ToRadians(float degrees) {
    return degrees * (PI / 180.0f);
}

/// <summary>
/// ラジアンを度に変換
/// </summary>
float ToDegrees(float radians) {
    return radians * (180.0f / PI);
}
// --- 角度変換ヘルパーここまで ---


void DebugEditor::Initialize(GamePlayScene* scene) {
    scene_ = scene;
    selectedObject_ = nullptr;
}

void DebugEditor::Update() {
    // ★ using 宣言は必ず関数の「内側」に書く
    using json = nlohmann::json;

    // --- ImGuizmoのフレーム開始 ---
    // (ImGui::BeginFrame の後、ImGuizmo::Manipulate の前に呼ぶ)
    ImGuizmo::BeginFrame();

    // --- シーン存在チェック ---
    if (scene_ == nullptr) {
        return;
    }

    // --- 1. オブジェクトリスト ウィンドウ ---
    ImGui::Begin("Object List");

    // GetObjects() でオブジェクト一覧を取得
    std::vector<std::unique_ptr<Object3d>>& objects = scene_->GetObjects();

    for (auto& obj : objects) {
        // オブジェクトの名前を取得
        const std::string& objName = obj->GetName();
        if (objName.empty()) { continue; } // 名前がないオブジェクトは無視

        // オブジェクト名をクリックで選択
        if (ImGui::Selectable(objName.c_str(), obj.get() == selectedObject_)) {
            selectedObject_ = obj.get();
        }
    }
    ImGui::End(); // Object List

    // --- 2. インスペクター ウィンドウ ---
    ImGui::Begin("Inspector");

    if (selectedObject_) {
        // --- トランスフォームの表示・編集 ---
        ImGui::Text("Name: %s", selectedObject_->GetName().c_str());
        Object3d::Transform* transform = selectedObject_->GetTransform();

        // Position
        ImGui::DragFloat3("Position", &transform->translate.x, 0.1f);

        // Rotation (★ 高速回転対策：ラジアン <-> 度 の変換)
        Vector3 rotationDegrees = {
            ToDegrees(transform->rotate.x),
            ToDegrees(transform->rotate.y),
            ToDegrees(transform->rotate.z)
        };
        // ImGuiでは「度」を操作
        if (ImGui::DragFloat3("Rotation (Degrees)", &rotationDegrees.x, 1.0f, -360.0f, 360.0f)) {
            // 変更された「度」を「ラジアン」に戻して格納
            transform->rotate.x = ToRadians(rotationDegrees.x);
            transform->rotate.y = ToRadians(rotationDegrees.y);
            transform->rotate.z = ToRadians(rotationDegrees.z);
        }

        // Scale
        ImGui::DragFloat3("Scale", &transform->scale.x, 0.05f);

        // --- 保存ボタン ---
        ImGui::Separator();
        if (ImGui::Button("Save Scene Layout")) {
            json sceneData;
            sceneData["objects"] = json::array();
            std::vector<std::unique_ptr<Object3d>>& allObjects = scene_->GetObjects();

            for (auto& obj : allObjects) {
                if (obj->GetName().empty()) { continue; } // 名前がないものは保存しない

                Object3d::Transform* objTransform = obj->GetTransform();
                json objData;
                objData["name"] = obj->GetName(); // 名前をキーにする
                objData["position"] = { objTransform->translate.x, objTransform->translate.y, objTransform->translate.z };
                objData["rotation"] = { objTransform->rotate.x, objTransform->rotate.y, objTransform->rotate.z }; // ラジアンで保存
                objData["scale"] = { objTransform->scale.x, objTransform->scale.y, objTransform->scale.z };
                sceneData["objects"].push_back(objData);
            }
            // 実行ファイルと同じ階層に "scene_layout.json" という名前で保存
            std::ofstream file("scene_layout.json");
            file << sceneData.dump(4); // 4スペースでインデント
            file.close();
        }

    } else {
        ImGui::Text("No object selected.");
    }

    // (Inspectorウィンドウはギズモセクションの後で閉じる)

    // --- 3. 3Dギズモの描画 ---
    if (selectedObject_) {
        // --- ギズモの操作設定 ---
        static ImGuizmo::OPERATION currentOperation = ImGuizmo::TRANSLATE; // 平行移動
        static ImGuizmo::MODE currentMode = ImGuizmo::WORLD; // ワールド座標系

        // --- スナップ用の変数 ---
        static float snapTranslate[3] = { 0.1f, 0.1f, 0.1f }; // 10cm単位
        static float snapRotation = 15.0f; // 15度単位
        static float snapScale = 0.1f;     // 10%単位

        // 操作モードの切り替え (T/R/Sキー)
        if (ImGui::IsKeyPressed(ImGuiKey_T)) { currentOperation = ImGuizmo::TRANSLATE; }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) { currentOperation = ImGuizmo::ROTATE; }
        if (ImGui::IsKeyPressed(ImGuiKey_S)) { currentOperation = ImGuizmo::SCALE; }

        // Inspectorウィンドウにギズモの操作UIを追加
        ImGui::Separator();
        ImGui::Text("Gizmo Mode:");
        if (ImGui::RadioButton("Translate", currentOperation == ImGuizmo::TRANSLATE)) { currentOperation = ImGuizmo::TRANSLATE; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", currentOperation == ImGuizmo::ROTATE)) { currentOperation = ImGuizmo::ROTATE; }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", currentOperation == ImGuizmo::SCALE)) { currentOperation = ImGuizmo::SCALE; }

        // スナップ設定のUI
        if (currentOperation == ImGuizmo::TRANSLATE) {
            ImGui::InputFloat3("Snap Translate", snapTranslate, "%.2f");
        } else if (currentOperation == ImGuizmo::ROTATE) {
            ImGui::InputFloat("Snap Angle (Degrees)", &snapRotation, 1.0f, 5.0f);
        } else if (currentOperation == ImGuizmo::SCALE) {
            ImGui::InputFloat("Snap Scale", &snapScale, 0.01f, 0.1f);
        }
        ImGui::Text("Hold [Left Ctrl] to snap.");


        // --- 1. カメラ行列の取得 ---
        const Camera* camera = CameraManager::GetInstance()->GetMainCamera();
        const Matrix4x4& viewMatrix = camera->GetViewMatrix();
        const Matrix4x4& projectionMatrix = camera->GetProjectionMatrix();

        // --- 2. 対象オブジェクトのワールド行列の作成 ---
        Object3d::Transform* transform = selectedObject_->GetTransform();
        Math math;
        Matrix4x4 worldMatrix = math.MakeAffineMatrix(transform->scale, transform->rotate, transform->translate);

        // --- 3. ImGuizmoのセットアップ (ビューポートいっぱいに) ---
        ImGuiIO& io = ImGui::GetIO();
        ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

        // --- 4. スナップ引数の準備 ---
        float snapValues[3];
        if (currentOperation == ImGuizmo::ROTATE) {
            snapValues[0] = snapValues[1] = snapValues[2] = snapRotation;
        } else if (currentOperation == ImGuizmo::SCALE) {
            snapValues[0] = snapValues[1] = snapValues[2] = snapScale;
        } else { // Translate
            snapValues[0] = snapTranslate[0];
            snapValues[1] = snapTranslate[1];
            snapValues[2] = snapTranslate[2];
        }
        // Ctrlキー押下中のみスナップを有効にする
        float* snap = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ? snapValues : nullptr;

        // --- 5. ギズモの描画と操作 ---
        ImGuizmo::Manipulate(
            &viewMatrix.m[0][0],        // View Matrix
            &projectionMatrix.m[0][0],  // Projection Matrix
            currentOperation,           // 操作モード
            currentMode,                // 座標系
            &worldMatrix.m[0][0],       // ★ 操作対象のワールド行列 (in/out)
            nullptr,                    // deltaMatrix (差分)
            snap                        // ★ スナップ設定
        );

        // --- 6. ギズモが操作されたか？ ---
        if (ImGuizmo::IsUsing()) {
            // ギズモによって変更されたワールド行列を、TRSに分解
            Vector3 newScale, newRotationDegrees, newTranslation;
            ImGuizmo::DecomposeMatrixToComponents(
                &worldMatrix.m[0][0],
                &newTranslation.x,
                &newRotationDegrees.x, // ★ ImGuizmoは「度」を返す
                &newScale.x
            );

            // オブジェクトのTransformに書き戻す
            transform->translate = newTranslation;
            // ★ 高速回転対策：「度」を「ラジアン」に変換
            transform->rotate.x = ToRadians(newRotationDegrees.x);
            transform->rotate.y = ToRadians(newRotationDegrees.y);
            transform->rotate.z = ToRadians(newRotationDegrees.z);
            transform->scale = newScale;
        }
    }

    ImGui::End(); // Inspectorウィンドウを閉じる
}

void DebugEditor::Finalize() {
    // 今は特に何もしない
}