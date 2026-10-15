#define NOMINMAX
#include "DebugEditor.h"
#include "SceneManager.h"    
#include "BaseScene.h"      
#include "Object3d.h"
#include "imgui.h"
#include <fstream>
#include <string>
#include <vector>
#include "json.hpp"
#include "ImGuizmo.h"
#include "CameraManager.h"
#include "WinApp.h"
#include "Math.h"
#include "DirectXCommon.h"
#include "CollisionConfig.h"
#include "ModelManager.h"      
#include "InputManager.h"    
#include <cmath>
#include <cassert> 
#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include "GhostRecorder.h" 
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "Transform.h"
#include "ParticleManager.h"
#include "EditorManager.h"
#include "PostEffectEditor.h"
#include "SpriteDebugEditor.h"
#include "ParticleEditor.h"
#include "GPUParticleEditor.h"
#include "VFXSequencerEditor.h"
#include "LightEditor.h"      
#include "IconsFontAwesome5.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <DebugConsole.h>
#include <CollisionManager.h>
#include <filesystem> // ファイル操作用
#include <BulletManager.h>
#include "engine/utility/SystemDebug/editor/core/preset/PresetManager.h"
#include <PresetEditor.h>
#include <MeshEffectManager.h>
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "GimmickFactory.h"
#include "ItemFactory.h"
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <utility>
namespace fs = std::filesystem;

