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
#include "CameraManager.h"
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
#include "PresetEditor.h"
#include "KeyConfig.h"
#include "MeshEffectEditor.h"
#include "SceneController.h"
#include "DebrisEffectEditor.h"
#include "TrailEmitterEditor.h"
#include "GimmickFactory.h"
#include "EnemyFactory.h"
#include "ItemFactory.h"
#include "json.hpp"
#include <filesystem>
#include <cctype>
#include <algorithm> // std::transform用
#include <cmath>
#include <vector>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static const float PI = (float)M_PI;
static float ToRadians(float degrees) { return degrees * (PI / 180.0f); }
static float ToDegrees(float radians) { return radians * (180.0f / PI); }

static std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

static bool ContainsLower(const std::string& text, const std::string& lowerNeedle) {
    return ToLowerAscii(text).find(lowerNeedle) != std::string::npos;
}

static bool MatchesHierarchySearch(Object3d* object, const std::string& lowerFilter) {
    if (!object) {
        return false;
    }
    if (lowerFilter.empty()) {
        return true;
    }

    return ContainsLower(object->GetName(), lowerFilter) ||
        ContainsLower(object->GetClassName(), lowerFilter) ||
        ContainsLower(object->GetSaveCategory(), lowerFilter) ||
        ContainsLower(object->GetTag(), lowerFilter) ||
        ContainsLower(object->GetLayer(), lowerFilter);
}

namespace fs = std::filesystem;

namespace {
    constexpr const char* kCinematicCameraModel = "Editor/camera_gizmo";

    std::string MakeUniqueName(BaseScene* scene, const std::string& baseName) {
        if (!scene) return baseName;

        auto exists = [&](const std::string& name) {
            for (const auto& obj : scene->GetObjects()) {
                if (obj && obj->GetName() == name) return true;
            }
            return false;
        };

        if (!exists(baseName)) return baseName;

        for (int index = 1; index < 10000; ++index) {
            std::string candidate = baseName + "_" + std::to_string(index);
            if (!exists(candidate)) return candidate;
        }
        return baseName + "_New";
    }

    std::string GetLeafName(const std::string& path) {
        size_t slash = path.find_last_of("/\\");
        if (slash == std::string::npos) {
            return path.empty() ? "Preset" : path;
        }
        std::string leaf = path.substr(slash + 1);
        return leaf.empty() ? "Preset" : leaf;
    }

    bool ReservedHasName(const std::vector<std::string>& reservedNames, const std::string& name) {
        return std::find(reservedNames.begin(), reservedNames.end(), name) != reservedNames.end();
    }

    std::string MakeUniquePresetName(BaseScene* scene, const std::string& baseName, const std::vector<std::string>& reservedNames) {
        std::string base = baseName.empty() ? "PresetObject" : GetLeafName(baseName);
        std::string uniqueName = MakeUniqueName(scene, base);
        if (!ReservedHasName(reservedNames, uniqueName)) {
            return uniqueName;
        }

        for (int index = 1; index < 10000; ++index) {
            std::string candidate = base + "_" + std::to_string(index);
            if (!ReservedHasName(reservedNames, candidate) && MakeUniqueName(scene, candidate) == candidate) {
                return candidate;
            }
        }
        return base + "_New";
    }

    void AssignPresetInstanceNames(BaseScene* scene, const std::string& presetName, std::vector<std::unique_ptr<Object3d>>& objects) {
        if (objects.empty()) return;

        std::vector<std::string> reservedNames;
        std::string rootName = MakeUniquePresetName(scene, GetLeafName(presetName), reservedNames);
        objects.front()->SetName(rootName);
        reservedNames.push_back(rootName);

        for (size_t index = 1; index < objects.size(); ++index) {
            if (!objects[index]) continue;

            std::string baseName = objects[index]->GetName();
            if (baseName.empty()) {
                baseName = rootName + "_Child";
            }
            std::string uniqueName = MakeUniquePresetName(scene, baseName, reservedNames);
            objects[index]->SetName(uniqueName);
            reservedNames.push_back(uniqueName);
        }
    }

    float GetObjectYOffset(const Object3d* object) {
        if (!object) return 1.0f;

        Object3d::ColliderConfig colConfig = object->GetColliderConfig();
        if (colConfig.size.y > 0.0f) return colConfig.size.y;
        if (object->GetTransform().scale.y > 0.0f) return object->GetTransform().scale.y;
        return 1.0f;
    }

    Vector3 CalculateCameraForward(const Vector3& rotation) {
        return {
            std::sin(rotation.y) * std::cos(rotation.x),
            -std::sin(rotation.x),
            std::cos(rotation.y) * std::cos(rotation.x)
        };
    }

    Vector3 GetDefaultCreatePosition(DebugEditor* editor, const Object3d* object) {
        if (!editor) return { 0.0f, 1.0f, 0.0f };

        Object3d* selected = editor->GetSelectedObject();
        if (selected) {
            Matrix4x4 world = selected->GetWorldMatrix();
            return { world.m[3][0] + 2.0f, world.m[3][1], world.m[3][2] };
        }

        Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
        if (!camera) {
            return { 0.0f, GetObjectYOffset(object), 0.0f };
        }

        Vector3 eye = camera->GetEye();
        Vector3 forward = CalculateCameraForward(camera->GetRotation());
        Vector3 position = {
            eye.x + forward.x * 10.0f,
            eye.y + forward.y * 10.0f,
            eye.z + forward.z * 10.0f
        };

        // カメラが床方向を向いている時は、画面中央の床付近に生成する
        if (std::abs(forward.y) > 0.0001f) {
            float t = (0.0f - eye.y) / forward.y;
            if (t > 0.0f && t < 80.0f) {
                position = {
                    eye.x + forward.x * t,
                    0.0f,
                    eye.z + forward.z * t
                };
            }
        }

        position.y += GetObjectYOffset(object);
        return position;
    }

    void AddCreatedObject(DebugEditor* editor, BaseScene* scene, std::unique_ptr<Object3d> object, const std::string& baseName, const std::string& commandLabel, bool useGameViewCursor) {
        if (!editor || !scene || !object) return;

        object->SetName(MakeUniqueName(scene, baseName));
        if (useGameViewCursor) {
            editor->StartGameViewCreatePreview(std::move(object), commandLabel);
            return;
        }

        Vector3 createPosition = useGameViewCursor
            ? editor->CalculateGameViewCreatePosition(object.get())
            : GetDefaultCreatePosition(editor, object.get());
        object->SetTranslate(createPosition);
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();

        std::string createdName = object->GetName();
        editor->AddEditorObject(std::move(object), commandLabel);
        DebugConsole::GetInstance()->AddLog("Create: " + createdName);
    }

    void AddCreatedObjects(DebugEditor* editor, BaseScene* scene, std::vector<std::unique_ptr<Object3d>> objects, const std::string& baseName, const std::string& commandLabel, bool useGameViewCursor) {
        if (!editor || !scene || objects.empty()) return;

        AssignPresetInstanceNames(scene, baseName, objects);
        Object3d* rootObject = objects.front().get();
        if (!rootObject) return;

        if (useGameViewCursor) {
            editor->StartGameViewCreatePreview(std::move(objects), commandLabel);
            return;
        }

        Vector3 createPosition = GetDefaultCreatePosition(editor, rootObject);
        rootObject->SetTranslate(createPosition);
        rootObject->UpdateLocalMatrix();
        rootObject->UpdateWorldMatrix();

        std::string createdName = rootObject->GetName();
        editor->AddEditorObjects(std::move(objects), commandLabel);
        DebugConsole::GetInstance()->AddLog("Create: " + createdName);
    }

