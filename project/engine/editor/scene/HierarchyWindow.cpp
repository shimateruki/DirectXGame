#include "HierarchyWindow.h"
#include "engine/editor/graphics/camera/CameraObjectBadge.h"
#include <numbers>
#include "DebugEditor.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3D.h"
#include "engine/graphics/effect/DecalSystem.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include "IconsFontAwesome5.h"
#include "EditorManager.h"
#include "SceneWorkspace.h"
#include "EditorAssetDragPayload.h"
#include "engine/editor/graphics/camera/CameraEditor.h"
#include "CameraManager.h"
#include "PostEffectEditor.h"
#include "SpriteDebugEditor.h"
#include "GpuParticleEditor.h"
#include "VfxSequencerEditor.h"
#include "ParticleEditor.h"
#include "GhostRecorder.h"
#include "game/system/cinematic/GhostDirector.h"
#include "LightEditor.h"
#include "ModelManager.h"
#include "DebugConsole.h"
#include "engine/editor/assets/preset/PresetManager.h"
#include "KeyConfig.h"
#include "MeshEffectEditor.h"
#include "DebrisEffectEditor.h"
#include "TrailEmitterEditor.h"
#include "EditorObjectFactoryRegistry.h"
#include "json.hpp"
#include <array>
#include <cctype>
#include <algorithm> // std::transform用
#include <cmath>
#include <map>
#include <vector>


// ImGuizmoの角度表示は度、Object3dの回転値はラジアンなので変換します。
static float ToRadians(float degrees) { return degrees * (std::numbers::pi_v<float> / 180.0f); }

// 回転値をInspectorへ表示するときにラジアンから度へ戻します。
static float ToDegrees(float radians) { return radians * (180.0f / std::numbers::pi_v<float>); }