namespace {
// Undo/Redoの保存状態から同じ動的型のObjectを作り直し、型情報が不正なら復元を中止します。
std::unique_ptr<engine::Object3d> CreateEditorObjectFromState(Object3dCommon* common, const nlohmann::json& state) {
    if (!common || !state.is_object() || !state.contains("_editorRuntimeType") ||
        !state["_editorRuntimeType"].is_string()) {
        return nullptr;
    }

    const std::string expectedType = state["_editorRuntimeType"].get<std::string>();
    if (expectedType.empty()) return nullptr;
    const std::string enemyType = state.value("enemyType", std::string{});
    const std::string gimmickType = state.value("gimmickType", std::string{});
    const std::string itemType = state.value("itemType", std::string{});
    std::unique_ptr<engine::Object3d> object;

    if (expectedType == typeid(engine::Object3d).name()) {
        object = std::make_unique<engine::Object3d>();
        object->Initialize(common);
    } else if (!enemyType.empty()) {
        object = EnemyFactory::GetInstance()->CreateEnemy(enemyType, common);
    } else if (!gimmickType.empty()) {
        object = GimmickFactory::GetInstance()->CreateGimmick(gimmickType, common);
    } else if (!itemType.empty()) {
        object = ItemFactory::GetInstance()->CreateItem(itemType, common);
    }

    // 不明な型やFactoryの代替オブジェクトを、元の派生型として扱わない。
    if (!object || expectedType != typeid(*object).name()) {
        return nullptr;
    }
    return object;
}

std::string ToLowerForGroundDefault(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool ContainsGroundKeywordForDefault(const std::string& value) {
    return value.find("block") != std::string::npos ||
        value.find("floor") != std::string::npos ||
        value.find("ground") != std::string::npos ||
        value.find("platform") != std::string::npos ||
        value.find("terrain") != std::string::npos ||
        value.find("wall") != std::string::npos;
}

bool ShouldApplyGroundDefaults(engine::Object3d* object) {
    // 地形として扱えるモデルだけに初期Colliderを補い、敵・演出・子Meshには付けない。
    if (!object) {
        return false;
    }

    const std::string className = ToLowerForGroundDefault(object->GetClassName());
    if (className == "enemy" || className == "player" || className == "gimmick" ||
        className == "item" || className == "gpuparticle" || className == "cinematiccamera" ||
        className == "spritecard" || className == "meshroot" || className == "meshpart") {
        return false;
    }

    const std::string modelName = ToLowerForGroundDefault(object->GetModelName());
    if (modelName.empty()) {
        return false;
    }
    if (modelName.rfind("characters/", 0) == 0 || modelName.rfind("item/", 0) == 0) {
        return false;
    }
    if (modelName == "primitives/plane" || modelName.find("portal") != std::string::npos) {
        return false;
    }

    return modelName.rfind("stages/", 0) == 0 ||
        modelName == "primitives/cube" ||
        ContainsGroundKeywordForDefault(modelName);
}

void ApplyGroundDefaultsForGeneratedModel(engine::Object3d* object) {
    // 自動生成した地形の未設定項目だけに地面用の衝突属性を与える。
    if (!ShouldApplyGroundDefaults(object)) {
        return;
    }

    auto colliderConfig = object->GetColliderConfig();
    if (colliderConfig.type == ColliderType::kNone) {
        colliderConfig.type = ColliderType::kAABB;
        object->SetColliderConfig(colliderConfig);
    }
    if (object->GetCollisionAttribute() == 0) {
        object->SetCollisionAttribute(CollisionCategories::Get("ground"));
    }
    if (object->GetCollisionMask() == 0) {
        object->SetCollisionMask(0xFFFFFFFFu);
    }
}
}




// ========================================================================
// 初期化
// ========================================================================

nlohmann::json DebugEditor::CaptureObjectState(engine::Object3d* object) const {
    nlohmann::json state = nlohmann::json::object();
    if (!object) return state;
    if (object == selectedObject_ && !IsObjectInCurrentScene(object)) return state;

    state = object->ExportToJson();
    // Editor の Undo/Redo はこの実行型を復元できる場合だけ再生成する。
    state["_editorRuntimeType"] = typeid(*object).name();
    state["name"] = object->GetName();
    state["parentName"] = object->GetParent() ? object->GetParent()->GetName() : "";
    state["parentGuid"] = object->GetParent() ? object->GetParent()->EnsurePersistentGuid() : "";
    state["isStatic"] = object->IsStatic();
    return state;
}

std::vector<DebugEditor::ObjectStateSnapshot> DebugEditor::CaptureObjectStates(const std::vector<engine::Object3d*>& objects) const {
    // 現在 Scene に属する Object だけ記録し、破棄済みポインタを Undo 対象にしない。
    std::vector<ObjectStateSnapshot> snapshots;
    snapshots.reserve(objects.size());
    for (engine::Object3d* object : objects) {
        if (!object || !IsObjectInCurrentScene(object)) {
            continue;
        }
        ObjectStateSnapshot snapshot;
        snapshot.object = object;
        snapshot.beforeState = CaptureObjectState(object);
        if (snapshot.beforeState.is_object()) {
            snapshots.push_back(std::move(snapshot));
        }
    }
    return snapshots;
}

void DebugEditor::ApplyObjectState(engine::Object3d* object, const nlohmann::json& state) {
    // JSON の属性を戻した後、親は GUID を優先して探し、旧データだけ名前へフォールバックする。
    if (!object || !state.is_object()) return;

    if (state.contains("name")) {
        object->SetName(state["name"].get<std::string>());
    }
    object->ImportFromJson(state);
    if (state.contains("isStatic")) {
        object->SetStatic(state["isStatic"].get<bool>());
    }

    engine::Object3d* parent = nullptr;
    if (state.contains("parentGuid") && state["parentGuid"].is_string()) {
        parent = FindObjectByPersistentGuid(state["parentGuid"].get<std::string>());
        if (parent == object) parent = nullptr;
    }
    if (!parent && state.contains("parentName")) {
        std::string parentName = state["parentName"].get<std::string>();
        if (!parentName.empty()) {
            parent = FindObjectByName(parentName);
            if (parent == object) parent = nullptr;
        }
    }
    object->SetParent(parent);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();
}

engine::Object3d* DebugEditor::FindObjectByName(const std::string& name) const {
    // 旧コマンド用の代替検索なので、対象は現在開いている Scene に限る。
    if (name.empty() || !sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (const auto& object : objects) {
        if (object && object->GetName() == name) {
            return object.get();
        }
    }
    return nullptr;
}

engine::Object3d* DebugEditor::FindObjectByPersistentGuid(const std::string& guid) const {
    // 名前が変わった後でも同じ Object を復元できるよう永続 ID で検索する。
    if (guid.empty() || !sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;
    return sceneManager_->GetCurrentScene()->FindObjectByPersistentGuid(guid);
}

engine::Object3d* DebugEditor::AddObjectFromState(const nlohmann::json& state) {
    // 保存時の実行型を再生成できる場合だけ追加し、GUID 衝突時は新しい ID を振る。
    if (!sceneManager_ || !sceneManager_->GetCurrentScene() || !state.is_object()) return nullptr;

    Object3dCommon* common = sceneManager_->GetCurrentScene()->GetObject3dCommon();
    if (!common) return nullptr;

    auto object = CreateEditorObjectFromState(common, state);
    if (!object) {
        DebugConsole::GetInstance()->AddLog("Object restore skipped: runtime type cannot be reconstructed.");
        return nullptr;
    }
    ApplyObjectState(object.get(), state);
    if (FindObjectByPersistentGuid(object->GetPersistentGuid())) {
        object->RegeneratePersistentGuid();
    }

    engine::Object3d* raw = object.get();
    sceneManager_->GetCurrentScene()->AddObjectImmediate(std::move(object));
    if (auto* enemy = dynamic_cast<BaseEnemy*>(raw)) {
        enemy->BindSceneSpawnContext(sceneManager_->GetCurrentScene());
    }
    return raw;
}

std::unique_ptr<engine::Object3d> DebugEditor::RemoveObjectImmediate(engine::Object3d* object) {
    // Scene 配列から即時に切り離し、所有権を呼び出し元へ返す。
    if (!object || !sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;
    return sceneManager_->GetCurrentScene()->TakeObjectImmediate(object);
}

void DebugEditor::RegisterCommand(const EditorCommand& command) {
    // 同じコマンドを逆方向にも適用し、共通 Transaction 履歴へ登録する。
    EditorTransaction transaction;
    transaction.label = command.label;
    transaction.undo = [this, command]() { ApplyEditorCommand(command, true); };
    transaction.redo = [this, command]() { ApplyEditorCommand(command, false); };
    EditorTransactionManager::GetInstance()->Register(std::move(transaction));
}

void DebugEditor::RegisterObjectEdited(engine::Object3d* object, const std::string& label) {
    // 単体編集も状態差分を使う共通経路へ渡す。
    if (!object) return;
    if (object == selectedObject_ && !IsObjectInCurrentScene(object)) return;
    RegisterObjectEdited(object, CaptureObjectState(object), label);
}

void DebugEditor::RegisterObjectEdited(engine::Object3d* object, const nlohmann::json& beforeState, const std::string& label) {
    // 前後の JSON が同じなら履歴を作らず、実際の変更だけ Dirty にする。
    if (!object || !beforeState.is_object()) return;
    if (object == selectedObject_ && !IsObjectInCurrentScene(object)) return;

    nlohmann::json afterState = CaptureObjectState(object);
    if (beforeState == afterState) return;

    EditorCommand command;
    command.type = EditorCommandType::ObjectEdited;
    command.label = label;
    command.beforeState = beforeState;
    command.afterState = afterState;
    command.beforeName = beforeState.value("name", object->GetName());
    command.afterName = afterState.value("name", object->GetName());
    RegisterCommand(command);
    MarkDirtyForObject(object);
}

void DebugEditor::RegisterObjectsEdited(const std::vector<ObjectStateSnapshot>& beforeStates, const std::string& label) {
    // 一括編集の前後を GUID に結び付ける。名前変更後も同じ対象へ Undo/Redo を適用するための記録。
    struct StateChange {
        std::string guid;
        std::string beforeName;
        std::string afterName;
        nlohmann::json beforeState;
        nlohmann::json afterState;
    };

    std::vector<StateChange> changes;
    changes.reserve(beforeStates.size());
    for (const ObjectStateSnapshot& snapshot : beforeStates) {
        if (!snapshot.object || !snapshot.beforeState.is_object() || !IsObjectInCurrentScene(snapshot.object)) {
            continue;
        }

        nlohmann::json afterState = CaptureObjectState(snapshot.object);
        if (!afterState.is_object() || snapshot.beforeState == afterState) {
            continue;
        }

        StateChange change;
        change.guid = snapshot.beforeState.value("guid", snapshot.object->GetPersistentGuid());
        change.beforeName = snapshot.beforeState.value("name", snapshot.object->GetName());
        change.afterName = afterState.value("name", snapshot.object->GetName());
        change.beforeState = snapshot.beforeState;
        change.afterState = std::move(afterState);
        changes.push_back(std::move(change));
        MarkDirtyForObject(snapshot.object);
    }

    if (changes.empty()) {
        return;
    }

    auto applyStates = [this, changes](bool undo) {
        std::vector<engine::Object3d*> appliedObjects;
        appliedObjects.reserve(changes.size());
        for (const StateChange& change : changes) {
            const std::string& primaryName = undo ? change.afterName : change.beforeName;
            const std::string& fallbackName = undo ? change.beforeName : change.afterName;
            engine::Object3d* object = FindObjectByPersistentGuid(change.guid);
            if (!object) object = FindObjectByName(primaryName);
            if (!object) {
                object = FindObjectByName(fallbackName);
            }
            if (!object) {
                continue;
            }

            // 保存先カテゴリが変わる編集では、復元前のカテゴリ側も更新対象に残します。
            MarkDirtyForObject(object);
            ApplyObjectState(object, undo ? change.beforeState : change.afterState);
            MarkDirtyForObject(object);
            appliedObjects.push_back(object);
        }
        if (!appliedObjects.empty()) {
            ClearObjectSelection();
            for (engine::Object3d* object : appliedObjects) {
                AddSelectedObject(object);
            }
            EditorManager::GetInstance()->SetSelectedObject(this);
        }
    };

    EditorTransaction transaction;
    transaction.label = label;
    transaction.undo = [applyStates]() { applyStates(true); };
    transaction.redo = [applyStates]() { applyStates(false); };
    EditorTransactionManager::GetInstance()->Register(std::move(transaction));
}

void DebugEditor::AddEditorObject(std::unique_ptr<engine::Object3d> object, const std::string& label) {
    if (!object || !sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    engine::Object3d* raw = object.get();
    raw->EnsurePersistentGuid();
    if (FindObjectByPersistentGuid(raw->GetPersistentGuid())) {
        raw->RegeneratePersistentGuid();
    }
    if (prefabEditMode_ && prefabEditRoot_ && raw != prefabEditRoot_ && raw->GetParent() == nullptr) {
        // Prefab Modeで作成したRoot Objectは、編集対象Prefabの直下へ自動的に追加します。
        raw->SetParent(prefabEditRoot_, true);
    }
    ApplyGroundDefaultsForGeneratedModel(raw);
    nlohmann::json afterState = CaptureObjectState(raw);

    sceneManager_->GetCurrentScene()->AddObjectImmediate(std::move(object));
    SetSelectedObject(raw);
    EditorManager::GetInstance()->SetSelectedObject(this);

    EditorCommand command;
    command.type = EditorCommandType::ObjectCreated;
    command.label = label;
    command.afterState = afterState;
    command.afterName = afterState.value("name", raw->GetName());
    RegisterCommand(command);
    MarkDirtyForObject(raw);
}

void DebugEditor::AddEditorObjects(std::vector<std::unique_ptr<engine::Object3d>> objects, const std::string& label) {
    // 複数配置を Scene へ移し、最後に先頭 Object を選択して編集を続けられるようにする。
    if (objects.empty()) return;

    engine::Object3d* root = objects.front().get();
    for (auto& object : objects) {
        if (object) {
            AddEditorObject(std::move(object), label);
        }
    }

    if (root) {
        SetSelectedObject(root);
        EditorManager::GetInstance()->SetSelectedObject(this);
    }
}

void DebugEditor::TrackInspectorEdit(const std::vector<ObjectStateSnapshot>& beforeStates) {
#ifdef USE_IMGUI
    // ドラッグ中の各フレームを履歴にせず、操作開始と終了を一つの編集として記録する。
    if (!selectedObject_ || beforeStates.empty()) {
        return;
    }
    if (!IsObjectInCurrentScene(selectedObject_)) {
        ClearInvalidSelectedObject();
        return;
    }

    bool changedThisFrame = false;
    for (const ObjectStateSnapshot& snapshot : beforeStates) {
        if (!snapshot.object || !snapshot.beforeState.is_object() || !IsObjectInCurrentScene(snapshot.object)) {
            continue;
        }
        if (snapshot.beforeState != CaptureObjectState(snapshot.object)) {
            changedThisFrame = true;
            break;
        }
    }
    bool active = ImGui::IsAnyItemActive();

    if (changedThisFrame && !hasInspectorEditStart_) {
        inspectorEditStartStates_ = beforeStates;
        hasInspectorEditStart_ = true;
    }

    if (changedThisFrame) {
        for (const ObjectStateSnapshot& snapshot : beforeStates) {
            if (snapshot.object && IsObjectInCurrentScene(snapshot.object)) {
                MarkDirtyForObject(snapshot.object);
            }
        }
    }

    if (!active && hasInspectorEditStart_) {
        RegisterObjectsEdited(inspectorEditStartStates_,
            inspectorEditStartStates_.size() > 1 ? "Inspector Multi Edit" : "Inspector Edit");
        hasInspectorEditStart_ = false;
        inspectorEditStartStates_.clear();
    }
#endif
}

void DebugEditor::MarkDirtyForObject(engine::Object3d* object) {
    // Prefab 編集中の変更は Scene Asset ではなく Prefab の未保存状態へ振り分ける。
    if (!object) {
        MarkDirty(SaveMode::Object);
        return;
    }
    if (prefabEditMode_ && IsPrefabEditObject(object)) {
        prefabEditDirty_ = true;
        return;
    }
    MarkDirtyForCategory(object->GetSaveCategory());
}

void DebugEditor::MarkDirtyForCategory(const std::string& category) {
    // 保存先 JSON の区分に合わせて未保存フラグを立てる。
    if (category == "Player") dirtyPlayer_ = true;
    else if (category == "Enemy") dirtyEnemy_ = true;
    else if (category == "Camera") dirtyCamera_ = true;
    else dirtyObject_ = true;
}

void DebugEditor::MarkDirty(SaveMode mode) {
    // All 指定は各カテゴリをまとめて未保存へし、個別指定は対象だけ変える。
    switch (mode) {
    case SaveMode::Player:
        dirtyPlayer_ = true;
        break;
    case SaveMode::Enemy:
        dirtyEnemy_ = true;
        break;
    case SaveMode::Object:
        dirtyObject_ = true;
        break;
    case SaveMode::Camera:
        dirtyCamera_ = true;
        break;
    case SaveMode::All:
    default:
        dirtyPlayer_ = true;
        dirtyEnemy_ = true;
        dirtyObject_ = true;
        dirtyCamera_ = true;
        break;
    }
}

void DebugEditor::ClearDirty(SaveMode mode) {
    // 保存済みになったカテゴリだけフラグを下ろす。
    switch (mode) {
    case SaveMode::Player:
        dirtyPlayer_ = false;
        break;
    case SaveMode::Enemy:
        dirtyEnemy_ = false;
        break;
    case SaveMode::Object:
        dirtyObject_ = false;
        break;
    case SaveMode::Camera:
        dirtyCamera_ = false;
        break;
    case SaveMode::All:
    default:
        dirtyPlayer_ = false;
        dirtyEnemy_ = false;
        dirtyObject_ = false;
        dirtyCamera_ = false;
        break;
    }
}

bool DebugEditor::IsDirty(SaveMode mode) const {
    // All はいずれか一件でも未保存なら true とする。
    switch (mode) {
    case SaveMode::Player:
        return dirtyPlayer_;
    case SaveMode::Enemy:
        return dirtyEnemy_;
    case SaveMode::Object:
        return dirtyObject_;
    case SaveMode::Camera:
        return dirtyCamera_;
    case SaveMode::All:
    default:
        return HasAnyDirty();
    }
}

bool DebugEditor::HasAnyDirty() const {
    // 保存ダイアログと終了警告が共有する未保存判定。
    return dirtyPlayer_ || dirtyEnemy_ || dirtyObject_ || dirtyCamera_;
}

std::string DebugEditor::GetDirtySummaryText() const {
    // 未保存の JSON 区分だけ列挙し、画面上で保存漏れを判別できるようにする。
    if (!HasAnyDirty()) return "保存済み";

    std::string text = "未保存:";
    if (dirtyPlayer_) text += " Player";
    if (dirtyEnemy_) text += " Enemy";
    if (dirtyObject_) text += " Object";
    if (dirtyCamera_) text += " Camera";
    return text;
}

// 複製 (スマート・コピペ版)
void DebugEditor::DuplicateSelected() {
    PruneInvalidSelectedObjects();
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    if (selectedObjects_.size() > 1) {
        std::vector<engine::Object3d*> sourceObjects = selectedObjects_;
        std::vector<engine::Object3d*> createdObjects;
        createdObjects.reserve(sourceObjects.size());
        std::unordered_map<engine::Object3d*, engine::Object3d*> duplicateMap;

        /// <summary>複製元、保存状態、生成中の複製Objectを確定前にまとめて保持する。</summary>
        struct PreparedDuplicate {
            engine::Object3d* source;
            nlohmann::json state;
            std::unique_ptr<engine::Object3d> object;
        };
        std::vector<PreparedDuplicate> prepared;
        std::unordered_set<std::string> reservedNames;
        Object3dCommon* common = sceneManager_->GetCurrentScene()->GetObject3dCommon();
        if (!common) return;

        const engine::Vector3 duplicateOffset = { 1.5f, 0.0f, 1.5f };
        for (engine::Object3d* source : sourceObjects) {
            if (!source || !IsObjectInCurrentScene(source)) continue;

            nlohmann::json duplicatedState = CaptureObjectState(source);
            duplicatedState.erase("guid");
            std::string baseName = source->GetName().empty() ? "Object" : source->GetName();
            std::string newName = baseName + "_copy";
            int suffix = 1;
            while (FindObjectByName(newName) || reservedNames.find(newName) != reservedNames.end()) {
                newName = baseName + "_copy" + std::to_string(suffix++);
            }
            reservedNames.insert(newName);
            duplicatedState["name"] = newName;

            auto object = CreateEditorObjectFromState(common, duplicatedState);
            if (!object) {
                DebugConsole::GetInstance()->AddLog("Duplicate cancelled: " + source->GetName() +
                                                    " has no matching runtime factory.");
                return;
            }
            prepared.push_back({source, std::move(duplicatedState), std::move(object)});
        }

        for (auto& candidate : prepared) {
            ApplyObjectState(candidate.object.get(), candidate.state);
            if (FindObjectByPersistentGuid(candidate.object->GetPersistentGuid())) {
                candidate.object->RegeneratePersistentGuid();
            }
            engine::Object3d* created = candidate.object.get();
            if (auto* enemy = dynamic_cast<BaseEnemy*>(created)) {
                enemy->BindSceneSpawnContext(sceneManager_->GetCurrentScene());
            }
            sceneManager_->GetCurrentScene()->AddObjectImmediate(std::move(candidate.object));

            const Transform* sourceTransform = candidate.source->GetTransform();
            created->SetTranslate(sourceTransform->translate + duplicateOffset);
            created->UpdateLocalMatrix();
            created->UpdateWorldMatrix();

            duplicateMap[candidate.source] = created;
            createdObjects.push_back(created);
        }

        // 選択範囲内の親子関係は、複製元ではなく複製先同士へ張り直します。
        for (const auto& [source, created] : duplicateMap) {
            engine::Object3d* sourceParent = source ? source->GetParent() : nullptr;
            const auto parent = duplicateMap.find(sourceParent);
            if (parent != duplicateMap.end()) {
                created->SetParent(parent->second, true);
                created->UpdateWorldMatrix();
            }
        }

        for (engine::Object3d* created : createdObjects) {
            nlohmann::json afterState = CaptureObjectState(created);
            EditorCommand command;
            command.type = EditorCommandType::ObjectCreated;
            command.label = "Duplicate Selected Objects";
            command.afterState = afterState;
            command.afterName = afterState.value("name", created->GetName());
            RegisterCommand(command);
            MarkDirtyForObject(created);
        }

        ClearObjectSelection();
        for (engine::Object3d* created : createdObjects) {
            AddSelectedObject(created);
        }
        PresetManager::GetInstance()->AssignNewPrefabInstanceId(createdObjects);
        if (selectedObject_) {
            EditorManager::GetInstance()->SetSelectedObject(this);
        }

        DebugConsole::GetInstance()->AddLog("Duplicated Objects: " + std::to_string(createdObjects.size()));
        return;
    }

    // 保存状態から同じ実行型を再生成する。未対応型は別型に変えず複製を中止する。
    nlohmann::json duplicateState = CaptureObjectState(selectedObject_);
    duplicateState.erase("guid");
    std::unique_ptr<engine::Object3d> newObj = CreateEditorObjectFromState(
        sceneManager_->GetCurrentScene()->GetObject3dCommon(), duplicateState);
    if (!newObj) {
        DebugConsole::GetInstance()->AddLog("Duplicate cancelled: " + selectedObject_->GetName() +
                                            " has no matching runtime factory.");
        return;
    }
    ApplyObjectState(newObj.get(), duplicateState);
    if (newObj && newObj->IsPrefabInstance()) {
        PresetManager::GetInstance()->AssignNewPrefabInstanceId({ newObj.get() });
    }

    // 2. 名前変更
    static int duplicateCount = 0;
    newObj->SetName(selectedObject_->GetName() + "_Copy" + std::to_string(duplicateCount++));

    // =========================================================
    //  マウスカーソルの位置(レイキャスト)を計算してペースト！
    // =========================================================
    engine::Math math;
    engine::Ray ray = ScreenPointToRay(gameViewMousePos_);
    engine::Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    // A. まず、他のオブジェクトの表面にマウスポインタが乗っているか判定
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    engine::RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        // 自分自身、カーソル、非表示のものは無視
        if (obj.get() == selectedObject_ || obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsRenderVisible()) continue;

        engine::Matrix4x4 wm = obj->GetWorldMatrix();
        engine::Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        engine::Vector3 ws = obj->GetTransform()->scale;
        engine::RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    if (best.isHit) {
        // オブジェクトに当たった場合、その表面に置く（めり込まないように高さを足す）
        finalPos = best.point;
        finalPos.y += newObj->GetTransform()->scale.y;
        found = true;
    }
    else {
        // 1. コピー元（選択中）のワールド座標を取得
        engine::Matrix4x4 sourceWm = selectedObject_->GetWorldMatrix();
        engine::Vector3 sourcePos = { sourceWm.m[3][0], sourceWm.m[3][1], sourceWm.m[3][2] };

        // 2. カメラ位置(ray.origin)からコピー元までの距離を計算
        float diffX = sourcePos.x - ray.origin.x;
        float diffY = sourcePos.y - ray.origin.y;
        float diffZ = sourcePos.z - ray.origin.z;
        float distToRef = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

        // 3. マウスクリックしたレイの方向に、その距離だけ進んだ場所を新しい点とする
        engine::Vector3 rayDir = math.Normalize(ray.diff);
        finalPos = { ray.origin.x + rayDir.x * distToRef,
                     ray.origin.y + rayDir.y * distToRef,
                     ray.origin.z + rayDir.z * distToRef };
        found = true;
    }


    // C. 座標の最終決定
    if (found) {
        // グリッドスナップがONなら、その位置でスナップさせる
        if (isGridSnapEnabled_) {
            finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
            finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
        }
        newObj->SetTranslate(finalPos);
        DebugConsole::GetInstance()->AddLog("Smart Pasted at Mouse Cursor!");
    }
    else {
        // カーソルが空を向いていた等でレイが当たらなかった場合の救済措置（今まで通り横にずらす）
        newObj->SetTranslateX(newObj->GetTranslate().x + 2.0f);
        DebugConsole::GetInstance()->AddLog("Pasted at offset (Ray missed).");
    }
    // =========================================================

    // 行列更新
    newObj->UpdateWorldMatrix();

    if (auto* enemy = dynamic_cast<BaseEnemy*>(newObj.get())) {
        enemy->BindSceneSpawnContext(sceneManager_->GetCurrentScene());
    }
    AddEditorObject(std::move(newObj), "Duplicate Object");
}

// 削除
void DebugEditor::DeleteSelected() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    PruneInvalidSelectedObjects();
    if (selectedObjects_.empty()) return;

    if (prefabEditMode_ && std::find(selectedObjects_.begin(), selectedObjects_.end(), prefabEditRoot_) != selectedObjects_.end()) {
        DebugConsole::GetInstance()->AddLog("Prefab Mode: Root Objectは削除できません。");
        return;
    }

    if (prefabEditMode_) {
        auto& sceneObjects = sceneManager_->GetCurrentScene()->GetObjects();
        std::unordered_set<engine::Object3d*> selectedSet(selectedObjects_.begin(), selectedObjects_.end());
        std::vector<engine::Object3d*> deleteRoots;
        for (engine::Object3d* selected : selectedObjects_) {
            bool hasSelectedAncestor = false;
            for (engine::Object3d* parent = selected ? selected->GetParent() : nullptr; parent; parent = parent->GetParent()) {
                if (selectedSet.find(parent) != selectedSet.end()) {
                    hasSelectedAncestor = true;
                    break;
                }
            }
            if (selected && !hasSelectedAncestor) {
                deleteRoots.push_back(selected);
            }
        }

        std::vector<engine::Object3d*> deleteObjects;
        std::function<void(engine::Object3d*)> collectChildren = [&](engine::Object3d* parent) {
            if (!parent) return;
            deleteObjects.push_back(parent);
            for (const auto& candidate : sceneObjects) {
                if (candidate && candidate->GetParent() == parent) {
                    collectChildren(candidate.get());
                }
            }
        };
        for (engine::Object3d* root : deleteRoots) {
            collectChildren(root);
        }

        std::vector<nlohmann::json> deletedStates;
        deletedStates.reserve(deleteObjects.size());
        Object3dCommon* common = sceneManager_->GetCurrentScene()->GetObject3dCommon();
        for (engine::Object3d* object : deleteObjects) {
            nlohmann::json state = CaptureObjectState(object);
            if (!CreateEditorObjectFromState(common, state)) {
                DebugConsole::GetInstance()->AddLog("Delete cancelled: " + object->GetName() +
                                                    " cannot be restored as the same runtime type.");
                return;
            }
            deletedStates.push_back(std::move(state));
        }
        for (auto it = deleteObjects.rbegin(); it != deleteObjects.rend(); ++it) {
            RemoveObjectImmediate(*it);
        }

        EditorTransaction transaction;
        transaction.label = "Delete Prefab Child";
        transaction.undo = [this, deletedStates]() {
            engine::Object3d* firstRestored = nullptr;
            for (const auto& state : deletedStates) {
                engine::Object3d* restored = AddObjectFromState(state);
                if (!firstRestored) firstRestored = restored;
            }
            if (firstRestored) {
                SetSelectedObject(firstRestored);
                EditorManager::GetInstance()->SetSelectedObject(this);
            }
            prefabEditDirty_ = true;
        };
        transaction.redo = [this, deletedStates]() {
            for (auto it = deletedStates.rbegin(); it != deletedStates.rend(); ++it) {
                const std::string name = it->value("name", "");
                if (engine::Object3d* object = FindObjectByName(name)) {
                    RemoveObjectImmediate(object);
                }
            }
            ClearObjectSelection();
            prefabEditDirty_ = true;
        };
        EditorTransactionManager::GetInstance()->Register(std::move(transaction));
        prefabEditDirty_ = true;
        ClearObjectSelection();
        EditorManager::GetInstance()->ClearSelection();
        DebugConsole::GetInstance()->AddLog("Prefab Childを削除: " + std::to_string(deleteObjects.size()));
        return;
    }

    std::vector<engine::Object3d*> targets = selectedObjects_;
    Object3dCommon* common = sceneManager_->GetCurrentScene()->GetObject3dCommon();
    for (engine::Object3d* target : targets) {
        if (!target || !IsObjectInCurrentScene(target)) continue;
        if (!CreateEditorObjectFromState(common, CaptureObjectState(target))) {
            DebugConsole::GetInstance()->AddLog("Delete cancelled: " + target->GetName() +
                                                " cannot be restored as the same runtime type.");
            return;
        }
    }
    const bool isGroupDelete = targets.size() > 1;
    int deletedCount = 0;
    std::string deletedName;

    for (engine::Object3d* target : targets) {
        if (!target || !IsObjectInCurrentScene(target)) continue;

        nlohmann::json beforeState = CaptureObjectState(target);
        deletedName = target->GetName();
        MarkDirtyForObject(target);

        EditorCommand command;
        command.type = EditorCommandType::ObjectDeleted;
        command.label = isGroupDelete ? "Delete Selected Objects" : "Delete Object";
        command.beforeState = beforeState;
        command.beforeName = beforeState.value("name", deletedName);
        RegisterCommand(command);

        sceneManager_->GetCurrentScene()->DestroyObject(target);
        ++deletedCount;
    }

    // 重要：削除したポインタを持ち続けないようにする
    ClearObjectSelection();
    EditorManager::GetInstance()->ClearSelection();

    if (isGroupDelete) {
        DebugConsole::GetInstance()->AddLog("Deleted Objects: " + std::to_string(deletedCount));
    } else {
        DebugConsole::GetInstance()->AddLog("Deleted Object: " + deletedName);
    }
}

void DebugEditor::ApplyEditorCommand(const EditorCommand& command, bool undo) {
    // 作成・削除・属性変更で Undo と Redo の方向を反転して同じ適用経路を使う。
    switch (command.type) {
    case EditorCommandType::ObjectCreated: {
        if (undo) {
            engine::Object3d* object = FindObjectByPersistentGuid(
                command.afterState.value("guid", std::string()));
            if (!object) object = FindObjectByName(command.afterName);
            if (object) {
                MarkDirtyForObject(object);
                RemoveObjectImmediate(object);
                ClearObjectSelection();
                EditorManager::GetInstance()->ClearSelection();
            }
        } else {
            engine::Object3d* created = AddObjectFromState(command.afterState);
            if (created) {
                SetSelectedObject(created);
                EditorManager::GetInstance()->SetSelectedObject(this);
                MarkDirtyForObject(created);
            }
        }
        break;
    }
    case EditorCommandType::ObjectDeleted: {
        if (undo) {
            engine::Object3d* restored = AddObjectFromState(command.beforeState);
            if (restored) {
                SetSelectedObject(restored);
                EditorManager::GetInstance()->SetSelectedObject(this);
                MarkDirtyForObject(restored);
            }
        } else {
            engine::Object3d* object = FindObjectByPersistentGuid(
                command.beforeState.value("guid", std::string()));
            if (!object) object = FindObjectByName(command.beforeName);
            if (object) {
                MarkDirtyForObject(object);
                RemoveObjectImmediate(object);
                ClearObjectSelection();
                EditorManager::GetInstance()->ClearSelection();
            }
        }
        break;
    }
    case EditorCommandType::ObjectEdited:
    default: {
        const std::string& primaryName = undo ? command.afterName : command.beforeName;
        const std::string& fallbackName = undo ? command.beforeName : command.afterName;
        const nlohmann::json& targetState = undo ? command.beforeState : command.afterState;
        engine::Object3d* object = FindObjectByPersistentGuid(targetState.value("guid", std::string()));
        if (!object) object = FindObjectByName(primaryName);
        if (!object) object = FindObjectByName(fallbackName);
        if (object) {
            ApplyObjectState(object, undo ? command.beforeState : command.afterState);
            SetSelectedObject(object);
            EditorManager::GetInstance()->SetSelectedObject(this);
            MarkDirtyForObject(object);
        }
        break;
    }
    }
}

// ==========================================
//  Undo処理
// ==========================================
void DebugEditor::PerformUndo() {
    // Transaction 側で適用に成功した場合だけ操作名をログへ残す。
    EditorTransactionManager* transactions = EditorTransactionManager::GetInstance();
    const std::string label = transactions->GetUndoLabel();
    if (transactions->Undo()) {
        DebugConsole::GetInstance()->AddLog("Undo: " + label);
    }
}

// ==========================================
//  Redo処理
// ==========================================
void DebugEditor::PerformRedo() {
    // やり直した操作名を履歴マネージャーから取り、成功時に表示する。
    EditorTransactionManager* transactions = EditorTransactionManager::GetInstance();
    const std::string label = transactions->GetRedoLabel();
    if (transactions->Redo()) {
        DebugConsole::GetInstance()->AddLog("Redo: " + label);
    }
}

// マウス位置からワールド空間へのレイを作成