    void CreatePrimitive(DebugEditor* editor, BaseScene* scene, const std::string& modelName, const std::string& baseName, bool useGameViewCursor, const Vector3& scale = { 1.0f, 1.0f, 1.0f }) {
        if (!scene || !scene->GetObject3dCommon()) return;

        auto object = std::make_unique<Object3d>();
        object->Initialize(scene->GetObject3dCommon());
        ModelManager::GetInstance()->LoadModel(modelName);
        object->SetModel(modelName);
        object->SetClassName("Model");
        object->SetScale(scale);
        AddCreatedObject(editor, scene, std::move(object), baseName, "Create " + baseName, useGameViewCursor);
    }

    void CreateTriggerBox(DebugEditor* editor, BaseScene* scene, bool useGameViewCursor) {
        if (!scene || !scene->GetObject3dCommon()) return;

        auto object = std::make_unique<Object3d>();
        object->Initialize(scene->GetObject3dCommon());
        object->SetModel(nullptr);
        object->SetIsVisible(true);
        object->SetClassName("InvisibleBox");
        Object3d::ColliderConfig colConfig;
        colConfig.type = ColliderType::kAABB;
        colConfig.size = { 1.0f, 1.0f, 1.0f };
        object->SetColliderConfig(colConfig);
        object->SetCollisionAttribute(CollisionAttribute::kTrigger);
        AddCreatedObject(editor, scene, std::move(object), "Trigger_Box", "Create Trigger Box", useGameViewCursor);
    }

    void CreateCollisionBox(DebugEditor* editor, BaseScene* scene, bool useGameViewCursor) {
        if (!scene || !scene->GetObject3dCommon()) return;

        auto object = std::make_unique<Object3d>();
        object->Initialize(scene->GetObject3dCommon());
        object->SetModel(nullptr);
        object->SetIsVisible(true);
        object->SetClassName("InvisibleBox");
        Object3d::ColliderConfig colConfig;
        colConfig.type = ColliderType::kAABB;
        colConfig.size = { 1.0f, 1.0f, 1.0f };
        object->SetColliderConfig(colConfig);
        object->SetCollisionAttribute(CollisionAttribute::kGround);
        AddCreatedObject(editor, scene, std::move(object), "Collision_Box", "Create Collision Box", useGameViewCursor);
    }

    void CreateCinematicCamera(DebugEditor* editor, BaseScene* scene, bool useGameViewCursor) {
        if (!scene || !scene->GetObject3dCommon()) return;

        auto object = std::make_unique<Object3d>();
        object->Initialize(scene->GetObject3dCommon());
        object->SetModel(kCinematicCameraModel);
        object->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
        object->SetIsVisible(true);
        object->SetCastShadow(false);
        object->SetClassName("Camera");
        object->SetSaveCategory("Camera");
        object->SetTranslate({ 0.0f, 5.0f, -10.0f });
        AddCreatedObject(editor, scene, std::move(object), "Cinematic_Camera", "Create Cinematic Camera", useGameViewCursor);
    }

    void CreateGimmick(DebugEditor* editor, BaseScene* scene, const std::string& type, bool useGameViewCursor) {
        if (!scene || !scene->GetObject3dCommon()) return;

        auto gimmick = GimmickFactory::GetInstance()->CreateGimmick(type, scene->GetObject3dCommon());
        AddCreatedObject(editor, scene, std::move(gimmick), "Gimmick_" + type, "Create Gimmick " + type, useGameViewCursor);
    }

    void CreateEnemy(DebugEditor* editor, BaseScene* scene, const std::string& type, bool useGameViewCursor) {
        if (!scene || !scene->GetObject3dCommon()) return;

        auto enemy = EnemyFactory::GetInstance()->CreateEnemy(type, scene->GetObject3dCommon());
        AddCreatedObject(editor, scene, std::move(enemy), "Enemy_" + type, "Create Enemy " + type, useGameViewCursor);
    }

    void CreateItem(DebugEditor* editor, BaseScene* scene, const std::string& type, bool useGameViewCursor) {
        if (!scene || !scene->GetObject3dCommon()) return;

        auto item = ItemFactory::GetInstance()->CreateItem(type, scene->GetObject3dCommon());
        AddCreatedObject(editor, scene, std::move(item), "Item_" + type, "Create Item " + type, useGameViewCursor);
    }