static std::string ToLowerAscii(std::string text) {
    // 照合用の文字列をコピー上で小文字化し、表示名の表記は維持する。
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

static bool ContainsLower(const std::string& text, const std::string& lowerNeedle) {
    return ToLowerAscii(text).find(lowerNeedle) != std::string::npos;
}

static bool MatchesHierarchySearch(engine::Object3D* object, const std::string& lowerFilter) {
    // 名前だけでなく種別・タグ・レイヤーも階層検索の対象にする。
    // 名前だけでなく種別・タグ・レイヤーも階層検索の対象にする。
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


namespace {
    constexpr const char* kCinematicCameraModel = "Editor/camera_gizmo";

    // 実際の親子関係や保存先カテゴリには触れず、一覧で使う表示上の分類だけを決める。
    enum class HierarchyDisplayGroup : size_t {
        Player,
        Enemy,
        Camera,
        Stage,
        Gimmick,
        Effect,
        Object,
        Other,
        Count
    };

    constexpr std::array<const char*, static_cast<size_t>(HierarchyDisplayGroup::Count)> kHierarchyGroupNames = {
        "プレイヤー", "敵", "カメラ", "地形・背景", "ギミック", "エフェクト", "オブジェクト", "その他"
    };

    HierarchyDisplayGroup GetHierarchyDisplayGroup(const engine::Object3D* object) {
        const std::string& category = object->GetSaveCategory();
        const std::string& className = object->GetClassName();
        const std::string modelName = object->GetModelName();
        if (category == "Player" || className == "Player")
            return HierarchyDisplayGroup::Player;
        if (category == "Enemy" || className == "Enemy")
            return HierarchyDisplayGroup::Enemy;
        if (category == "Camera" || object->IsCameraObject())
            return HierarchyDisplayGroup::Camera;
        if (className == "Effect" || className == "GPUParticle" || modelName.rfind("Effects/", 0) == 0) {
            return HierarchyDisplayGroup::Effect;
        }

        if (className == "Gimmick")
            return HierarchyDisplayGroup::Gimmick;
        if (modelName.rfind("Stages/", 0) == 0)
            return HierarchyDisplayGroup::Stage;

        // モデルを持たない地形用の親も、子の地形と同じ分類で表示する。
        if (modelName.empty() && category == "Object") {
            for (const engine::Object3D* child : object->GetChildren()) {
                if (child && child->GetModelName().rfind("Stages/", 0) == 0) {
                    return HierarchyDisplayGroup::Stage;
                }
            }
        }

        if (category == "Object" || className == "Model")
            return HierarchyDisplayGroup::Object;
        return HierarchyDisplayGroup::Other;
    }

    std::string GetHierarchyDisplaySubtype(const engine::Object3D* object) {
        // タグやゲーム種別を優先し、設定がなければモデル名で階層内の識別を助ける。
        if (!object->GetTag().empty())
            return "#" + object->GetTag();
        if (!object->GetGimmickType().empty())
            return object->GetGimmickType();
        if (!object->GetEnemyType().empty())
            return object->GetEnemyType();
        if (!object->GetItemType().empty())
            return object->GetItemType();
        const std::string modelName = object->GetModelName();

        if (modelName.empty() && GetHierarchyDisplayGroup(object) == HierarchyDisplayGroup::Stage) {
            return "地形グループ";
        }

        if (!modelName.empty()) {
            const size_t separator = modelName.find_last_of("/\\");
            return modelName.substr(separator == std::string::npos ? 0 : separator + 1);
        }

        return object->GetClassName().empty() ? "種類未設定" : object->GetClassName();
    }

    std::string MakeUniqueName(BaseScene* scene, const std::string& baseName) {
        // シーン内で同名があれば連番を付け、既存オブジェクト名を変えずに追加する。
        if (!scene)
            return baseName;

        auto exists = [&](const std::string& name) {
            for (const auto& obj : scene->GetObjects()) {
                if (obj && obj->GetName() == name)
                    return true;
            }

            return false;
        };

        if (!exists(baseName))
            return baseName;

        for (int index = 1; index < 10000; ++index) {
            std::string candidate = baseName + "_" + std::to_string(index);
            if (!exists(candidate))
                return candidate;
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
        // 配置済みオブジェクトに加え、今回生成する兄弟の予約名とも衝突させない。
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

    void AssignPresetInstanceNames(BaseScene* scene, const std::string& presetName, std::vector<std::unique_ptr<engine::Object3D>>& objects) {
        // 同一プリセットの複数配置でも、ルートと子の名前がシーン内で衝突しないようにする。
        if (objects.empty())
            return;

        std::vector<std::string> reservedNames;
        std::string rootName = MakeUniquePresetName(scene, GetLeafName(presetName), reservedNames);
        objects.front()->SetName(rootName);
        reservedNames.push_back(rootName);

        for (size_t index = 1; index < objects.size(); ++index) {
            if (!objects[index])
                continue;

            std::string baseName = objects[index]->GetName();

            if (baseName.empty()) {
                baseName = rootName + "_Child";
            }

            std::string uniqueName = MakeUniquePresetName(scene, baseName, reservedNames);
            objects[index]->SetName(uniqueName);
            reservedNames.push_back(uniqueName);
        }
    }

    float GetObjectYOffset(const engine::Object3D* object) {
        if (!object)
            return 1.0f;

        // 床からの初期オフセットはコライダーのY寸法を優先し、未設定なら表示倍率を目安にする。
        engine::Object3D::ColliderConfig colConfig = object->GetColliderConfig();
        if (colConfig.size.y > 0.0f)
            return colConfig.size.y;
        if (object->GetTransform().scale.y > 0.0f)
            return object->GetTransform().scale.y;
        return 1.0f;
    }

    // ラジアンのピッチとヨーから、回転ゼロでワールドの+Zを向く単位ベクトルを求める。
    engine::Vector3 CalculateCameraForward(const engine::Vector3& rotation) {
        return {
            std::sin(rotation.y) * std::cos(rotation.x),
            -std::sin(rotation.x),
            std::cos(rotation.y) * std::cos(rotation.x)
        };
    }

    engine::Vector3 GetDefaultCreatePosition(DebugEditor* editor, const engine::Object3D* object) {
        if (!editor)
            return { 0.0f, 1.0f, 0.0f };

        engine::Object3D* selected = editor->GetSelectedObject();

        if (selected) {
            // 親子関係の影響を含むワールド位置を取り、選択物のX方向に2単位離して置く。
            engine::Matrix4x4 world = selected->GetWorldMatrix();
            return { world.m[3][0] + 2.0f, world.m[3][1], world.m[3][2] };
        }

        engine::Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

        if (!camera) {
            return { 0.0f, GetObjectYOffset(object), 0.0f };
        }

        // 床との交点を使えない場合は、視線上でカメラから10ワールド単位先に置く。
        engine::Vector3 eye = camera->GetEye();
        engine::Vector3 forward = CalculateCameraForward(camera->GetRotation());
        engine::Vector3 position = {
            eye.x + forward.x * 10.0f,
            eye.y + forward.y * 10.0f,
            eye.z + forward.z * 10.0f
        };

        // カメラが床方向を向いている時は、画面中央の床付近に生成する
        if (std::abs(forward.y) > 0.0001f) {
            float t = (0.0f - eye.y) / forward.y;

            // 視線の後ろや80ワールド単位以上離れた交点は、配置先として使わない。
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

    void AddCreatedObject(DebugEditor* editor, BaseScene* scene, std::unique_ptr<engine::Object3D> object, const std::string& baseName, const std::string& commandLabel, bool useGameViewCursor) {
        // ゲームビュー配置だけは確定を遅らせ、半透明プレビューで位置を選ばせる。
        if (!editor || !scene || !object)
            return;

        object->SetName(MakeUniqueName(scene, baseName));

        if (useGameViewCursor) {
            editor->StartGameViewCreatePreview(std::move(object), commandLabel);
            return;
        }

        // その場で登録する場合は先に行列を更新し、追加直後から描画や選択に使えるようにする。
        engine::Vector3 createPosition = useGameViewCursor
            ? editor->CalculateGameViewCreatePosition(object.get())
            : GetDefaultCreatePosition(editor, object.get());
        object->SetTranslate(createPosition);
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();

        std::string createdName = object->GetName();
        editor->AddEditorObject(std::move(object), commandLabel);
        DebugConsole::GetInstance()->AddLog("Create: " + createdName);
    }

    void AddCreatedObjects(DebugEditor* editor, BaseScene* scene, std::vector<std::unique_ptr<engine::Object3D>> objects, const std::string& baseName, const std::string& commandLabel, bool useGameViewCursor) {
        // 複数Objectはルートだけを移動し、親子の相対位置を保ったまま一括で追加する。
        if (!editor || !scene || objects.empty())
            return;

        AssignPresetInstanceNames(scene, baseName, objects);
        engine::Object3D* rootObject = objects.front().get();
        if (!rootObject)
            return;

        if (useGameViewCursor) {
            editor->StartGameViewCreatePreview(std::move(objects), commandLabel);
            return;
        }

        // 親子のローカル変換は維持し、既定位置への移動をルートにまとめる。
        engine::Vector3 createPosition = GetDefaultCreatePosition(editor, rootObject);
        rootObject->SetTranslate(createPosition);
        rootObject->UpdateLocalMatrix();
        rootObject->UpdateWorldMatrix();

        std::string createdName = rootObject->GetName();
        editor->AddEditorObjects(std::move(objects), commandLabel);
        DebugConsole::GetInstance()->AddLog("Create: " + createdName);
    }

    void CreatePrimitive(DebugEditor* editor, BaseScene* scene, const std::string& modelName, const std::string& baseName, bool useGameViewCursor, const engine::Vector3& scale = { 1.0f, 1.0f, 1.0f }) {
        // モデルと初期倍率を決めたら、追加位置・命名・Undoは共通の配置経路に任せる。
        if (!scene || !scene->GetObject3DCommon())
            return;

        auto object = std::make_unique<engine::Object3D>();
        object->Initialize(scene->GetObject3DCommon());
        ModelManager::GetInstance()->LoadModel(modelName);
        object->SetModel(modelName);
        object->SetClassName("Model");
        object->SetScale(scale);
        AddCreatedObject(editor, scene, std::move(object), baseName, "Create " + baseName, useGameViewCursor);
    }

    // 生成する箱の形状は共通。分類とUndo名はメニューの入口から渡す。
    void CreateInvisibleBox(DebugEditor* editor, BaseScene* scene, bool useGameViewCursor,
                            const char* category, const char* name, const char* command) {
        if (!scene || !scene->GetObject3DCommon())
            return;

        auto object = std::make_unique<engine::Object3D>();
        object->Initialize(scene->GetObject3DCommon());
        object->SetModel(nullptr);
        object->SetIsVisible(true);
        object->SetClassName("InvisibleBox");
        engine::Object3D::ColliderConfig colConfig;
        colConfig.type = ColliderType::Aabb;
        colConfig.size = { 1.0f, 1.0f, 1.0f };
        object->SetColliderConfig(colConfig);
        object->SetCollisionAttribute(CollisionCategories::Get(category));
        AddCreatedObject(editor, scene, std::move(object), name, command, useGameViewCursor);
    }

    void CreateTriggerBox(DebugEditor* editor, BaseScene* scene, bool useGameViewCursor) {
        CreateInvisibleBox(editor, scene, useGameViewCursor, "trigger", "Trigger_Box", "Create Trigger Box");
    }

    void CreateCollisionBox(DebugEditor* editor, BaseScene* scene, bool useGameViewCursor) {
        CreateInvisibleBox(editor, scene, useGameViewCursor, "ground", "Collision_Box", "Create Collision Box");
    }

    void CreateCinematicCamera(DebugEditor* editor, BaseScene* scene, bool useGameViewCursor) {
        // 演出カメラを編集しやすい可視モデルとして作り、保存分類をカメラへ固定する。
        if (!scene || !scene->GetObject3DCommon())
            return;

        auto object = std::make_unique<engine::Object3D>();
        object->Initialize(scene->GetObject3DCommon());
        object->SetModel(kCinematicCameraModel);
        object->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
        object->SetIsVisible(true);
        object->SetCastShadow(false);
        object->SetClassName("Camera");
        object->SetSaveCategory("Camera");
        object->SetTranslate({ 0.0f, 5.0f, -10.0f });
        AddCreatedObject(editor, scene, std::move(object), "Cinematic_Camera", "Create Cinematic Camera", useGameViewCursor);
    }

    void CreateDecal(DebugEditor* editor, BaseScene* scene, bool useGameViewCursor) {
        // 生成時は基準姿勢だけ指定し、画面上の配置位置は共通の配置経路へ任せる。
        if (!scene || !scene->GetObject3DCommon())
            return;

        DecalSpawnDesc desc;
        desc.name = "Surface_Decal";
        // 時間経過で消える一時VFXと区別し、配置物としてシーンに残す。
        desc.lifetime = 0.0f;
        desc.transient = false;
        std::unique_ptr<engine::Object3D> decal = DecalSystem::Create(
            scene->GetObject3DCommon(), { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, desc);
        AddCreatedObject(editor, scene, std::move(decal), "Surface_Decal", "Create Surface Decal", useGameViewCursor);
    }

    void CreateGimmick(DebugEditor* editor, BaseScene* scene, const std::string& type, bool useGameViewCursor) {
        if (!scene || !scene->GetObject3DCommon())
            return;

        auto gimmick = EditorObjectFactoryRegistry::Create(EditorObjectFactoryRegistry::Kind::Gimmick, type, scene->GetObject3DCommon());
        AddCreatedObject(editor, scene, std::move(gimmick), "Gimmick_" + type, "Create Gimmick " + type, useGameViewCursor);
    }

    void CreateEnemy(DebugEditor* editor, BaseScene* scene, const std::string& type, bool useGameViewCursor) {
        if (!scene || !scene->GetObject3DCommon())
            return;

        auto enemy = EditorObjectFactoryRegistry::Create(EditorObjectFactoryRegistry::Kind::Enemy, type, scene->GetObject3DCommon());
        AddCreatedObject(editor, scene, std::move(enemy), "Enemy_" + type, "Create Enemy " + type, useGameViewCursor);
    }

    void DrawCreateContextMenu(DebugEditor* editor, BaseScene* scene, bool useGameViewCursor) {
#ifdef USE_IMGUI
        // ゲームビュー上の配置はプレビューで位置を決め、Hierarchy側は選択位置を基準に作る。
        if (!editor || !scene)
            return;

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
            if (ImGui::MenuItem("ステージブロック"))
                CreatePrimitive(editor, scene, "Stages/block", "Block", useGameViewCursor);
            if (ImGui::MenuItem("Cube"))
                CreatePrimitive(editor, scene, "Primitives/cube", "Cube", useGameViewCursor);
            if (ImGui::MenuItem("Sphere"))
                CreatePrimitive(editor, scene, "Primitives/sphere", "Sphere", useGameViewCursor);
            if (ImGui::MenuItem("Cylinder"))
                CreatePrimitive(editor, scene, "Primitives/cylinder", "Cylinder", useGameViewCursor);
            ImGui::Separator();
            if (ImGui::MenuItem("トリガーボックス"))
                CreateTriggerBox(editor, scene, useGameViewCursor);
            if (ImGui::MenuItem("当たり判定ボックス"))
                CreateCollisionBox(editor, scene, useGameViewCursor);
            if (ImGui::MenuItem("演出用カメラ"))
                CreateCinematicCamera(editor, scene, useGameViewCursor);
            if (ImGui::MenuItem("サーフェスデカール"))
                CreateDecal(editor, scene, useGameViewCursor);
            ImGui::EndMenu();
        }

        // ゲーム側で登録された種類一覧を使い、表示名とFactoryへ渡すIDを分ける。
        if (ImGui::BeginMenu(ICON_FA_PUZZLE_PIECE " ギミック")) {
            for (const auto& type : EditorObjectFactoryRegistry::GetTypes(EditorObjectFactoryRegistry::Kind::Gimmick)) {
                if (ImGui::MenuItem(type.displayName.c_str()))
                    CreateGimmick(editor, scene, type.id, useGameViewCursor);
            }
            ImGui::EndMenu();
        }

        // 敵は登録済みのうち新規配置できる種類に絞り、同じ表示名もIDで区別する。
        if (ImGui::BeginMenu(ICON_FA_SKULL " 敵")) {
            using Registry = EditorObjectFactoryRegistry;
            for (const auto* type : Registry::GetTypes(Registry::Kind::Enemy, Registry::TypeList::Creation)) {
                ImGui::PushID(type->id.c_str());
                if (ImGui::MenuItem(type->displayName.c_str()))
                    CreateEnemy(editor, scene, type->id, useGameViewCursor);
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(ICON_FA_SAVE " プリセット")) {
            // 登録済みプリセットは生成Factoryと同じ導線へ渡し、まとめてUndoできるようにする。
            const auto& presets = PresetManager::GetInstance()->GetPresets();

            if (presets.empty()) {
                ImGui::TextDisabled("プリセットがありません");
            }

            for (const auto& [presetName, data] : presets) {
                if (ImGui::MenuItem(presetName.c_str())) {
                    if (!scene->GetObject3DCommon())
                        continue;
                    auto objects = PresetManager::GetInstance()->CreateObjectsFromPreset(presetName, scene->GetObject3DCommon());
                    AddCreatedObjects(editor, scene, std::move(objects), presetName, "Create Preset " + presetName, useGameViewCursor);
                }
            }

            ImGui::EndMenu();
        }
#endif
    }
}

void HierarchyWindow::Initialize(DebugEditor* editor) {
    // Scene操作と選択状態の所有者であるEditorを借用します。
    editor_ = editor;
    sceneAssetPanel_.Initialize(editor);
}

void HierarchyWindow::Draw() {
#ifdef USE_IMGUI
    // Asset Browserを先に描き、Sceneがある場合だけHierarchy本体を開く。
    editor_->DrawProjectWindow();

    if (editor_->GetSceneManager() == nullptr)
        return;
    BaseScene* currentScene = editor_->GetSceneManager()->GetCurrentScene();
    if (currentScene == nullptr)
        return;

    ImGui::Begin(ICON_FA_SITEMAP " Hierarchy###Hierarchy");

    // Game View等で変わった主選択を一覧へ反映する。古いポインターは比較だけに使う。
    engine::Object3D* selection = editor_->GetSelectedObject();
    if (selection != lastHierarchySelection_ || currentScene != lastHierarchyScene_) {
        // ツリーの展開と展開後のスクロール位置の確定に、それぞれ1フレーム使う。
        revealFrames_ = selection ? 2 : 0;
        if (selection) {
            // フィルターで選択対象が隠れないよう、選択変更時に一覧へ戻す。
            editor_->GetSearchFilterBuffer()[0] = '\0';
            currentCategoryFilter_ = 0;
            currentLayerFilter_ = 0;
        }
    }
    revealPath_.clear();
    // 選択したObjectが現シーンに属することを確かめてから、親をたどる。
    if (revealFrames_ > 0 && selection) {
        const auto& objects = currentScene->GetObjects();
        if (std::any_of(objects.begin(), objects.end(), [selection](const auto& object) {
            return object.get() == selection;
        })) {
            // 万一親が循環していても、表示追従で無限ループしない。
            for (auto* object = selection; object; object = object->GetParent()) {
                if (std::find(revealPath_.begin(), revealPath_.end(), object) != revealPath_.end())
                    break;
                revealPath_.push_back(object);
            }
        }
    }

    // Prefab編集中は通常Sceneと区別できる帯を表示し、保存・破棄の入口をここに置く。
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

    // 既存の行と競合しないよう、空白を右クリックしたときに作成メニューを開く。
    if (ImGui::BeginPopupContextWindow("HierarchyCreateContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        DrawCreateContextMenu(currentScene, false);
        ImGui::EndPopup();
    }

    // 旧ツール一覧は選択中のIEditableを切り替え、Inspectorの編集対象を変える。
    if (ImGui::CollapsingHeader(ICON_FA_COGS " システム設定 (System Settings)")) {
        ImGui::TextWrapped("ツールは上部の「ツール」メニューから分類別に開けます。ここは従来の一覧です。");
        IEditable* currentObj = EditorManager::GetInstance()->GetSelectedObject();

        if (ImGui::Selectable("  " ICON_FA_VIDEO " カメラ設定 (Camera)", currentObj == CameraEditor::GetInstance())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(CameraEditor::GetInstance());
        }

        if (editor_->GetLightEditor() && ImGui::Selectable("  " ICON_FA_LIGHTBULB " ライティング設定 (Lighting)", currentObj == editor_->GetLightEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetLightEditor());
        }

        if (editor_->GetPostEffectEditor() && ImGui::Selectable("  " ICON_FA_MAGIC " ポストエフェクト (Post Effect)", currentObj == editor_->GetPostEffectEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetPostEffectEditor());
        }

        if (editor_->GetGpuParticleEditor() &&
            ImGui::Selectable("  " ICON_FA_FIRE " GPUパーティクル (GPU Particle)", currentObj == editor_->GetGpuParticleEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetGpuParticleEditor());
        }

        if (editor_->GetVfxSequencerEditor() &&
            ImGui::Selectable("  " ICON_FA_FILM " VFXシーケンサー (VFX Sequencer)", currentObj == editor_->GetVfxSequencerEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetVfxSequencerEditor());
        }

        if (editor_->GetParticleEditor() && ImGui::Selectable("  " ICON_FA_WIND " 通常パーティクル (Particle)", currentObj == editor_->GetParticleEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetParticleEditor());
        }

        if (editor_->GetGhostRecorder() && ImGui::Selectable("  " ICON_FA_GHOST " ゴーストレコーダー (Ghost Recorder)", currentObj == editor_->GetGhostRecorder())) {
            // 対象ObjectをRecorderへ先に渡し、Inspectorの選択先をツールへ切り替えても記録対象を失わない。
            if (editor_->GetSelectedObject())
                editor_->GetGhostRecorder()->SetTarget(editor_->GetSelectedObject());
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetGhostRecorder());
        }

        if (editor_->GetGhostDirector() && ImGui::Selectable("  " ICON_FA_FILM " 演出タイムライン (Cinematic Timeline)", currentObj == editor_->GetGhostDirector())) {
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetGhostDirector());
        }

     if (editor_->GetMeshEffectEditor() && ImGui::Selectable("  " ICON_FA_MAGIC " メッシュエフェクト (Mesh Effect)", currentObj == editor_->GetMeshEffectEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetMeshEffectEditor());
        }

        if (editor_->GetDebrisEffectEditor() &&
            ImGui::Selectable("  " ICON_FA_CUBES " 3D破片エフェクト (Debris Effect)", currentObj == editor_->GetDebrisEffectEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetDebrisEffectEditor());
        }

        if (editor_->GetTrailEmitterEditor() &&
            ImGui::Selectable("  " ICON_FA_FIRE " トレイルエミッター (Trail Emitter)", currentObj == editor_->GetTrailEmitterEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetTrailEmitterEditor());
        }









        if (editor_->GetAnimatorControllerEditor() &&
            ImGui::Selectable("  " ICON_FA_RANDOM " Animator Controller", currentObj == editor_->GetAnimatorControllerEditor())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetAnimatorControllerEditor());
        }

        if (editor_->GetTextSpriteGenerator() &&
            ImGui::Selectable("  " ICON_FA_FONT " テキストPNG生成 (Text PNG)", currentObj == editor_->GetTextSpriteGenerator())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetTextSpriteGenerator());
        }

        if (editor_->GetAssetAuditWindow() && ImGui::Selectable("  " ICON_FA_SEARCH " アセット監査 (Asset Audit)", currentObj == editor_->GetAssetAuditWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetAssetAuditWindow());
        }

        if (editor_->GetAudioSettingsWindow() &&
            ImGui::Selectable("  " ICON_FA_MUSIC " 音声設定 (Audio Settings)", currentObj == editor_->GetAudioSettingsWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetAudioSettingsWindow());
        }

        if (editor_->GetExecutablePackageWindow() &&
            ImGui::Selectable("  " ICON_FA_BOX_OPEN " 実行ファイルセット (Executable Package)", currentObj == editor_->GetExecutablePackageWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetExecutablePackageWindow());
        }

        if (editor_->GetCaptureToolWindow() &&
            ImGui::Selectable("  " ICON_FA_CAMERA " キャプチャツール (Capture Tool)", currentObj == editor_->GetCaptureToolWindow())) {
            editor_->SetSelectedObject(nullptr);
            EditorManager::GetInstance()->SetSelectedObject(editor_->GetCaptureToolWindow());
        }

        if (editor_->GetGameDataDebugEditor() &&
            ImGui::Selectable("  " ICON_FA_DATABASE " 内部データ編集 (Game Data)", currentObj == editor_->GetGameDataDebugEditor())) {
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

    // SceneAssetの作成・切替と、SceneWorkspace固有の階層表示を先に置く。
    sceneAssetPanel_.DrawList();

    if (SceneWorkspace* workspace = editor_->GetSceneWorkspace()) {
        ImGui::Separator();
        workspace->DrawHierarchyPanel();
    }

    ImGui::Separator();
    ImGui::Separator();

    // 検索、Object種類、Layerの絞り込みはHierarchy表示だけに適用する。
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

    // 一覧の絞り込みとは別に、複数選択の操作対象と補助表示の設定を示します。
    const auto& selectedObjects = editor_->GetSelectedObjects();
    engine::Object3D* primaryObject = editor_->GetSelectedObject();

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

    // ここは表示方法だけを変え、Scene内の親子構造には触れません。
    ImGui::Checkbox("種類ごとにまとめる", &groupHierarchy_);

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("一覧だけを分類します。親子関係やシーンの保存内容は変わりません。");
    }

    std::string filterStr = editor_->GetSearchFilterBuffer();
    filterStr = ToLowerAscii(filterStr);


    // 検索中は一致Objectを平坦に表示し、通常時だけ親子ツリーを描く。
    if (!filterStr.empty()) {
        ImGui::TextColored(ImVec4(0, 1, 1, 1), ICON_FA_SEARCH_PLUS " 検索結果:");
        auto& objects = currentScene->GetObjects();

        for (auto& obj : objects) {
            if (editor_->IsPrefabEditMode() && !editor_->IsPrefabEditObject(obj.get()))
                continue;
            std::string name = obj->GetName();
            if (name.empty())
                continue;
            if (!HasMatchingCategory(obj.get()))
                continue;
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

        // 空白領域へのAssetドロップは種類ごとにPreviewまたはScene Objectを生成する。
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
                const std::string modelName = ReadEditorAssetDragPath(payload->Data, payload->DataSize);
                engine::Object3DCommon* common = currentScene->GetObject3DCommon();

                if (common && !modelName.empty()) {
                    ModelManager::GetInstance()->LoadModel(modelName);
                    auto newObj = std::make_unique<engine::Object3D>();
                    newObj->Initialize(common);
                    newObj->SetModel(modelName);
                    newObj->SetClassName("Model");
                    newObj->SetName("Preview_" + modelName);
                    newObj->UpdateLocalMatrix();
                    newObj->UpdateWorldMatrix();
                    editor_->SetPreviewObject(std::move(newObj));
                }
            }

            // Presetは複数Objectになり得るので、単独モデルの仮配置とは別の生成経路を使います。
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PRESET_ASSET")) {
                const char* presetName = (const char*)payload->Data;
                const auto& presets = PresetManager::GetInstance()->GetPresets();

                if (presets.count(presetName) > 0) {
                    engine::Object3DCommon* common = currentScene->GetObject3DCommon();

                    if (common) {
                        auto objects = PresetManager::GetInstance()->CreateObjectsFromPreset(presetName, common);
                        AddCreatedObjects(editor_, currentScene, std::move(objects), presetName, "Create Preset " + std::string(presetName), false);
                    }
                }
            }

            // 演出カメラはSceneに保存できるCameraとして作り、編集履歴にも登録します。
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CINEMATIC_CAMERA_ASSET")) {
                engine::Object3DCommon* common = currentScene ? currentScene->GetObject3DCommon() : nullptr;

                if (common) {
                    auto newObj = std::make_unique<engine::Object3D>();
                    newObj->Initialize(common);
                    newObj->SetModel(kCinematicCameraModel);
                    newObj->SetName("Camera_Cinematic");
                    newObj->SetClassName("Camera");
                    newObj->SetSaveCategory("Camera");
                    newObj->SetCastShadow(false);
                    newObj->SetTranslate({ 0.0f, 5.0f, -10.0f });
                    newObj->UpdateLocalMatrix();
                    newObj->UpdateWorldMatrix();
                    editor_->AddEditorObject(std::move(newObj), "Create Cinematic Camera");
                }
            }

            // 粒子素材は配置位置を調整できる仮Objectへ紐付け、確定前はSceneへ保存しません。
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PARTICLE_ASSET")) {
                const char* presetName = (const char*)payload->Data;
                engine::Object3DCommon* common = currentScene ? currentScene->GetObject3DCommon() : nullptr;

                if (common) {
                    auto newObj = std::make_unique<engine::Object3D>();
                    newObj->Initialize(common);
                    newObj->SetModel("Stages/block");
                    newObj->SetName("VFX_" + std::string(presetName));
                    newObj->SetClassName("GPUParticle");
                    newObj->SetGpuParticleName(presetName);

                    // エディタ上で分かりやすいように色をオレンジ半透明っぽくする
                    newObj->SetColor({1.0f, 0.5f, 0.0f, 0.5f});
                    newObj->SetBlendMode(BlendMode::Normal);

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

        // グループ表示はルートObjectだけを分類し、親子関係そのものは変えない。
        if (groupHierarchy_) {
            // 親を持つObjectは既存のツリー内に表示する。ここではルートだけを仮想フォルダへ振り分ける。
            std::array<std::vector<engine::Object3D*>, static_cast<size_t>(HierarchyDisplayGroup::Count)> groups;

            for (auto& obj : objects) {
                if (!obj || obj->GetParent() || !HasMatchingCategory(obj.get()))
                    continue;
                if (editor_->IsPrefabEditMode() && !editor_->IsPrefabEditObject(obj.get()))
                    continue;
                groups[static_cast<size_t>(GetHierarchyDisplayGroup(obj.get()))].push_back(obj.get());
            }

            for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
                const auto& rows = groups[groupIndex];
                if (rows.empty())
                    continue;
                ImGui::PushID(static_cast<int>(groupIndex));
                if (!revealPath_.empty() && std::find(rows.begin(), rows.end(), revealPath_.back()) != rows.end())
                    ImGui::SetNextItemOpen(true);
                const bool groupOpen = ImGui::TreeNodeEx("Group", ImGuiTreeNodeFlags_DefaultOpen,
                    "%s (%zu)", kHierarchyGroupNames[groupIndex], rows.size());
                if (groupOpen) {
                    std::map<std::string, std::vector<engine::Object3D*>> subgroups;

                    for (engine::Object3D* object : rows) {
                        subgroups[GetHierarchyDisplaySubtype(object)].push_back(object);
                    }

                    // 1件だけならサブグループを省き、分類を開くだけで対象行を見せる。
                    if (subgroups.size() == 1 && rows.size() == 1) {
                        DrawHierarchyNode(rows.front());
                    } else {
                        for (const auto& [subtype, members] : subgroups) {
                            if (!revealPath_.empty() && std::find(members.begin(), members.end(), revealPath_.back()) != members.end())
                                ImGui::SetNextItemOpen(true);
                            const bool subtypeOpen = ImGui::TreeNodeEx(subtype.c_str(), 0,
                                "%s (%zu)", subtype.c_str(), members.size());
                            if (subtypeOpen) {
                                for (engine::Object3D* object : members)
                                    DrawHierarchyNode(object);
                                ImGui::TreePop();
                            }
                        }
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        } else {
            for (auto& obj : objects) {
                if (editor_->IsPrefabEditMode() && !editor_->IsPrefabEditObject(obj.get()))
                    continue;
                if (obj->GetParent() == nullptr)
                    DrawHierarchyNode(obj.get());
            }
        }
    }

    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.8f, 0.6f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.8f, 0.7f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.8f, 0.8f, 0.8f));

    // よく使う衝突用・演出用Objectは固定設定で作り、Undo対象としてEditorへ渡す。
    if (ImGui::Button(ICON_FA_BOLT " 透明ボックス生成 (トリガー用)", ImVec2(-1, 40))) {
        engine::Object3DCommon* common = currentScene->GetObject3DCommon();

        if (common) {
            auto newObj = std::make_unique<engine::Object3D>();
            newObj->Initialize(common);
            newObj->SetModel(nullptr);
            newObj->SetIsVisible(true);
            newObj->SetClassName("InvisibleBox");
            newObj->SetName("Trigger_Box");
            engine::Object3D::ColliderConfig colConfig;
            colConfig.type = ColliderType::Aabb;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            newObj->SetColliderConfig(colConfig);
            newObj->SetCollisionAttribute(CollisionCategories::Get("trigger"));
            newObj->SetTranslate({ 0, 2.0f, 0 });
            editor_->AddEditorObject(std::move(newObj), "Create Trigger Box");
        }
    }

    if (ImGui::Button(ICON_FA_SHIELD_ALT " 透明ボックス生成 (当たり判定用)", ImVec2(-1, 40))) {
        engine::Object3DCommon* common = currentScene->GetObject3DCommon();

        if (common) {
            auto newObj = std::make_unique<engine::Object3D>();
            newObj->Initialize(common);
            newObj->SetModel(nullptr);
            newObj->SetIsVisible(true);
            newObj->SetClassName("InvisibleBox");
            newObj->SetName("collision_Box");
            engine::Object3D::ColliderConfig colConfig;
            colConfig.type = ColliderType::Aabb;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            newObj->SetColliderConfig(colConfig);
            newObj->SetCollisionAttribute(CollisionCategories::Get("ground"));
            newObj->SetTranslate({ 0, 2.0f, 0 });
            editor_->AddEditorObject(std::move(newObj), "Create Collision Box");
        }
    }

    if (ImGui::Button(ICON_FA_VIDEO " 演出用カメラ生成 (Cinematic)", ImVec2(-1, 40))) {
        engine::Object3DCommon* common = currentScene->GetObject3DCommon();

        if (common) {
            auto newObj = std::make_unique<engine::Object3D>();
            newObj->Initialize(common);
            newObj->SetModel(kCinematicCameraModel);
            newObj->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
            newObj->SetIsVisible(true);
            newObj->SetClassName("Camera");
            newObj->SetSaveCategory("Camera");
            newObj->SetCastShadow(false);
            newObj->SetName("Cinematic_Camera_01");
            newObj->SetTranslate({ 0, 5.0f, -10.0f });
            newObj->UpdateWorldMatrix();
            editor_->AddEditorObject(std::move(newObj), "Create Cinematic Camera");
        }
    }

    ImGui::PopStyleColor(3);
    ImGui::Dummy(ImVec2(0, 50));
    ImGui::TextDisabled(ICON_FA_UNLINK " (ここにドロップして親解除)");

    // 親解除時は元のワールド姿勢を保持し、結果を編集履歴へ登録する。
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_OBJ")) {
            engine::Object3D* sourceObj = *(engine::Object3D**)payload->Data;

            if (sourceObj->GetParent() != nullptr) {
                nlohmann::json beforeState = editor_->CaptureObjectState(sourceObj);
                engine::Matrix4x4 worldMat = sourceObj->GetWorldMatrix();
                // 親を外した後のローカル変換に、旧ワールド行列を分解した位置と倍率を使う。
                sourceObj->SetParent(nullptr, true);
                engine::Vector3 t, rDeg, s;
                ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], &t.x, &rDeg.x, &s.x);
                sourceObj->SetTranslate(t);
                sourceObj->SetQuaternionWithEulerHint(sourceObj->GetTransform()->quaternion,
                    { ToRadians(rDeg.x), ToRadians(rDeg.y), ToRadians(rDeg.z) });
                sourceObj->SetScale(s);
                sourceObj->UpdateWorldMatrix();
                editor_->RegisterObjectEdited(sourceObj, beforeState, "Unparent Object");
            }
        }

        ImGui::EndDragDropTarget();
    }

    sceneAssetPanel_.DrawDialogs();
    // 一覧内のクリックは既に見えているため、次フレームにスクロールを発生させない。
    lastHierarchySelection_ = editor_->GetSelectedObject();
    lastHierarchyScene_ = currentScene;
    if (revealFrames_ > 0)
        --revealFrames_;
    ImGui::End();
#endif
}

