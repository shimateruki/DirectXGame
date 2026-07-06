#include "HierarchyWindow.h"
#include "DebugEditor.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3d.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include "IconsFontAwesome5.h"
#include "EditorManager.h"
#include "CameraEditor.h"
#include "PostEffectEditor.h"
#include "SpriteDebugEditor.h"
#include "GPUParticleEditor.h"
#include "VFXSequencerEditor.h"
#include "ParticleEditor.h"
#include "GhostRecorder.h"
#include "GhostDirector.h"
#include "LightEditor.h"
#include "ModelManager.h"
#include "DebugConsole.h"
#include "PresetManager.h"
#include "KeyConfig.h"
#include "MeshEffectEditor.h"
#include "json.hpp"
#include "TrailEmitterEditor.h"
#include <filesystem>
#include "../../../PathUtility.h"
#include <algorithm> // std::transform用
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static const float PI = (float)M_PI;
static float ToRadians(float degrees) { return degrees * (PI / 180.0f); }
static float ToDegrees(float radians) { return radians * (180.0f / PI); }

namespace fs = std::filesystem;

void HierarchyWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
}



void HierarchyWindow::Draw() {
#ifdef USE_IMGUI
    // プロジェクトウィンドウ (Asset Browserなど) の描画
    editor_->DrawProjectWindow();

    if (editor_->GetSceneManager() == nullptr) return;
    BaseScene* currentScene = editor_->GetSceneManager()->GetCurrentScene();
    if (currentScene == nullptr) return;

    ImGui::Begin(ICON_FA_SITEMAP " Hierarchy###Hierarchy");


    if (ImGui::CollapsingHeader(ICON_FA_COGS " システム設定 (System Settings)", ImGuiTreeNodeFlags_DefaultOpen)) {
        IEditable* currentObj = EditorManager::GetInstance()->GetSelectedObject();

        if (ImGui::Selectable("  " ICON_FA_VIDEO " カメラ設定 (Camera)", currentObj == CameraEditor::GetInstance())) {
            editor_->SetSelectedObject(nullptr); EditorManager::GetInstance()->SetSelectedObject(CameraEditor::GetInstance());
        }
        if (editor_->GetLightEditor() && ImGui::Selectable("  " ICON_FA_LIGHTBULB " ライティング設定 (Lighting)", currentObj == editor_->GetLightEditor())) {
            editor_->SetSelectedObject(nullptr); EditorManager::GetInstance()->SetSelectedObject(editor_->GetLightEditor());
        }
        if (editor_->GetPostEffectEditor() && ImGui::Selectable("  " ICON_FA_MAGIC " ポストエフェクト (Post Effect)", currentObj == editor_->GetPostEffectEditor())) {
            editor_->SetSelectedObject(nullptr); EditorManager::GetInstance()->SetSelectedObject(editor_->GetPostEffectEditor());
        }
        if (ImGui::Selectable("  " ICON_FA_KEYBOARD " キーコンフィグ (Key Config)", currentObj == KeyConfig::GetInstance())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(KeyConfig::GetInstance());
        }
        if (editor_->GetGPUParticleEditor() && ImGui::Selectable("  " ICON_FA_FIRE " GPUパーティクル (GPU Particle)", currentObj == editor_->GetGPUParticleEditor())) {
            editor_->SetSelectedObject(nullptr); EditorManager::GetInstance()->SetSelectedObject(editor_->GetGPUParticleEditor());
        }
        if (editor_->GetVFXSequencerEditor() && ImGui::Selectable("  " ICON_FA_FILM " VFXシーケンサー (VFX Sequencer)", currentObj == editor_->GetVFXSequencerEditor())) {
            editor_->SetSelectedObject(nullptr); EditorManager::GetInstance()->SetSelectedObject(editor_->GetVFXSequencerEditor());
        }
        if (editor_->GetParticleEditor() && ImGui::Selectable("  " ICON_FA_WIND " 通常パーティクル (Particle)", currentObj == editor_->GetParticleEditor())) {
            editor_->SetSelectedObject(nullptr); EditorManager::GetInstance()->SetSelectedObject(editor_->GetParticleEditor());
        }
        if (editor_->GetGhostRecorder() && ImGui::Selectable("  " ICON_FA_GHOST " ゴーストレコーダー (Ghost Recorder)", currentObj == editor_->GetGhostRecorder())) {
            if (editor_->GetSelectedObject()) editor_->GetGhostRecorder()->SetTarget(editor_->GetSelectedObject());
            editor_->SetSelectedObject(nullptr); EditorManager::GetInstance()->SetSelectedObject(editor_->GetGhostRecorder());
        }
        if (editor_->GetGhostDirector() && ImGui::Selectable("  " ICON_FA_BULLHORN " ゴーストディレクター (Ghost Director)", currentObj == editor_->GetGhostDirector())) {
            editor_->SetSelectedObject(nullptr); EditorManager::GetInstance()->SetSelectedObject(editor_->GetGhostDirector());
        }
        if (editor_->GetMeshEffectEditor() && ImGui::Selectable("  " ICON_FA_MAGIC " メッシュエフェクト (Mesh Effect)", currentObj == editor_->GetMeshEffectEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetMeshEffectEditor());
        }
        if (editor_->GetTrailEmitterEditor() && ImGui::Selectable("  " ICON_FA_FIRE " トレイルエミッター (Trail Emitter)", currentObj == editor_->GetTrailEmitterEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetTrailEmitterEditor());
        }
        ImGui::Separator();
        if (ImGui::Selectable("  " ICON_FA_GAMEPAD " ゲーム設定 (Game Settings)", currentObj == currentScene)) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(currentScene);
        }
    }

    ImGui::Separator();
    std::string currentJsonPath = "Resources/json/3Dobject/" + std::string(editor_->GetCurrentSceneFilenameBuffer());
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " シーンファイル管理 (Scene File)", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string directoryPath = "Resources/json/3Dobject/";
        const auto sceneDirectoryPath = cg2::path::FromUtf8(directoryPath);
        if (!cg2::path::Exists(sceneDirectoryPath)) cg2::path::CreateDirectories(sceneDirectoryPath);

        if (ImGui::BeginCombo(ICON_FA_FOLDER_OPEN " 既存ファイル", editor_->GetCurrentSceneFilenameBuffer())) {
            if (cg2::path::Exists(sceneDirectoryPath)) {
                for (const auto& entry : fs::directory_iterator(sceneDirectoryPath, cg2::path::SafeDirectoryOptions())) {
                    if (cg2::path::IsRegularFile(entry) && cg2::path::ExtensionLower(entry.path()) == ".json") {
                        std::string filename = cg2::path::ToUtf8String(entry.path().filename());
                        if (filename.find("_player.json") != std::string::npos || filename.find("_enemy.json") != std::string::npos || filename.find("_object.json") != std::string::npos) continue;
                        bool isSelected = (std::string(editor_->GetCurrentSceneFilenameBuffer()) == filename);
                        if (ImGui::Selectable(filename.c_str(), isSelected)) {
                            strcpy_s(editor_->GetCurrentSceneFilenameBuffer(), editor_->GetSceneFilenameBufferSize(), filename.c_str());
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText(ICON_FA_FILE_SIGNATURE " 保存名 (.json)", editor_->GetCurrentSceneFilenameBuffer(), editor_->GetSceneFilenameBufferSize());
        ImGui::Text(ICON_FA_FILTER " 個別保存 (競合回避用):");
        if (ImGui::Button(ICON_FA_USER " Playerのみ保存")) editor_->SaveScene(SaveMode::Player);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SKULL " Enemyのみ保存")) editor_->SaveScene(SaveMode::Enemy);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CUBE " Objectのみ保存")) editor_->SaveScene(SaveMode::Object);
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_DOWNLOAD " シーン全体保存 (All)", ImVec2(-1, 0))) editor_->SaveScene(SaveMode::All);
        ImGui::TextDisabled("保存先: %s", currentJsonPath.c_str());
    }

    ImGui::Separator();
    ImGui::Separator();

    // =======================================================
    //  検索バーとカテゴリフィルタを横に並べて表示！
    // =======================================================
    ImGui::Text(ICON_FA_SEARCH " 検索:");
    ImGui::SameLine();
    ImGui::PushItemWidth(120.0f); // 検索バーの幅
    ImGui::InputText("##Search", editor_->GetSearchFilterBuffer(), editor_->GetSearchFilterBufferSize());
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::Text(ICON_FA_FILTER " 分類:");
    ImGui::SameLine();
    ImGui::PushItemWidth(100.0f); // フィルタの幅
    const char* filterNames[] = { "All", "Player", "Enemy", "Object" };
    ImGui::Combo("##CategoryFilter", &currentCategoryFilter_, filterNames, IM_ARRAYSIZE(filterNames));
    ImGui::PopItemWidth();
    ImGui::Separator();

    std::string filterStr = editor_->GetSearchFilterBuffer();
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);


    if (!filterStr.empty()) {
        ImGui::TextColored(ImVec4(0, 1, 1, 1), ICON_FA_SEARCH_PLUS " 検索結果:");
        auto& objects = currentScene->GetObjects();
        for (auto& obj : objects) {
            std::string name = obj->GetName();
            if (name.empty()) continue;
            std::string nameLower = name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (currentCategoryFilter_ != 0) {
                std::string cat = obj->GetSaveCategory();
                if (currentCategoryFilter_ == 1 && cat != "Player") continue;
                if (currentCategoryFilter_ == 2 && cat != "Enemy") continue;
                if (currentCategoryFilter_ == 3 && cat != "Object") continue;
            }
            if (nameLower.find(filterStr) != std::string::npos) {
                bool isSelected = (editor_->GetSelectedObject() == obj.get());
                ImGui::PushID(obj.get());
                if (ImGui::Selectable(name.c_str(), isSelected)) {
                    editor_->SetSelectedObject(obj.get());
                    EditorManager::GetInstance()->SetSelectedObject(editor_);
                }
                ImGui::PopID();
            }
        }
    }
    else {
        ImGui::Button(ICON_FA_BOX_OPEN " [ ここにモデルをドロップして生成 ]", ImVec2(-1, 30));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                const char* modelName = (const char*)payload->Data;
                ModelManager::GetInstance()->LoadModel(modelName);
                Object3dCommon* common = currentScene->GetObject3dCommon();
                if (common) {
                    auto newObj = std::make_unique<Object3d>();
                    newObj->Initialize(common); newObj->SetModel(modelName); newObj->SetClassName("Model"); newObj->SetName("Preview_" + std::string(modelName));
                    newObj->UpdateLocalMatrix(); newObj->UpdateWorldMatrix();
                    editor_->SetPreviewObject(std::move(newObj)); // ★ セッター経由で渡す！
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
                const char* presetName = (const char*)payload->Data;
                const auto& presets = PresetManager::GetInstance()->GetPresets();
                if (presets.count(presetName) > 0) {
                    const nlohmann::json& data = presets.at(presetName);
                    std::string modelName = "cube.obj";
                    if (data.contains("modelName")) { modelName = data["modelName"]; ModelManager::GetInstance()->LoadModel(modelName); }
                    Object3dCommon* common = currentScene->GetObject3dCommon();
                    if (common) {
                        auto newObj = std::make_unique<Object3d>();
                        newObj->Initialize(common); newObj->ImportFromJson(data); newObj->SetModel(modelName); newObj->SetName("Preview_" + std::string(presetName));
                        newObj->UpdateLocalMatrix(); newObj->UpdateWorldMatrix();
                        editor_->SetPreviewObject(std::move(newObj)); // ★ セッター経由で渡す！
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CINEMATIC_CAMERA_ASSET")) {
                Object3dCommon* common = currentScene ? currentScene->GetObject3dCommon() : nullptr;
                if (common) {
                    auto newObj = std::make_unique<Object3d>();
                    newObj->Initialize(common); newObj->SetModel("block"); newObj->SetName("Camera_Cinematic"); newObj->SetClassName("CinematicCamera");
                    newObj->SetTranslate({ 0.0f, 5.0f, -10.0f }); newObj->UpdateLocalMatrix(); newObj->UpdateWorldMatrix();
                    editor_->SetSelectedObject(newObj.get());
                    currentScene->AddObject(std::move(newObj));
                    EditorManager::GetInstance()->SetSelectedObject(editor_);
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::Separator();
        auto& objects = currentScene->GetObjects();
        for (auto& obj : objects) {
            if (obj->GetParent() == nullptr) {
                DrawHierarchyNode(obj.get());
            }
        }
    }

    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.8f, 0.6f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.8f, 0.7f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.8f, 0.8f, 0.8f));

    if (ImGui::Button(ICON_FA_BOLT " 透明ボックス生成 (トリガー用)", ImVec2(-1, 40))) {
        Object3dCommon* common = currentScene->GetObject3dCommon();
        if (common) {
            auto newObj = std::make_unique<Object3d>();
            newObj->Initialize(common); newObj->SetModel(nullptr); newObj->SetIsVisible(true); newObj->SetClassName("InvisibleBox"); newObj->SetName("Trigger_Box");
            Object3d::ColliderConfig colConfig; colConfig.type = ColliderType::kAABB; colConfig.size = { 1.0f, 1.0f, 1.0f };
            newObj->SetColliderConfig(colConfig); newObj->SetCollisionAttribute(CollisionAttribute::kTrigger); newObj->SetTranslate({ 0, 2.0f, 0 });
            editor_->SetSelectedObject(newObj.get()); currentScene->AddObject(std::move(newObj)); EditorManager::GetInstance()->SetSelectedObject(editor_);
        }
    }
    if (ImGui::Button(ICON_FA_SHIELD_ALT " 透明ボックス生成 (当たり判定用)", ImVec2(-1, 40))) {
        Object3dCommon* common = currentScene->GetObject3dCommon();
        if (common) {
            auto newObj = std::make_unique<Object3d>();
            newObj->Initialize(common); newObj->SetModel(nullptr);newObj->SetIsVisible(true); newObj->SetClassName("InvisibleBox"); newObj->SetName("collision_Box");
            Object3d::ColliderConfig colConfig; colConfig.type = ColliderType::kAABB; colConfig.size = { 1.0f, 1.0f, 1.0f };
            newObj->SetColliderConfig(colConfig); newObj->SetCollisionAttribute(CollisionAttribute::kGround); newObj->SetTranslate({ 0, 2.0f, 0 });
            editor_->SetSelectedObject(newObj.get()); currentScene->AddObject(std::move(newObj)); EditorManager::GetInstance()->SetSelectedObject(editor_);
        }
    }
    if (ImGui::Button(ICON_FA_VIDEO " 演出用カメラ生成 (Cinematic)", ImVec2(-1, 40))) {
        Object3dCommon* common = currentScene->GetObject3dCommon();
        if (common) {
            auto newObj = std::make_unique<Object3d>();
            newObj->Initialize(common); newObj->SetModel("block"); newObj->SetColor({ 0.8f, 0.2f, 0.8f, 1.0f }); newObj->SetIsVisible(true); newObj->SetClassName("CinematicCamera"); newObj->SetName("Cinematic_Camera_01");
            Object3d::ColliderConfig colConfig; colConfig.type = ColliderType::kAABB; colConfig.size = { 1.0f, 1.0f, 1.0f };
            newObj->SetColliderConfig(colConfig); newObj->SetTranslate({ 0, 5.0f, -10.0f }); newObj->UpdateWorldMatrix();
            editor_->SetSelectedObject(newObj.get()); currentScene->AddObject(std::move(newObj)); EditorManager::GetInstance()->SetSelectedObject(editor_);
        }
    }

    ImGui::PopStyleColor(3);
    ImGui::Dummy(ImVec2(0, 50));
    ImGui::TextDisabled(ICON_FA_UNLINK " (ここにドロップして親解除)");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_OBJ")) {
            Object3d* sourceObj = *(Object3d**)payload->Data;
            if (sourceObj->GetParent() != nullptr) {
                Matrix4x4 worldMat = sourceObj->GetWorldMatrix();
                sourceObj->SetParent(nullptr);
                Vector3 t, rDeg, s;
                ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], &t.x, &rDeg.x, &s.x);
                sourceObj->GetTransform()->translate = t;
                sourceObj->GetTransform()->rotate = { ToRadians(rDeg.x), ToRadians(rDeg.y), ToRadians(rDeg.z) };
                sourceObj->GetTransform()->scale = s;
                sourceObj->UpdateWorldMatrix();
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::End();
#endif
}