    void DrawCreateContextMenu(DebugEditor* editor, BaseScene* scene, bool useGameViewCursor) {
#ifdef USE_IMGUI
        if (!editor || !scene) return;

        if (useGameViewCursor) {
            ImGui::TextDisabled("選択後: 半透明プレビュー配置");
            ImGui::TextDisabled("左クリックで確定 / 右クリック・Eでキャンセル");
            ImGui::TextDisabled("面スナップ・法線整列: 有効");
        }
        else {
            ImGui::TextDisabled("作成位置: 選択中オブジェクトの横 / 未選択ならカメラ前方");
        }
        ImGui::Separator();

        if (ImGui::BeginMenu(ICON_FA_CUBE " 基本オブジェクト")) {
            if (ImGui::MenuItem("ステージブロック")) CreatePrimitive(editor, scene, "Stages/block", "Block", useGameViewCursor);
            if (ImGui::MenuItem("Cube")) CreatePrimitive(editor, scene, "Primitives/cube", "Cube", useGameViewCursor);
            if (ImGui::MenuItem("Sphere")) CreatePrimitive(editor, scene, "Primitives/sphere", "Sphere", useGameViewCursor);
            if (ImGui::MenuItem("Cylinder")) CreatePrimitive(editor, scene, "Primitives/cylinder", "Cylinder", useGameViewCursor);
            ImGui::Separator();
            if (ImGui::MenuItem("トリガーボックス")) CreateTriggerBox(editor, scene, useGameViewCursor);
            if (ImGui::MenuItem("当たり判定ボックス")) CreateCollisionBox(editor, scene, useGameViewCursor);
            if (ImGui::MenuItem("演出用カメラ")) CreateCinematicCamera(editor, scene, useGameViewCursor);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(ICON_FA_PUZZLE_PIECE " ギミック")) {
            if (ImGui::MenuItem("動く床")) CreateGimmick(editor, scene, "MovingFloor", useGameViewCursor);
            if (ImGui::MenuItem("破壊ブロック")) CreateGimmick(editor, scene, "BreakableBlock", useGameViewCursor);
            if (ImGui::MenuItem("ジャンプ台")) CreateGimmick(editor, scene, "Trampoline", useGameViewCursor);
            if (ImGui::MenuItem("沈む床")) CreateGimmick(editor, scene, "SinkingFloor", useGameViewCursor);
            if (ImGui::MenuItem("シーソー床")) CreateGimmick(editor, scene, "SeesawFloor", useGameViewCursor);
            if (ImGui::MenuItem("ダッシュパネル")) CreateGimmick(editor, scene, "DashPanel", useGameViewCursor);
            if (ImGui::MenuItem("氷の床")) CreateGimmick(editor, scene, "IceFloor", useGameViewCursor);
            if (ImGui::MenuItem("時限スイッチ")) CreateGimmick(editor, scene, "TimedSwitch", useGameViewCursor);
            if (ImGui::MenuItem("出現床")) CreateGimmick(editor, scene, "AppearingFloor", useGameViewCursor);
            if (ImGui::MenuItem("汎用スイッチ")) CreateGimmick(editor, scene, "Switch", useGameViewCursor);
            if (ImGui::MenuItem("イベント受信")) CreateGimmick(editor, scene, "EventReceiver", useGameViewCursor);
            if (ImGui::MenuItem("フックアンカー")) CreateGimmick(editor, scene, "HookAnchor", useGameViewCursor);
            if (ImGui::MenuItem("フック可動ブロック")) CreateGimmick(editor, scene, "HookPullBlock", useGameViewCursor);
            if (ImGui::MenuItem("一方通行床")) CreateGimmick(editor, scene, "OneWayFloor", useGameViewCursor);
            if (ImGui::MenuItem("水位/マグマ上下")) CreateGimmick(editor, scene, "LiquidLevel", useGameViewCursor);
            if (ImGui::MenuItem("連鎖崩れ床")) CreateGimmick(editor, scene, "ChainCollapseFloor", useGameViewCursor);
            if (ImGui::MenuItem("回転床")) CreateGimmick(editor, scene, "RotatingFloor", useGameViewCursor);
            if (ImGui::MenuItem("回転柱")) CreateGimmick(editor, scene, "RotatingPillar", useGameViewCursor);
            if (ImGui::MenuItem("時間反転床")) CreateGimmick(editor, scene, "PhaseFlipFloor", useGameViewCursor);
            if (ImGui::MenuItem("レーザー発射")) CreateGimmick(editor, scene, "LaserEmitter", useGameViewCursor);
            if (ImGui::MenuItem("レーザーノード")) CreateGimmick(editor, scene, "LaserNode", useGameViewCursor);
            if (ImGui::MenuItem("ステージゲート")) CreateGimmick(editor, scene, "StageGate", useGameViewCursor);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(ICON_FA_SKULL " 敵")) {
            if (ImGui::MenuItem("Fire Slime")) CreateEnemy(editor, scene, "FireSlime", useGameViewCursor);
            if (ImGui::MenuItem("Thunder Slime")) CreateEnemy(editor, scene, "ThunderSlime", useGameViewCursor);
            if (ImGui::MenuItem("スライム")) CreateEnemy(editor, scene, "Slime", useGameViewCursor);
            if (ImGui::MenuItem("ボム")) CreateEnemy(editor, scene, "Bomb", useGameViewCursor);
            if (ImGui::MenuItem("ボマー")) CreateEnemy(editor, scene, "Bomber", useGameViewCursor);
            if (ImGui::MenuItem("キノコ")) CreateEnemy(editor, scene, "Mushroom", useGameViewCursor);
            if (ImGui::MenuItem("巨大スライム")) CreateEnemy(editor, scene, "GiantSlime", useGameViewCursor);
            if (ImGui::MenuItem("コウモリ")) CreateEnemy(editor, scene, "Bat", useGameViewCursor);
            if (ImGui::MenuItem("目玉ビーム")) CreateEnemy(editor, scene, "BeamDrone", useGameViewCursor);
            if (ImGui::MenuItem("ボスコア")) CreateEnemy(editor, scene, "BossCore", useGameViewCursor);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(ICON_FA_HEART " アイテム")) {
            if (ImGui::MenuItem("体力回復")) CreateItem(editor, scene, "Heal", useGameViewCursor);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(ICON_FA_SAVE " プリセット")) {
            const auto& presets = PresetManager::GetInstance()->GetPresets();
            if (presets.empty()) {
                ImGui::TextDisabled("プリセットがありません");
            }
            for (const auto& [presetName, data] : presets) {
                if (ImGui::MenuItem(presetName.c_str())) {
                    if (!scene->GetObject3dCommon()) continue;
                    auto objects = PresetManager::GetInstance()->CreateObjectsFromPreset(presetName, scene->GetObject3dCommon());
                    AddCreatedObjects(editor, scene, std::move(objects), presetName, "Create Preset " + presetName, useGameViewCursor);
                }
            }
            ImGui::EndMenu();
        }
#endif
    }
}

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

    if (editor_->IsPrefabEditMode()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.18f, 0.28f, 1.0f));
        if (ImGui::BeginChild("PrefabModeHeader", ImVec2(0.0f, 74.0f), true)) {
            ImGui::TextColored(ImVec4(0.35f, 0.78f, 1.0f, 1.0f),
                ICON_FA_CUBE " Prefab Mode > %s%s",
                editor_->GetPrefabEditName().c_str(),
                editor_->IsPrefabEditDirty() ? " *" : "");
            if (ImGui::Button(ICON_FA_SAVE " Prefabを保存")) {
                editor_->SavePrefabEditSession();
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_TIMES " 破棄して戻る")) {
                editor_->CancelPrefabEditSession();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    if (ImGui::BeginPopupContextWindow("HierarchyCreateContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        DrawCreateContextMenu(currentScene, false);
        ImGui::EndPopup();
    }

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
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetGhostDirector());
        }
     if (editor_->GetMeshEffectEditor() && ImGui::Selectable("  " ICON_FA_MAGIC " メッシュエフェクト (Mesh Effect)", currentObj == editor_->GetMeshEffectEditor())) {
            editor_->SetSelectedObject(nullptr); 
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetMeshEffectEditor());
        }
        if (editor_->GetDebrisEffectEditor() && ImGui::Selectable("  " ICON_FA_CUBES " 3D破片エフェクト (Debris Effect)", currentObj == editor_->GetDebrisEffectEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetDebrisEffectEditor());
        }
        if (editor_->GetTrailEmitterEditor() && ImGui::Selectable("  " ICON_FA_FIRE " トレイルエミッター (Trail Emitter)", currentObj == editor_->GetTrailEmitterEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetTrailEmitterEditor());
        }
        if (ImGui::Selectable("  " ICON_FA_DATABASE " プリセットエディタ (Preset Editor)", EditorManager::GetInstance()->GetSelectedObject() == PresetEditor::GetInstance())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(PresetEditor::GetInstance());
        }
        if (editor_->GetSceneValidator() && ImGui::Selectable("  " ICON_FA_CHECK_CIRCLE " シーン検証 (Scene Validator)", currentObj == editor_->GetSceneValidator())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetSceneValidator());
        }
        if (editor_->GetMaterialPreviewBoard() && ImGui::Selectable("  " ICON_FA_TH_LARGE " マテリアル確認 (Material Preview)", currentObj == editor_->GetMaterialPreviewBoard())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetMaterialPreviewBoard());
        }
        if (editor_->GetEffectPreviewStage() && ImGui::Selectable("  " ICON_FA_MAGIC " エフェクト確認ステージ (Effect Preview)", currentObj == editor_->GetEffectPreviewStage())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetEffectPreviewStage());
        }
        if (editor_->GetAnimationWorkbench() && ImGui::Selectable("  " ICON_FA_RUNNING " アニメーション制作 (Animation Workbench)", currentObj == editor_->GetAnimationWorkbench())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetAnimationWorkbench());
        }
        if (editor_->GetAnimatorControllerEditor() && ImGui::Selectable("  " ICON_FA_RANDOM " Animator Controller", currentObj == editor_->GetAnimatorControllerEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetAnimatorControllerEditor());
        }
        if (editor_->GetEventLinkGraph() && ImGui::Selectable("  " ICON_FA_PROJECT_DIAGRAM " イベントリンク図 (Event Link Graph)", currentObj == editor_->GetEventLinkGraph())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetEventLinkGraph());
        }
        if (editor_->GetNodeGraphEditorWindow() && ImGui::Selectable("  " ICON_FA_PROJECT_DIAGRAM " 演出ノード (Effect Sequence Graph)", currentObj == editor_->GetNodeGraphEditorWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetNodeGraphEditorWindow());
        }
        if (editor_->GetTextSpriteGenerator() && ImGui::Selectable("  " ICON_FA_FONT " テキストPNG生成 (Text PNG)", currentObj == editor_->GetTextSpriteGenerator())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetTextSpriteGenerator());
        }
        if (editor_->GetText3DGenerator() && ImGui::Selectable("  " ICON_FA_CUBE " 3Dテキスト生成 (Text 3D)", currentObj == editor_->GetText3DGenerator())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetText3DGenerator());
        }
        if (editor_->GetModelOptimizerWindow() && ImGui::Selectable("  " ICON_FA_COMPRESS_ARROWS_ALT " モデル最適化 (Model Optimizer)", currentObj == editor_->GetModelOptimizerWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetModelOptimizerWindow());
        }
        if (editor_->GetTerrainEditorWindow() && ImGui::Selectable("  " ICON_FA_MOUNTAIN " 地形生成 (Terrain Builder)", currentObj == editor_->GetTerrainEditorWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetTerrainEditorWindow());
        }
        if (editor_->GetAssetAuditWindow() && ImGui::Selectable("  " ICON_FA_SEARCH " アセット監査 (Asset Audit)", currentObj == editor_->GetAssetAuditWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetAssetAuditWindow());
        }
        if (editor_->GetPropertyMatrixWindow() && ImGui::Selectable("  " ICON_FA_TABLE " プロパティマトリクス (Property Matrix)", currentObj == editor_->GetPropertyMatrixWindow())) {
            // 表へ渡す複数選択を維持したまま、専用ウィンドウとInspectorを開きます。
            editor_->GetPropertyMatrixWindow()->Open();
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetPropertyMatrixWindow());
        }
        if (editor_->GetStatusTuningWindow() && ImGui::Selectable("  " ICON_FA_SLIDERS_H " ステータス管理 (Status Management)", currentObj == editor_->GetStatusTuningWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetStatusTuningWindow());
        }
        if (editor_->GetJsonBackupWindow() && ImGui::Selectable("  " ICON_FA_SAVE " JSONバックアップ (Json Backup)", currentObj == editor_->GetJsonBackupWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetJsonBackupWindow());
        }
        if (editor_->GetAudioSettingsWindow() && ImGui::Selectable("  " ICON_FA_MUSIC " 音声設定 (Audio Settings)", currentObj == editor_->GetAudioSettingsWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetAudioSettingsWindow());
        }
        if (editor_->GetExecutablePackageWindow() && ImGui::Selectable("  " ICON_FA_BOX_OPEN " 実行ファイルセット (Executable Package)", currentObj == editor_->GetExecutablePackageWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetExecutablePackageWindow());
        }
        if (editor_->GetCaptureToolWindow() && ImGui::Selectable("  " ICON_FA_CAMERA " キャプチャツール (Capture Tool)", currentObj == editor_->GetCaptureToolWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetCaptureToolWindow());
        }
        if (editor_->GetGameDataDebugEditor() && ImGui::Selectable("  " ICON_FA_DATABASE " 内部データ編集 (Game Data)", currentObj == editor_->GetGameDataDebugEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetGameDataDebugEditor());
        }
        ImGui::Separator();
        if (ImGui::Selectable("  " ICON_FA_GAMEPAD " ゲーム設定 (Game Settings)", currentObj == currentScene)) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(currentScene);
        }
    }

    ImGui::Separator();
    DrawSceneAssetManager();

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
    const char* filterNames[] = { "All", "Player", "Enemy", "Object", "Camera" };
    ImGui::Combo("##CategoryFilter", &currentCategoryFilter_, filterNames, IM_ARRAYSIZE(filterNames));
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::Text("Layer:");
    ImGui::SameLine();
    ImGui::PushItemWidth(100.0f);
    const char* layerFilterNames[] = {
        "All",
        "Default",
        "Player",
        "Enemy",
        "Stage",
        "Trigger",
        "Effect",
        "EditorOnly"
    };
    ImGui::Combo("##LayerFilter", &currentLayerFilter_, layerFilterNames, IM_ARRAYSIZE(layerFilterNames));
    ImGui::PopItemWidth();

    const auto& selectedObjects = editor_->GetSelectedObjects();
    Object3d* primaryObject = editor_->GetSelectedObject();
    if (!selectedObjects.empty() && primaryObject) {
        const std::string primaryName = primaryObject->GetName().empty() ? "Selected" : primaryObject->GetName();
        ImGui::Text("選択: %zu個 / 操作: %s", selectedObjects.size(), primaryName.c_str());
        ImGui::SameLine();
        ImGui::PushItemWidth(74.0f);
        int overlayMode = editor_->GetSelectionOverlayMode();
        const char* overlayModes[] = { "簡易", "詳細", "非表示" };
        if (ImGui::Combo("##SelectionOverlayMode", &overlayMode, overlayModes, IM_ARRAYSIZE(overlayModes))) {
            editor_->SetSelectionOverlayMode(overlayMode);
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("簡易: 主選択は外枠、副選択は小さいマーカー\n詳細: すべて外枠\n非表示: ImGuizmoのみ");
        }
    }
    ImGui::Separator();

    std::string filterStr = editor_->GetSearchFilterBuffer();
    filterStr = ToLowerAscii(filterStr);


    if (!filterStr.empty()) {
        ImGui::TextColored(ImVec4(0, 1, 1, 1), ICON_FA_SEARCH_PLUS " 検索結果:");
        auto& objects = currentScene->GetObjects();
        for (auto& obj : objects) {
            if (editor_->IsPrefabEditMode() && !editor_->IsPrefabEditObject(obj.get())) continue;
            std::string name = obj->GetName();
            if (name.empty()) continue;
            if (!HasMatchingCategory(obj.get())) continue;
            if (MatchesHierarchySearch(obj.get(), filterStr)) {
                bool isSelected = editor_->IsObjectSelected(obj.get());
                ImGui::PushID(obj.get());
                if (ImGui::Selectable(name.c_str(), isSelected)) {
                    if (ImGui::GetIO().KeyShift) {
                        editor_->ToggleSelectedObject(obj.get());
                    } else {
                        editor_->SetSelectedObject(obj.get());
                    }
                    editor_->SyncObjectSelectionToInspector();
                }
                ImGui::PopID();
            }
        }
    }
    else {
        ImGui::Button(ICON_FA_BOX_OPEN " [ ここにモデルをドロップして生成 ]", ImVec2(-1, 30));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("右クリックで作成メニューを開けます");
        }
        if (ImGui::BeginPopupContextItem("HierarchyDropCreateContext", ImGuiPopupFlags_MouseButtonRight)) {
            DrawCreateContextMenu(currentScene, false);
            ImGui::EndPopup();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                const char* modelName = (const char*)payload->Data;
                ModelManager::GetInstance()->LoadModel(modelName);
                Object3dCommon* common = currentScene->GetObject3dCommon();
                if (common) {
                    auto newObj = std::make_unique<Object3d>();
                    newObj->Initialize(common); newObj->SetModel(modelName); newObj->SetClassName("Model"); newObj->SetName("Preview_" + std::string(modelName));
                    newObj->UpdateLocalMatrix(); newObj->UpdateWorldMatrix();
                    editor_->SetPreviewObject(std::move(newObj));
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
                const char* presetName = (const char*)payload->Data;
                const auto& presets = PresetManager::GetInstance()->GetPresets();
                if (presets.count(presetName) > 0) {
                    Object3dCommon* common = currentScene->GetObject3dCommon();
                    if (common) {
                        auto objects = PresetManager::GetInstance()->CreateObjectsFromPreset(presetName, common);
                        AddCreatedObjects(editor_, currentScene, std::move(objects), presetName, "Create Preset " + std::string(presetName), false);
                    }
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CINEMATIC_CAMERA_ASSET")) {
                Object3dCommon* common = currentScene ? currentScene->GetObject3dCommon() : nullptr;
                if (common) {
                    auto newObj = std::make_unique<Object3d>();
                    newObj->Initialize(common); newObj->SetModel(kCinematicCameraModel); newObj->SetName("Camera_Cinematic"); newObj->SetClassName("Camera"); newObj->SetSaveCategory("Camera"); newObj->SetCastShadow(false);
                    newObj->SetTranslate({ 0.0f, 5.0f, -10.0f }); newObj->UpdateLocalMatrix(); newObj->UpdateWorldMatrix();
                    editor_->AddEditorObject(std::move(newObj), "Create Cinematic Camera");
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PARTICLE_ASSET")) {
                const char* presetName = (const char*)payload->Data;
                Object3dCommon* common = currentScene ? currentScene->GetObject3dCommon() : nullptr;
                if (common) {
                    auto newObj = std::make_unique<Object3d>();
                    newObj->Initialize(common); 
                    newObj->SetModel("Stages/block"); 
                    newObj->SetName("VFX_" + std::string(presetName)); 
                    newObj->SetClassName("GPUParticle");
                    newObj->SetGPUParticleName(presetName);
                    // エディタ上で分かりやすいように色をオレンジ半透明っぽくする
                    newObj->SetColor({1.0f, 0.5f, 0.0f, 0.5f});
                    newObj->SetBlendMode(BlendMode::kNormal);
                    
                    newObj->SetTranslate({ 0.0f, 0.0f, 0.0f }); 
                    newObj->UpdateLocalMatrix(); 
                    newObj->UpdateWorldMatrix();
                    
                    editor_->SetPreviewObject(std::move(newObj));
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::Separator();
        auto& objects = currentScene->GetObjects();
        for (auto& obj : objects) {
            if (editor_->IsPrefabEditMode() && !editor_->IsPrefabEditObject(obj.get())) continue;
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
            editor_->AddEditorObject(std::move(newObj), "Create Trigger Box");
        }
    }
    if (ImGui::Button(ICON_FA_SHIELD_ALT " 透明ボックス生成 (当たり判定用)", ImVec2(-1, 40))) {
        Object3dCommon* common = currentScene->GetObject3dCommon();
        if (common) {
            auto newObj = std::make_unique<Object3d>();
            newObj->Initialize(common); newObj->SetModel(nullptr);newObj->SetIsVisible(true); newObj->SetClassName("InvisibleBox"); newObj->SetName("collision_Box");
            Object3d::ColliderConfig colConfig; colConfig.type = ColliderType::kAABB; colConfig.size = { 1.0f, 1.0f, 1.0f };
            newObj->SetColliderConfig(colConfig); newObj->SetCollisionAttribute(CollisionAttribute::kGround); newObj->SetTranslate({ 0, 2.0f, 0 });
            editor_->AddEditorObject(std::move(newObj), "Create Collision Box");
        }
    }
    if (ImGui::Button(ICON_FA_VIDEO " 演出用カメラ生成 (Cinematic)", ImVec2(-1, 40))) {
        Object3dCommon* common = currentScene->GetObject3dCommon();
        if (common) {
            auto newObj = std::make_unique<Object3d>();
            newObj->Initialize(common); newObj->SetModel(kCinematicCameraModel); newObj->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f }); newObj->SetIsVisible(true); newObj->SetClassName("Camera"); newObj->SetSaveCategory("Camera"); newObj->SetCastShadow(false); newObj->SetName("Cinematic_Camera_01");
            newObj->SetTranslate({ 0, 5.0f, -10.0f }); newObj->UpdateWorldMatrix();
            editor_->AddEditorObject(std::move(newObj), "Create Cinematic Camera");
        }
    }

    ImGui::PopStyleColor(3);
    ImGui::Dummy(ImVec2(0, 50));
    ImGui::TextDisabled(ICON_FA_UNLINK " (ここにドロップして親解除)");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_OBJ")) {
            Object3d* sourceObj = *(Object3d**)payload->Data;
            if (sourceObj->GetParent() != nullptr) {
                nlohmann::json beforeState = editor_->CaptureObjectState(sourceObj);
                Matrix4x4 worldMat = sourceObj->GetWorldMatrix();
                sourceObj->SetParent(nullptr, true);
                Vector3 t, rDeg, s;
                ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], &t.x, &rDeg.x, &s.x);
                sourceObj->GetTransform()->translate = t;
                sourceObj->GetTransform()->rotate = { ToRadians(rDeg.x), ToRadians(rDeg.y), ToRadians(rDeg.z) };
                sourceObj->GetTransform()->scale = s;
                sourceObj->UpdateWorldMatrix();
                editor_->RegisterObjectEdited(sourceObj, beforeState, "Unparent Object");
            }
        }
        ImGui::EndDragDropTarget();
    }
    DrawSceneAssetDialogs();
    ImGui::End();
