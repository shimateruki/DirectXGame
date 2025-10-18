
#include "DebugEditor.h"
#include "engine/scene/GamePlayScene.h" 
#include "engine/3d/Object3d.h"       
#include "externals/imgui/imgui.h"  
#include <fstream>                  // ファイル入出力
#include <string>                   // std::string
#include "externals/nlohmann/json.hpp"


void DebugEditor::Initialize(GamePlayScene* scene) {
    scene_ = scene;
    selectedObject_ = nullptr;
}

void DebugEditor::Update() {
    using json = nlohmann::json;
    // シーンがなければ何もしない
    if (scene_ == nullptr) {
        return;
    }

    // --- 1. オブジェクトリスト ---
    ImGui::Begin("Object List");

    // GetObjects() でオブジェクト一覧を取得 (ステップ1で実装)
    std::vector<std::unique_ptr<Object3d>>& objects = scene_->GetObjects();

    for (auto& obj : objects) {
        // オブジェクトの名前を取得 (ステップ1で実装)
        const std::string& objName = obj->GetName();

        // オブジェクト名をクリックで選択
        // (選択中なら true が返る)
        if (ImGui::Selectable(objName.c_str(), obj.get() == selectedObject_)) {
            selectedObject_ = obj.get();
        }
    }
    ImGui::End(); // Object List

    // --- 2. インスペクター (オブジェクト詳細) ---
    ImGui::Begin("Inspector");

    // オブジェクトが選択されている場合
    if (selectedObject_) {
        // (変更なし: 名前表示, Transform操作のDragFloat3)
        ImGui::Text("Name: %s", selectedObject_->GetName().c_str());
        Object3d::Transform* transform = selectedObject_->GetTransform();
        ImGui::DragFloat3("Position", &transform->translate.x, 0.1f);
        ImGui::DragFloat3("Rotation", &transform->rotate.x, 0.01f);
        ImGui::DragFloat3("Scale", &transform->scale.x, 0.05f);

        // --- ★★★ 「Save Scene」ボタンの追加 ★★★ ---
        ImGui::Separator(); // 区切り線
        if (ImGui::Button("Save Scene Layout")) {

            // 1. JSON オブジェクトを構築
            json sceneData;
            sceneData["objects"] = json::array(); // "objects" という名前のJSON配列を作成

            // 2. シーン内の全オブジェクトをループ
            std::vector<std::unique_ptr<Object3d>>& objects = scene_->GetObjects();
            for (auto& obj : objects) {
                Object3d::Transform* transform = obj->GetTransform();

                // 3. オブジェクトの情報をJSONに詰める
                json objData;
                objData["name"] = obj->GetName(); // ★名前をキーにする
                objData["position"] = {
                    transform->translate.x,
                    transform->translate.y,
                    transform->translate.z
                };
                objData["rotation"] = {
                    transform->rotate.x,
                    transform->rotate.y,
                    transform->rotate.z
                };
                objData["scale"] = {
                    transform->scale.x,
                    transform->scale.y,
                    transform->scale.z
                };

                // 4. "objects" 配列にデータを追加
                sceneData["objects"].push_back(objData);
            }

            // 5. ファイルに書き出し
            // (実行ファイルと同じ階層に "scene_layout.json" という名前で保存)
            std::ofstream file("scene_layout.json");
            file << sceneData.dump(4); // 4スペースでインデントして保存
            file.close();
        }
        // --- ★★★ 追加ここまで ★★★ ---
    } else {
        ImGui::Text("No object selected.");
    }

    ImGui::End(); // Inspectorr
}

void DebugEditor::Finalize() {
    // 今は特に何もしない
}