void HierarchyWindow::DrawHierarchyNode(Object3d* obj) {
#ifdef USE_IMGUI
    if (!obj) return;
    if (!HasMatchingCategory(obj)) return;
    //  IDの衝突を防ぐためにPushIDを使用
    ImGui::PushID(obj);

    //  AllowItemOverlapを追加して、ツリーと同じ行にボタンを押せるようにする
    ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (editor_->GetSelectedObject() == obj) node_flags |= ImGuiTreeNodeFlags_Selected;

    std::string name = obj->GetName();
    if (name.empty()) name = "NoName";
    if (obj->GetClassName() == "InvisibleBox") name = "[Trigger] " + name;

    // 左側にツリーノードを描画
    bool node_open = ImGui::TreeNodeEx((void*)obj, node_flags, name.c_str());

    // アイテムがクリックされたら選択状態にする
    if (ImGui::IsItemClicked()) {
        editor_->SetSelectedObject(obj);
        EditorManager::GetInstance()->SetSelectedObject(editor_);
    }

    // --- ドラッグ＆ドロップ処理 ---
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("HIERARCHY_OBJ", &obj, sizeof(Object3d*));
        ImGui::Text("Move %s", name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_OBJ")) {
            Object3d* sourceObj = *(Object3d**)payload->Data;
            if (sourceObj != obj && sourceObj->GetParent() != obj) {
                sourceObj->SetParent(obj);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // ==========================================================
    //  右端に目玉と南京錠のアイコンを配置する
    // ==========================================================
    ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f); // 右端から75pxの位置に寄せる

    // 1. 表示・非表示トグル (目のアイコン)
    bool isVisible = obj->GetIsVisible();
    const char* eyeIcon = isVisible ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
    if (!isVisible) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // 非表示時はグレーアウト
    if (ImGui::Button(eyeIcon)) {
        obj->SetIsVisible(!isVisible);
    }
    if (!isVisible) ImGui::PopStyleColor();

    ImGui::SameLine();

    // 2. ロックトグル (南京錠アイコン)
    bool isLocked = obj->GetIsLocked();
    const char* lockIcon = isLocked ? ICON_FA_LOCK : ICON_FA_UNLOCK;
    if (isLocked) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); // ロック時は赤色
    if (ImGui::Button(lockIcon)) {
        obj->SetIsLocked(!isLocked);
    }
    if (isLocked) ImGui::PopStyleColor();

    if (node_open) {
        if (editor_->GetSceneManager() && editor_->GetSceneManager()->GetCurrentScene()) {
            auto& allObjs = editor_->GetSceneManager()->GetCurrentScene()->GetObjects();
            for (auto& child : allObjs) {
                if (child->GetParent() == obj) DrawHierarchyNode(child.get());
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopID(); // PushIDの解除
#endif
}

bool HierarchyWindow::HasMatchingCategory(Object3d* obj) {
    if (currentCategoryFilter_ == 0) return true; // Allなら無条件でパス
    if (!obj) return false;

    std::string cat = obj->GetSaveCategory();
    if (currentCategoryFilter_ == 1 && cat == "Player") return true;
    if (currentCategoryFilter_ == 2 && cat == "Enemy") return true;
    if (currentCategoryFilter_ == 3 && cat == "Object") return true;

    // 子要素も再帰的にチェック（親がObjectでも子がEnemyなら表示を許可する！）
    for (Object3d* child : obj->GetChildren()) {
        if (HasMatchingCategory(child)) return true;
    }

    return false;
}