#endif
}

void HierarchyWindow::OpenCreateSceneDialog() {
    sceneAssetStatusMessage_.clear();
    sceneAssetStatusIsError_ = false;
    if (editor_) {
        const std::vector<std::string> runtimeScenes = editor_->GetRegisteredSceneNames();
        const auto editorScene = std::find(runtimeScenes.begin(), runtimeScenes.end(), "SCENE_EDITOR");
        createRuntimeSceneIndex_ = editorScene != runtimeScenes.end()
            ? static_cast<int>(std::distance(runtimeScenes.begin(), editorScene))
            : 0;
    }
    requestCreateScenePopup_ = true;
}

void HierarchyWindow::OpenSceneAssetFromMenu(const std::string& filename) {
    RequestOpenSceneAsset(filename);
}

void HierarchyWindow::DrawSceneAssetManager() {
#ifdef USE_IMGUI
    if (!editor_ || !ImGui::CollapsingHeader(
        ICON_FA_SAVE " Scene Asset",
        ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const std::vector<SceneSerializer::SceneAssetInfo> assets = editor_->GetSceneAssets();
    const std::string currentFilename = editor_->GetCurrentSceneFilenameBuffer();
    const bool sceneTransitioning = editor_->GetSceneManager() && editor_->GetSceneManager()->IsTransitioning();

    const auto findAsset = [&](const std::string& filename) {
        return std::find_if(assets.begin(), assets.end(), [&](const SceneSerializer::SceneAssetInfo& asset) {
            return asset.filename == filename;
        });
    };

    if (selectedSceneAssetFilename_.empty() || findAsset(selectedSceneAssetFilename_) == assets.end()) {
        if (findAsset(currentFilename) != assets.end()) {
            selectedSceneAssetFilename_ = currentFilename;
        }
        else if (!assets.empty()) {
            selectedSceneAssetFilename_ = assets.front().filename;
        }
    }

    std::string selectedLabel = "Scene Assetがありません";
    const auto selectedAsset = findAsset(selectedSceneAssetFilename_);
    if (selectedAsset != assets.end()) {
        selectedLabel = selectedAsset->displayName + "  (" + selectedAsset->id + ")";
    }
    const bool selectedAssetIsActive = selectedAsset != assets.end() &&
        editor_->IsCurrentSceneAsset(selectedSceneAssetFilename_);
    bool selectedRuntimeNeedsReload = false;
    if (selectedAssetIsActive && editor_->GetSceneManager()) {
        const SceneLoadContext& activeContext = editor_->GetSceneManager()->GetActiveSceneLoadContext();
        selectedRuntimeNeedsReload =
            selectedAsset->runtimeScene != activeContext.runtimeScene ||
            selectedAsset->controllerName != activeContext.controllerName ||
            selectedAsset->bgmPath != activeContext.bgmPath ||
            selectedAsset->lightPath != activeContext.lightPath ||
            selectedAsset->cameraPath != activeContext.cameraPath ||
            selectedAsset->skyboxPath != activeContext.skyboxPath;
    }

    ImGui::TextColored(
        ImVec4(0.42f, 0.78f, 1.0f, 1.0f),
        ICON_FA_CIRCLE " Active: %s%s",
        currentFilename.c_str(),
        editor_->HasAnyDirty() ? " *" : "");

    if (ImGui::BeginCombo(ICON_FA_FOLDER_OPEN " Scene一覧", selectedLabel.c_str())) {
        for (const SceneSerializer::SceneAssetInfo& asset : assets) {
            const bool isSelected = asset.filename == selectedSceneAssetFilename_;
            const bool isActive = editor_->IsCurrentSceneAsset(asset.filename);
            std::string label = isActive ? ICON_FA_CIRCLE " " : "   ";
            label += asset.displayName + "##" + asset.filename;
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                selectedSceneAssetFilename_ = asset.filename;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !isActive) {
                    RequestOpenSceneAsset(asset.filename);
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\n実行クラス: %s\nController: %s\nObject: %s\nSprite: %s\nダブルクリックで開く",
                    asset.filename.c_str(),
                    asset.runtimeScene.c_str(),
                    asset.controllerName.c_str(),
                    asset.usesSplitFiles ? "分割JSON" : "単一JSON",
                    asset.hasSpriteLayout ? asset.spriteLayoutPath.c_str() : "なし");
            }
        }
        ImGui::EndCombo();
    }

    if (selectedAsset != assets.end()) {
        const std::vector<std::string> runtimeScenes = editor_->GetRegisteredSceneNames();
        ImGui::BeginDisabled(sceneTransitioning || runtimeScenes.empty());
        if (ImGui::BeginCombo("実行クラス", selectedAsset->runtimeScene.c_str())) {
            for (const std::string& runtimeScene : runtimeScenes) {
                const bool isSelected = runtimeScene == selectedAsset->runtimeScene;
                if (ImGui::Selectable(runtimeScene.c_str(), isSelected) && !isSelected) {
                    std::string errorMessage;
                    if (editor_->SetSceneAssetRuntimeScene(
                        selectedAsset->filename,
                        runtimeScene,
                        errorMessage)) {
                        sceneAssetStatusMessage_ = "実行クラスを変更しました: " + runtimeScene;
                        sceneAssetStatusIsError_ = false;
                    }
                    else {
                        sceneAssetStatusMessage_ = errorMessage;
                        sceneAssetStatusIsError_ = true;
                    }
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Sceneを開くと、このC++ SceneクラスとAssetのJSONを同時に読み込みます。");

        if (selectedAsset->usesLegacyMetadata) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.72f, 0.2f, 1.0f),
                ICON_FA_EXCLAMATION_TRIANGLE " 旧形式Scene。実行設定の保存でScene Asset形式へ更新します。");
        }
        if (ImGui::Button(ICON_FA_COG " 実行設定", ImVec2(122.0f, 0.0f))) {
            const std::vector<std::string>& controllerNames =
                SceneControllerFactory::GetInstance()->GetRegisteredNames();
            const auto controller = std::find(
                controllerNames.begin(),
                controllerNames.end(),
                selectedAsset->controllerName);
            runtimeSettingsControllerIndex_ = controller != controllerNames.end()
                ? static_cast<int>(std::distance(controllerNames.begin(), controller))
                : 0;
            strcpy_s(runtimeBgmPath_, sizeof(runtimeBgmPath_), selectedAsset->bgmPath.c_str());
            strcpy_s(runtimeLightPath_, sizeof(runtimeLightPath_), selectedAsset->lightPath.c_str());
            strcpy_s(runtimeCameraPath_, sizeof(runtimeCameraPath_), selectedAsset->cameraPath.c_str());
            strcpy_s(runtimeSkyboxPath_, sizeof(runtimeSkyboxPath_), selectedAsset->skyboxPath.c_str());
            requestRuntimeSettingsPopup_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CHECK_CIRCLE " 検証", ImVec2(102.0f, 0.0f))) {
            const SceneSerializer::SceneAssetValidationResult validation =
                editor_->ValidateSceneAsset(selectedAsset->filename);
            if (!validation.errors.empty()) {
                sceneAssetStatusMessage_ = "エラー: " + validation.errors.front();
                sceneAssetStatusIsError_ = true;
            }
            else if (!validation.warnings.empty()) {
                sceneAssetStatusMessage_ = "警告: " + validation.warnings.front();
                sceneAssetStatusIsError_ = false;
            }
            else {
                sceneAssetStatusMessage_ = "Scene Assetの検証に成功しました。";
                sceneAssetStatusIsError_ = false;
            }
        }
    }

    ImGui::BeginDisabled(sceneTransitioning);
    if (ImGui::Button(ICON_FA_PLUS " 新規", ImVec2(84.0f, 0.0f))) {
        OpenCreateSceneDialog();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(sceneTransitioning || selectedAsset == assets.end() ||
        (selectedAssetIsActive && !selectedRuntimeNeedsReload));
    if (ImGui::Button(
        selectedRuntimeNeedsReload ? ICON_FA_SYNC " 再読込" : ICON_FA_FOLDER_OPEN " 開く",
        ImVec2(84.0f, 0.0f))) {
        RequestOpenSceneAsset(selectedSceneAssetFilename_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(sceneTransitioning || selectedAsset == assets.end());
    if (ImGui::Button(ICON_FA_COPY " 複製", ImVec2(84.0f, 0.0f))) {
        sceneAssetStatusMessage_.clear();
        sceneAssetStatusIsError_ = false;
        const std::string duplicateId = selectedAsset->id + "_copy";
        strcpy_s(duplicateSceneId_, sizeof(duplicateSceneId_), duplicateId.c_str());
        strcpy_s(duplicateSceneDisplayName_, sizeof(duplicateSceneDisplayName_), selectedAsset->displayName.c_str());
        requestDuplicateScenePopup_ = true;
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(sceneTransitioning || selectedAsset == assets.end());
    if (ImGui::Button(ICON_FA_EDIT " 名前変更", ImVec2(122.0f, 0.0f))) {
        sceneAssetStatusMessage_.clear();
        sceneAssetStatusIsError_ = false;
        strcpy_s(renameSceneId_, sizeof(renameSceneId_), selectedAsset->id.c_str());
        strcpy_s(renameSceneDisplayName_, sizeof(renameSceneDisplayName_), selectedAsset->displayName.c_str());
        requestRenameScenePopup_ = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool canDelete = selectedAsset != assets.end() && !editor_->IsCurrentSceneAsset(selectedSceneAssetFilename_);
    ImGui::BeginDisabled(sceneTransitioning || !canDelete);
    if (ImGui::Button(ICON_FA_TRASH_ALT " 削除", ImVec2(102.0f, 0.0f))) {
        sceneAssetStatusMessage_.clear();
        sceneAssetStatusIsError_ = false;
        requestDeleteScenePopup_ = true;
    }
    ImGui::EndDisabled();
    if (!canDelete && selectedAsset != assets.end() && editor_->IsCurrentSceneAsset(selectedSceneAssetFilename_) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Active Sceneは削除できません。別のSceneを開いてください。");
    }

    if (!sceneAssetStatusMessage_.empty()) {
        ImGui::TextColored(
            sceneAssetStatusIsError_ ? ImVec4(1.0f, 0.38f, 0.32f, 1.0f) : ImVec4(0.45f, 1.0f, 0.58f, 1.0f),
            "%s",
            sceneAssetStatusMessage_.c_str());
    }

    ImGui::SeparatorText("保存");
    if (editor_->HasAnyDirty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.2f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " %s", editor_->GetDirtySummaryText().c_str());
    }
    else {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), ICON_FA_CHECK_CIRCLE " %s", editor_->GetDirtySummaryText().c_str());
    }

    ImGui::BeginDisabled(sceneTransitioning);
    if (ImGui::Button(ICON_FA_USER " Player")) editor_->SaveScene(SaveMode::Player);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SKULL " Enemy")) editor_->SaveScene(SaveMode::Enemy);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CUBE " Object")) editor_->SaveScene(SaveMode::Object);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_VIDEO " Camera")) editor_->SaveScene(SaveMode::Camera);
    if (ImGui::Button(ICON_FA_DOWNLOAD " Active Sceneを保存", ImVec2(-1.0f, 0.0f))) {
        editor_->SaveScene(SaveMode::All);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("保存先: Resources/json/3Dobject/%s", currentFilename.c_str());
#endif
}