void HierarchyWindow::OpenCreateSceneDialog() {
    sceneAssetPanel_.OpenCreateSceneDialog();
}

void HierarchyWindow::OpenSceneAssetFromMenu(const std::string& filename) {
    sceneAssetPanel_.RequestOpen(filename);
}

void HierarchyWindow::DrawCreateContextMenu(BaseScene* scene, bool useGameViewCursor) {
#ifdef USE_IMGUI
    // 配置座標の基準をGame Viewカーソルか選択Objectに応じて切り替えます。
    ::DrawCreateContextMenu(editor_, scene, useGameViewCursor);
#else
    (void)scene;
    (void)useGameViewCursor;
#endif
}

void HierarchyWindow::DrawHierarchyNode(engine::Object3D* obj) {
#ifdef USE_IMGUI
    if (!obj)
        return;
    if (!HasMatchingCategory(obj))
        return;

    //  IDの衝突を防ぐためにPushIDを使用
    ImGui::PushID(obj);

    //  AllowItemOverlapを追加して、ツリーと同じ行にボタンを押せるようにする
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (editor_->IsObjectSelected(obj))
        nodeFlags |= ImGuiTreeNodeFlags_Selected;

    std::string name = obj->GetName();
    if (name.empty())
        name = "NoName";
    if (obj->GetClassName() == "InvisibleBox")
        name = "[Trigger] " + name;
    if (!obj->GetTag().empty()) {
        name += " #" + obj->GetTag();
    }

    const std::string& layerName = obj->GetLayer();

    if (!layerName.empty() && layerName != "Default") {
        name += " [" + layerName + "]";
    }

    if (obj->IsEditorTemporarilyHidden()) {
        name += " [一時非表示]";
    }

    // 左側にツリーノードを描画
    if (std::find(revealPath_.begin(), revealPath_.end(), obj) != revealPath_.end())
        ImGui::SetNextItemOpen(true);
    const bool showCameraBadge = obj->IsCameraObject();
    std::string cameraRow;
    if (showCameraBadge) {
        const auto badge = GetCameraObjectBadge(obj->GetSceneCameraSettings());
        cameraRow = std::string(badge.label) + ' ' + name;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(badge.color));
    }
    const bool nodeOpen = ImGui::TreeNodeEx((void*)obj, nodeFlags, "%s",
        showCameraBadge ? cameraRow.c_str() : name.c_str());
    if (showCameraBadge)
        ImGui::PopStyleColor();

    // 展開直後はスクロール範囲が前フレームのままなので、翌フレームも位置を合わせる。
    if (!revealPath_.empty() && obj == revealPath_.front())
        ImGui::SetScrollHereY(0.5f);

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
        ImGui::SetDragDropPayload("HIERARCHY_OBJ", &obj, sizeof(engine::Object3D*));
        ImGui::Text("Move %s", name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_OBJ")) {
            engine::Object3D* sourceObj = *(engine::Object3D**)payload->Data;

            // 自分自身や既存の親へのドロップで履歴を増やさない。親変更時はワールド姿勢を保持する。
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
    if (!isVisible)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // 非表示時はグレーアウト
    if (ImGui::Button(eyeIcon)) {
        nlohmann::json beforeState = editor_->CaptureObjectState(obj);
        obj->SetIsVisible(!isVisible);
        editor_->RegisterObjectEdited(obj, beforeState, "Toggle Visibility");
    }

    if (!isVisible)
        ImGui::PopStyleColor();

    ImGui::SameLine();

    // 2. ロックトグル (南京錠アイコン)
    bool isLocked = obj->GetIsLocked();
    const char* lockIcon = isLocked ? ICON_FA_LOCK : ICON_FA_UNLOCK;
    if (isLocked)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); // ロック時は赤色
    if (ImGui::Button(lockIcon)) {
        nlohmann::json beforeState = editor_->CaptureObjectState(obj);
        obj->SetIsLocked(!isLocked);
        editor_->RegisterObjectEdited(obj, beforeState, "Toggle Lock");
    }

    if (isLocked)
        ImGui::PopStyleColor();

    // 展開時だけ現在のシーンから直属の子を探し、同じカテゴリ・レイヤー条件で描画する。
    if (nodeOpen) {
        if (editor_->GetSceneManager() && editor_->GetSceneManager()->GetCurrentScene()) {
            auto& allObjs = editor_->GetSceneManager()->GetCurrentScene()->GetObjects();

            for (auto& child : allObjs) {
                if (child->GetParent() == obj)
                    DrawHierarchyNode(child.get());
            }
        }

        ImGui::TreePop();
    }

    ImGui::PopID(); // PushIDの解除
#endif
}

bool HierarchyWindow::HasMatchingCategory(engine::Object3D* obj) {
    // 表示カテゴリとLayerの条件を共に満たすObjectだけHierarchyへ出します。
    if (!obj || obj->IsEditorInternal())
        return false;

    // カメラは保存カテゴリに加え、Objectが持つカメラ設定でも判定する。
    const std::string cat = obj->GetSaveCategory();
    bool categoryMatches = currentCategoryFilter_ == 0;
    if (currentCategoryFilter_ == 1 && cat == "Player")
        categoryMatches = true;
    if (currentCategoryFilter_ == 2 && cat == "Enemy")
        categoryMatches = true;
    if (currentCategoryFilter_ == 3 && cat == "Object")
        categoryMatches = true;
    if (currentCategoryFilter_ == 4 && (cat == "Camera" || obj->IsCameraObject()))
        categoryMatches = true;

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

    if (categoryMatches && layerMatches)
        return true;

    // 条件を満たす子がいるときは、子への経路として親も表示する。
    for (engine::Object3D* child : obj->GetChildren()) {
        if (HasMatchingCategory(child))
            return true;
    }

    return false;
}