void HierarchyWindow::RequestOpenSceneAsset(const std::string& filename) {
    if (!editor_ || filename.empty()) {
        return;
    }
    if (editor_->IsCurrentSceneAsset(filename)) {
        const std::vector<SceneSerializer::SceneAssetInfo> assets = editor_->GetSceneAssets();
        const auto asset = std::find_if(assets.begin(), assets.end(), [&](const SceneSerializer::SceneAssetInfo& candidate) {
            return candidate.filename == filename;
        });
        SceneManager* sceneManager = editor_->GetSceneManager();
        if (asset == assets.end() || !sceneManager) {
            return;
        }
        const SceneLoadContext& activeContext = sceneManager->GetActiveSceneLoadContext();
        const bool settingsMatch =
            asset->runtimeScene == activeContext.runtimeScene &&
            asset->controllerName == activeContext.controllerName &&
            asset->bgmPath == activeContext.bgmPath &&
            asset->lightPath == activeContext.lightPath &&
            asset->cameraPath == activeContext.cameraPath &&
            asset->skyboxPath == activeContext.skyboxPath;
        if (settingsMatch) return;
    }
    if (editor_->HasAnyDirty()) {
        pendingOpenSceneAssetFilename_ = filename;
        requestUnsavedOpenPopup_ = true;
        return;
    }

    std::string errorMessage;
    if (!editor_->OpenSceneAsset(filename, false, errorMessage)) {
        sceneAssetStatusMessage_ = errorMessage;
        sceneAssetStatusIsError_ = true;
    }
    else {
        sceneAssetStatusMessage_ = "Sceneを開いています: " + filename;
        sceneAssetStatusIsError_ = false;
    }
}

void HierarchyWindow::DrawSceneAssetDialogs() {
#ifdef USE_IMGUI
    if (!editor_) {
        return;
    }

    if (requestCreateScenePopup_) {
        ImGui::OpenPopup("新規Scene Asset###CreateSceneAsset");
        requestCreateScenePopup_ = false;
    }
    if (requestDuplicateScenePopup_) {
        ImGui::OpenPopup("Scene Assetを複製###DuplicateSceneAsset");
        requestDuplicateScenePopup_ = false;
    }
    if (requestRenameScenePopup_) {
        ImGui::OpenPopup("Scene Assetの名前変更###RenameSceneAsset");
        requestRenameScenePopup_ = false;
    }
    if (requestDeleteScenePopup_) {
        ImGui::OpenPopup("Scene Assetを削除###DeleteSceneAsset");
        requestDeleteScenePopup_ = false;
    }
    if (requestUnsavedOpenPopup_) {
        ImGui::OpenPopup("未保存のScene変更###OpenSceneAssetUnsaved");
        requestUnsavedOpenPopup_ = false;
    }
    if (requestRuntimeSettingsPopup_) {
        ImGui::OpenPopup("Scene Asset実行設定###SceneAssetRuntimeSettings");
        requestRuntimeSettingsPopup_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(
        "Scene Asset実行設定###SceneAssetRuntimeSettings",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::vector<std::string>& controllerNames =
            SceneControllerFactory::GetInstance()->GetRegisteredNames();
        if (!controllerNames.empty()) {
            runtimeSettingsControllerIndex_ = std::clamp(
                runtimeSettingsControllerIndex_,
                0,
                static_cast<int>(controllerNames.size()) - 1);
            if (ImGui::BeginCombo(
                "Scene Controller",
                controllerNames[runtimeSettingsControllerIndex_].c_str())) {
                for (int index = 0; index < static_cast<int>(controllerNames.size()); ++index) {
                    const bool isSelected = index == runtimeSettingsControllerIndex_;
                    if (ImGui::Selectable(controllerNames[index].c_str(), isSelected)) {
                        runtimeSettingsControllerIndex_ = index;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        ImGui::InputText("BGM", runtimeBgmPath_, sizeof(runtimeBgmPath_));
        ImGui::InputText("Light JSON", runtimeLightPath_, sizeof(runtimeLightPath_));
        ImGui::InputText("Camera JSON", runtimeCameraPath_, sizeof(runtimeCameraPath_));
        ImGui::InputText("Skybox", runtimeSkyboxPath_, sizeof(runtimeSkyboxPath_));
        ImGui::TextDisabled("空欄は選択したC++ Sceneクラスの既定値を使用します。");
        ImGui::TextDisabled("Camera JSONはResources/json/camera以下のファイル名を指定します。");
        ImGui::Separator();

        ImGui::BeginDisabled(controllerNames.empty());
        if (ImGui::Button(ICON_FA_SAVE " 保存", ImVec2(120.0f, 0.0f))) {
            std::string errorMessage;
            if (editor_->SetSceneAssetRuntimeSettings(
                selectedSceneAssetFilename_,
                controllerNames[runtimeSettingsControllerIndex_],
                runtimeBgmPath_,
                runtimeLightPath_,
                runtimeCameraPath_,
                runtimeSkyboxPath_,
                errorMessage)) {
                sceneAssetStatusMessage_ = "Scene Assetの実行設定を保存しました。";
                sceneAssetStatusIsError_ = false;
                ImGui::CloseCurrentPopup();
            }
            else {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("新規Scene Asset###CreateSceneAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Scene IDはファイル名に使用します。表示名には日本語を使用できます。");
        ImGui::InputText("Scene ID", createSceneId_, sizeof(createSceneId_));
        ImGui::InputText("表示名", createSceneDisplayName_, sizeof(createSceneDisplayName_));
        const char* templates[] = { "空のScene", "現在のSceneを複製" };
        ImGui::Combo("テンプレート", &createSceneTemplate_, templates, IM_ARRAYSIZE(templates));
        const std::vector<std::string> runtimeScenes = editor_->GetRegisteredSceneNames();
        if (!runtimeScenes.empty()) {
            createRuntimeSceneIndex_ = std::clamp(
                createRuntimeSceneIndex_,
                0,
                static_cast<int>(runtimeScenes.size()) - 1);
            if (ImGui::BeginCombo("実行クラス", runtimeScenes[createRuntimeSceneIndex_].c_str())) {
                for (int index = 0; index < static_cast<int>(runtimeScenes.size()); ++index) {
                    const bool isSelected = index == createRuntimeSceneIndex_;
                    if (ImGui::Selectable(runtimeScenes[index].c_str(), isSelected)) {
                        createRuntimeSceneIndex_ = index;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "登録済みの実行クラスがありません。");
        }
        if (createSceneTemplate_ == 1) {
            ImGui::TextDisabled("現在のメモリ上のObjectと関連Spriteレイアウトを複製します。");
        }
        if (sceneAssetStatusIsError_ && !sceneAssetStatusMessage_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "%s", sceneAssetStatusMessage_.c_str());
        }
        ImGui::Separator();

        const auto createAsset = [&](bool openAfterCreate) {
            if (runtimeScenes.empty()) {
                sceneAssetStatusMessage_ = "SceneFactoryに実行クラスが登録されていません。";
                sceneAssetStatusIsError_ = true;
                return false;
            }
            std::string createdFilename;
            std::string errorMessage;
            const SceneSerializer::SceneAssetTemplate sceneTemplate = createSceneTemplate_ == 0
                ? SceneSerializer::SceneAssetTemplate::Empty
                : SceneSerializer::SceneAssetTemplate::CurrentScene;
            if (!editor_->CreateSceneAsset(
                createSceneId_,
                createSceneDisplayName_,
                runtimeScenes[createRuntimeSceneIndex_],
                sceneTemplate,
                createdFilename,
                errorMessage)) {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
                return false;
            }
            selectedSceneAssetFilename_ = createdFilename;
            sceneAssetStatusMessage_ = "Scene Assetを作成しました: " + createdFilename;
            sceneAssetStatusIsError_ = false;
            if (openAfterCreate) {
                RequestOpenSceneAsset(createdFilename);
            }
            return true;
        };

        if (ImGui::Button(ICON_FA_FOLDER_OPEN " 作成して開く", ImVec2(150.0f, 0.0f))) {
            if (createAsset(true)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLUS " 作成のみ", ImVec2(120.0f, 0.0f))) {
            if (createAsset(false)) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Scene Assetを複製###DuplicateSceneAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("複製元: %s", selectedSceneAssetFilename_.c_str());
        ImGui::InputText("新しいScene ID", duplicateSceneId_, sizeof(duplicateSceneId_));
        ImGui::InputText("新しい表示名", duplicateSceneDisplayName_, sizeof(duplicateSceneDisplayName_));
        if (sceneAssetStatusIsError_ && !sceneAssetStatusMessage_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "%s", sceneAssetStatusMessage_.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_COPY " 複製", ImVec2(120.0f, 0.0f))) {
            std::string createdFilename;
            std::string errorMessage;
            if (editor_->DuplicateSceneAsset(
                selectedSceneAssetFilename_,
                duplicateSceneId_,
                duplicateSceneDisplayName_,
                createdFilename,
                errorMessage)) {
                selectedSceneAssetFilename_ = createdFilename;
                sceneAssetStatusMessage_ = "Scene Assetを複製しました: " + createdFilename;
                sceneAssetStatusIsError_ = false;
                ImGui::CloseCurrentPopup();
            }
            else {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Scene Assetの名前変更###RenameSceneAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string sourceFilename = selectedSceneAssetFilename_;
        ImGui::InputText("Scene ID", renameSceneId_, sizeof(renameSceneId_));
        ImGui::InputText("表示名", renameSceneDisplayName_, sizeof(renameSceneDisplayName_));
        ImGui::TextDisabled("Active Sceneを変更した場合は、以降の保存先も新しい名前へ切り替わります。");
        if (sceneAssetStatusIsError_ && !sceneAssetStatusMessage_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "%s", sceneAssetStatusMessage_.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_EDIT " 名前変更", ImVec2(130.0f, 0.0f))) {
            std::string renamedFilename;
            std::string errorMessage;
            if (editor_->RenameSceneAsset(
                sourceFilename,
                renameSceneId_,
                renameSceneDisplayName_,
                renamedFilename,
                errorMessage)) {
                selectedSceneAssetFilename_ = renamedFilename;
                sceneAssetStatusMessage_ = "Scene Assetの名前を変更しました: " + renamedFilename;
                sceneAssetStatusIsError_ = false;
                ImGui::CloseCurrentPopup();
            }
            else {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Scene Assetを削除###DeleteSceneAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " この操作は元に戻せません。");
        ImGui::TextWrapped("%s と関連する分割JSON、Spriteレイアウトを削除します。", selectedSceneAssetFilename_.c_str());
        if (sceneAssetStatusIsError_ && !sceneAssetStatusMessage_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "%s", sceneAssetStatusMessage_.c_str());
        }
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_TRASH_ALT " 削除する", ImVec2(130.0f, 0.0f))) {
            const std::string deletedFilename = selectedSceneAssetFilename_;
            std::string errorMessage;
            if (editor_->DeleteSceneAsset(deletedFilename, errorMessage)) {
                selectedSceneAssetFilename_.clear();
                sceneAssetStatusMessage_ = "Scene Assetを削除しました: " + deletedFilename;
                sceneAssetStatusIsError_ = false;
                ImGui::CloseCurrentPopup();
            }
            else {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("未保存のScene変更###OpenSceneAssetUnsaved", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.28f, 1.0f), ICON_FA_EXCLAMATION_TRIANGLE " 未保存の変更があります。");
        ImGui::TextWrapped("%s を開く前に、現在のSceneを保存しますか？", pendingOpenSceneAssetFilename_.c_str());
        ImGui::TextDisabled("%s", editor_->GetDirtySummaryText().c_str());
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_SAVE " 保存して開く", ImVec2(150.0f, 0.0f))) {
            editor_->SaveThenOpenSceneAsset(pendingOpenSceneAssetFilename_);
            pendingOpenSceneAssetFilename_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " 保存せず開く", ImVec2(150.0f, 0.0f))) {
            const std::string filename = pendingOpenSceneAssetFilename_;
            pendingOpenSceneAssetFilename_.clear();
            std::string errorMessage;
            if (!editor_->OpenSceneAsset(filename, true, errorMessage)) {
                sceneAssetStatusMessage_ = errorMessage;
                sceneAssetStatusIsError_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(100.0f, 0.0f))) {
            pendingOpenSceneAssetFilename_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#endif
}

void HierarchyWindow::DrawCreateContextMenu(BaseScene* scene, bool useGameViewCursor) {
#ifdef USE_IMGUI
    ::DrawCreateContextMenu(editor_, scene, useGameViewCursor);
#else
    (void)scene;
    (void)useGameViewCursor;
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
    if (editor_->IsObjectSelected(obj)) node_flags |= ImGuiTreeNodeFlags_Selected;

    std::string name = obj->GetName();
    if (name.empty()) name = "NoName";
    if (obj->GetClassName() == "InvisibleBox") name = "[Trigger] " + name;
    if (!obj->GetTag().empty()) {
        name += " #" + obj->GetTag();
    }
    const std::string& layerName = obj->GetLayer();
    if (!layerName.empty() && layerName != "Default") {
        name += " [" + layerName + "]";
    }

    // 左側にツリーノードを描画
    bool node_open = ImGui::TreeNodeEx((void*)obj, node_flags, name.c_str());

    // アイテムがクリックされたら選択状態にする
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (ImGui::GetIO().KeyShift) {
            editor_->ToggleSelectedObject(obj);
        } else {
            editor_->SetSelectedObject(obj);
        }
        editor_->SyncObjectSelectionToInspector();
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
                nlohmann::json beforeState = editor_->CaptureObjectState(sourceObj);
                sourceObj->SetParent(obj, true);
                editor_->RegisterObjectEdited(sourceObj, beforeState, "Parent Object");
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
        nlohmann::json beforeState = editor_->CaptureObjectState(obj);
        obj->SetIsVisible(!isVisible);
        editor_->RegisterObjectEdited(obj, beforeState, "Toggle Visibility");
    }
    if (!isVisible) ImGui::PopStyleColor();

    ImGui::SameLine();

    // 2. ロックトグル (南京錠アイコン)
    bool isLocked = obj->GetIsLocked();
    const char* lockIcon = isLocked ? ICON_FA_LOCK : ICON_FA_UNLOCK;
    if (isLocked) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); // ロック時は赤色
    if (ImGui::Button(lockIcon)) {
        nlohmann::json beforeState = editor_->CaptureObjectState(obj);
        obj->SetIsLocked(!isLocked);
        editor_->RegisterObjectEdited(obj, beforeState, "Toggle Lock");
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
    if (!obj || obj->IsEditorInternal()) return false;

    const std::string cat = obj->GetSaveCategory();
    bool categoryMatches = currentCategoryFilter_ == 0;
    if (currentCategoryFilter_ == 1 && cat == "Player") categoryMatches = true;
    if (currentCategoryFilter_ == 2 && cat == "Enemy") categoryMatches = true;
    if (currentCategoryFilter_ == 3 && cat == "Object") categoryMatches = true;
    if (currentCategoryFilter_ == 4 && (cat == "Camera" || obj->IsCameraObject())) categoryMatches = true;

    const char* layerFilterNames[] = {
        "All",
        "Default",
        "Player",
        "Enemy",
        "Stage",
        "Trigger",
        "Effect",
        "EditorOnly"
    };
    bool layerMatches = currentLayerFilter_ == 0;
    if (currentLayerFilter_ > 0 && currentLayerFilter_ < IM_ARRAYSIZE(layerFilterNames)) {
        layerMatches = obj->GetLayer() == layerFilterNames[currentLayerFilter_];
    }

    if (categoryMatches && layerMatches) return true;

    for (Object3d* child : obj->GetChildren()) {
        if (HasMatchingCategory(child)) return true;
    }

    return false;
}
