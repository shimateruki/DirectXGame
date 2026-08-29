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
#include "CameraEditor.h"
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
#include <PresetManager.h>
#include <PresetEditor.h>
#include <MeshEffectManager.h>
#include "BuiltInCreatePresetRegistry.h"
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <utility>
namespace fs = std::filesystem;
const float PI = (float)M_PI;

namespace {
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

bool ShouldApplyGroundDefaults(Object3d* object) {
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

void ApplyGroundDefaultsForGeneratedModel(Object3d* object) {
    if (!ShouldApplyGroundDefaults(object)) {
        return;
    }

    auto colliderConfig = object->GetColliderConfig();
    if (colliderConfig.type == ColliderType::kNone) {
        colliderConfig.type = ColliderType::kAABB;
        object->SetColliderConfig(colliderConfig);
    }
    if (object->GetCollisionAttribute() == 0) {
        object->SetCollisionAttribute(kGround);
    }
    if (object->GetCollisionMask() == 0) {
        object->SetCollisionMask(0xFFFFFFFFu);
    }
}
}

static float ToRadians(float degrees) { return degrees * (PI / 180.0f); }
static float ToDegrees(float radians) { return radians * (180.0f / PI); }

namespace {
    constexpr float kCrashRecoveryAutosaveInterval = 3.0f;
    constexpr float kCrashRecoveryHeartbeatInterval = 5.0f;
    constexpr const char* kCrashRecoveryRoot = "Resources/.backup/crash_recovery";
    constexpr const char* kCrashRecoverySessionFile = "session.json";

    std::string ToGenericPath(const fs::path& path) {
        return path.generic_string();
    }

    std::string NowIsoString() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &time);
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return out.str();
    }

    std::string MakeCrashRecoveryStamp() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm tm{};
        localtime_s(&tm, &time);
        std::ostringstream out;
        out << std::put_time(&tm, "%Y%m%d_%H%M%S") << "_" << std::setw(3) << std::setfill('0') << millis;
        return out.str();
    }

    bool ReadJsonFileSafe(const fs::path& path, nlohmann::json& outJson) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        try {
            file >> outJson;
            return outJson.is_object();
        }
        catch (...) {
            return false;
        }
    }

    bool WriteJsonFileSafe(const fs::path& path, const nlohmann::json& data) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }

        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        file << data.dump(4);
        return file.good();
    }

    bool IsPathInside(const fs::path& child, const fs::path& parent) {
        std::error_code ec;
        const fs::path childAbs = fs::absolute(child, ec).lexically_normal();
        if (ec) {
            return false;
        }
        const fs::path parentAbs = fs::absolute(parent, ec).lexically_normal();
        if (ec) {
            return false;
        }

        std::wstring childText = childAbs.native();
        std::wstring parentText = parentAbs.native();
        std::transform(childText.begin(), childText.end(), childText.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        std::transform(parentText.begin(), parentText.end(), parentText.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (!parentText.empty() && parentText.back() != L'\\' && parentText.back() != L'/') {
            parentText.push_back(fs::path::preferred_separator);
        }
        return childText.rfind(parentText, 0) == 0;
    }

    bool IsCrashRecoverySourcePathAllowed(const fs::path& path) {
        const std::string generic = path.lexically_normal().generic_string();
        const std::string lower = [&generic]() {
            std::string value = generic;
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }();
        return lower.rfind("resources/json/", 0) == 0 &&
            lower.find("/../") == std::string::npos &&
            lower.find("resources/.backup/") != 0 &&
            lower.find("resources/.cache/") != 0;
    }

    float GetCreateYOffsetForObject(const Object3d* object) {
        if (!object) return 1.0f;

        const auto& collider = object->GetColliderConfig();
        if (collider.size.y > 0.0f) return collider.size.y;
        if (collider.size.x > 0.0f) return collider.size.x;

        const Transform& transform = object->GetTransform();
        if (transform.scale.y > 0.0f) return transform.scale.y;
        return 1.0f;
    }

    Vector3 GetPlacementExtents(const Object3d* object) {
        if (!object) return { 1.0f, 1.0f, 1.0f };

        const auto& collider = object->GetColliderConfig();
        const Transform& transform = object->GetTransform();
        return {
            collider.size.x > 0.0f ? collider.size.x : std::abs(transform.scale.x),
            collider.size.y > 0.0f ? collider.size.y : std::abs(transform.scale.y),
            collider.size.z > 0.0f ? collider.size.z : std::abs(transform.scale.z)
        };
    }

    bool IsNearlyZero(const Vector3& value) {
        return std::abs(value.x) < 0.0001f &&
            std::abs(value.y) < 0.0001f &&
            std::abs(value.z) < 0.0001f;
    }

    Vector3 NormalizeOrUp(const Vector3& value) {
        if (IsNearlyZero(value)) {
            return { 0.0f, 1.0f, 0.0f };
        }
        return Math::Normalize(value);
    }

    void ApplyEditorPreviewLightOverride(Object3d* object) {
        if (!object) {
            return;
        }

        if (auto* material = object->GetMaterialData()) {
            material->enableLighting = 0;
            material->selectedLighting = 0;
            material->emissive = (std::max)(material->emissive, 1.0f);
        }
    }

    float GetSurfaceOffset(const Object3d* object, const Vector3& normal) {
        Vector3 extents = GetPlacementExtents(object);
        Vector3 absNormal = { std::abs(normal.x), std::abs(normal.y), std::abs(normal.z) };
        float extentOnNormal = extents.x * absNormal.x + extents.y * absNormal.y + extents.z * absNormal.z;
        if (!object) return extentOnNormal;

        const auto& collider = object->GetColliderConfig();
        float centerOnNormal =
            collider.center.x * normal.x +
            collider.center.y * normal.y +
            collider.center.z * normal.z;
        return std::max(extentOnNormal - centerOnNormal, 0.0f);
    }

    Vector3 GetSurfaceAlignedRotation(const Vector3& currentRotation, const Vector3& normal) {
        constexpr float kHalfPi = PI * 0.5f;
        Vector3 n = NormalizeOrUp(normal);

        if (std::abs(n.y) >= std::abs(n.x) && std::abs(n.y) >= std::abs(n.z)) {
            if (n.y < 0.0f) {
                return { PI, currentRotation.y, 0.0f };
            }
            return { 0.0f, currentRotation.y, 0.0f };
        }

        if (std::abs(n.x) >= std::abs(n.z)) {
            return { 0.0f, currentRotation.y, n.x > 0.0f ? -kHalfPi : kHalfPi };
        }

        return { n.z > 0.0f ? kHalfPi : -kHalfPi, currentRotation.y, 0.0f };
    }

    void ApplyGridSnap(Vector3& position, const Vector3& normal, bool hasSurface, bool enabled, float snapValue) {
        if (!enabled || snapValue <= 0.0f) return;

        if (!hasSurface || std::abs(normal.x) < 0.5f) {
            position.x = std::round(position.x / snapValue) * snapValue;
        }
        if (hasSurface && std::abs(normal.y) < 0.5f) {
            position.y = std::round(position.y / snapValue) * snapValue;
        }
        if (!hasSurface || std::abs(normal.z) < 0.5f) {
            position.z = std::round(position.z / snapValue) * snapValue;
        }
    }

    bool ShouldUsePreviewDrawClassOverride(const Object3d* object) {
        if (!object) return false;

        const std::string className = object->GetClassName();
        return object->IsCameraObject() ||
            className == "GPUParticle" ||
            className == "InvisibleBox";
    }

    bool IsValidAabb(const AABB& aabb) {
        return aabb.max.x > aabb.min.x &&
            aabb.max.y > aabb.min.y &&
            aabb.max.z > aabb.min.z;
    }

    Vector3 GetAabbCenter(const AABB& aabb) {
        return {
            (aabb.min.x + aabb.max.x) * 0.5f,
            (aabb.min.y + aabb.max.y) * 0.5f,
            (aabb.min.z + aabb.max.z) * 0.5f
        };
    }

    float Distance(const Vector3& a, const Vector3& b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    std::string GetLeafName(const std::string& path) {
        size_t slash = path.find_last_of("/\\");
        if (slash == std::string::npos) {
            return path.empty() ? "Preset" : path;
        }
        std::string leaf = path.substr(slash + 1);
        return leaf.empty() ? "Preset" : leaf;
    }

    bool SceneHasObjectName(BaseScene* scene, const std::string& name) {
        if (!scene) return false;

        for (const auto& object : scene->GetObjects()) {
            if (object && object->GetName() == name) {
                return true;
            }
        }
        return false;
    }

    bool ReservedHasName(const std::vector<std::string>& reservedNames, const std::string& name) {
        return std::find(reservedNames.begin(), reservedNames.end(), name) != reservedNames.end();
    }

    std::string MakeUniquePresetObjectName(BaseScene* scene, const std::string& baseName, const std::vector<std::string>& reservedNames) {
        std::string base = baseName.empty() ? "PresetObject" : GetLeafName(baseName);
        if (!SceneHasObjectName(scene, base) && !ReservedHasName(reservedNames, base)) {
            return base;
        }

        for (int index = 1; index < 10000; ++index) {
            std::string candidate = base + "_" + std::to_string(index);
            if (!SceneHasObjectName(scene, candidate) && !ReservedHasName(reservedNames, candidate)) {
                return candidate;
            }
        }
        return base + "_New";
    }

    void AssignPresetInstanceNames(BaseScene* scene, const std::string& presetName, std::vector<std::unique_ptr<Object3d>>& objects) {
        if (objects.empty()) return;

        std::vector<std::string> reservedNames;
        std::string rootName = MakeUniquePresetObjectName(scene, GetLeafName(presetName), reservedNames);
        objects.front()->SetName(rootName);
        reservedNames.push_back(rootName);

        for (size_t index = 1; index < objects.size(); ++index) {
            if (!objects[index]) continue;

            std::string baseName = objects[index]->GetName();
            if (baseName.empty()) {
                baseName = rootName + "_Child";
            }
            std::string uniqueName = MakeUniquePresetObjectName(scene, baseName, reservedNames);
            objects[index]->SetName(uniqueName);
            reservedNames.push_back(uniqueName);
        }
    }

}

// ========================================================================
// 初期化
// ========================================================================

nlohmann::json DebugEditor::CaptureObjectState(Object3d* object) const {
    nlohmann::json state = nlohmann::json::object();
    if (!object) return state;
    if (object == selectedObject_ && !IsObjectInCurrentScene(object)) return state;

    state = object->ExportToJson();
    state["name"] = object->GetName();
    state["parentName"] = object->GetParent() ? object->GetParent()->GetName() : "";
    state["parentGuid"] = object->GetParent() ? object->GetParent()->EnsurePersistentGuid() : "";
    state["isStatic"] = object->IsStatic();
    return state;
}

std::vector<DebugEditor::ObjectStateSnapshot> DebugEditor::CaptureObjectStates(const std::vector<Object3d*>& objects) const {
    std::vector<ObjectStateSnapshot> snapshots;
    snapshots.reserve(objects.size());
    for (Object3d* object : objects) {
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

void DebugEditor::ApplyObjectState(Object3d* object, const nlohmann::json& state) {
    if (!object || !state.is_object()) return;

    if (state.contains("name")) {
        object->SetName(state["name"].get<std::string>());
    }
    object->ImportFromJson(state);
    if (state.contains("isStatic")) {
        object->SetStatic(state["isStatic"].get<bool>());
    }

    Object3d* parent = nullptr;
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

Object3d* DebugEditor::FindObjectByName(const std::string& name) const {
    if (name.empty() || !sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (const auto& object : objects) {
        if (object && object->GetName() == name) {
            return object.get();
        }
    }
    return nullptr;
}

Object3d* DebugEditor::FindObjectByPersistentGuid(const std::string& guid) const {
    if (guid.empty() || !sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;
    return sceneManager_->GetCurrentScene()->FindObjectByPersistentGuid(guid);
}

Object3d* DebugEditor::AddObjectFromState(const nlohmann::json& state) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene() || !state.is_object()) return nullptr;

    Object3dCommon* common = sceneManager_->GetCurrentScene()->GetObject3dCommon();
    if (!common) return nullptr;

    auto object = std::make_unique<Object3d>();
    object->Initialize(common);
    ApplyObjectState(object.get(), state);
    if (FindObjectByPersistentGuid(object->GetPersistentGuid())) {
        object->RegeneratePersistentGuid();
    }

    Object3d* raw = object.get();
    CollisionManager::GetInstance()->AddObject(raw);
    sceneManager_->GetCurrentScene()->GetObjects().push_back(std::move(object));
    return raw;
}

std::unique_ptr<Object3d> DebugEditor::RemoveObjectImmediate(Object3d* object) {
    if (!object || !sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    auto it = std::find_if(objects.begin(), objects.end(), [object](const std::unique_ptr<Object3d>& candidate) {
        return candidate.get() == object;
    });
    if (it == objects.end()) return nullptr;

    CollisionManager::GetInstance()->RemoveObject(object);
    std::unique_ptr<Object3d> removed = std::move(*it);
    objects.erase(it);
    return removed;
}

void DebugEditor::RegisterCommand(const EditorCommand& command) {
    EditorTransaction transaction;
    transaction.label = command.label;
    transaction.undo = [this, command]() { ApplyEditorCommand(command, true); };
    transaction.redo = [this, command]() { ApplyEditorCommand(command, false); };
    EditorTransactionManager::GetInstance()->Register(std::move(transaction));
}

void DebugEditor::RegisterObjectEdited(Object3d* object, const std::string& label) {
    if (!object) return;
    if (object == selectedObject_ && !IsObjectInCurrentScene(object)) return;
    RegisterObjectEdited(object, CaptureObjectState(object), label);
}

void DebugEditor::RegisterObjectEdited(Object3d* object, const nlohmann::json& beforeState, const std::string& label) {
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
        std::vector<Object3d*> appliedObjects;
        appliedObjects.reserve(changes.size());
        for (const StateChange& change : changes) {
            const std::string& primaryName = undo ? change.afterName : change.beforeName;
            const std::string& fallbackName = undo ? change.beforeName : change.afterName;
            Object3d* object = FindObjectByPersistentGuid(change.guid);
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
            for (Object3d* object : appliedObjects) {
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

void DebugEditor::AddEditorObject(std::unique_ptr<Object3d> object, const std::string& label) {
    if (!object || !sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    Object3d* raw = object.get();
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

    CollisionManager::GetInstance()->AddObject(raw);
    sceneManager_->GetCurrentScene()->GetObjects().push_back(std::move(object));
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

void DebugEditor::AddEditorObjects(std::vector<std::unique_ptr<Object3d>> objects, const std::string& label) {
    if (objects.empty()) return;

    Object3d* root = objects.front().get();
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

void DebugEditor::MarkDirtyForObject(Object3d* object) {
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
    if (category == "Player") dirtyPlayer_ = true;
    else if (category == "Enemy") dirtyEnemy_ = true;
    else if (category == "Camera") dirtyCamera_ = true;
    else dirtyObject_ = true;
}

void DebugEditor::MarkDirty(SaveMode mode) {
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
    return dirtyPlayer_ || dirtyEnemy_ || dirtyObject_ || dirtyCamera_;
}

std::string DebugEditor::GetDirtySummaryText() const {
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
        std::vector<Object3d*> sourceObjects = selectedObjects_;
        std::vector<Object3d*> createdObjects;
        createdObjects.reserve(sourceObjects.size());
        std::unordered_map<Object3d*, Object3d*> duplicateMap;

        const Vector3 duplicateOffset = { 1.5f, 0.0f, 1.5f };
        for (Object3d* source : sourceObjects) {
            if (!source || !IsObjectInCurrentScene(source)) continue;

            nlohmann::json duplicatedState = CaptureObjectState(source);
            duplicatedState.erase("guid");
            std::string baseName = source->GetName().empty() ? "Object" : source->GetName();
            std::string newName = baseName + "_copy";
            int suffix = 1;
            while (FindObjectByName(newName)) {
                newName = baseName + "_copy" + std::to_string(suffix++);
            }
            duplicatedState["name"] = newName;

            Object3d* created = AddObjectFromState(duplicatedState);
            if (!created) continue;

            Transform* createdTransform = created->GetTransform();
            Transform* sourceTransform = source->GetTransform();
            createdTransform->translate = sourceTransform->translate + duplicateOffset;
            created->UpdateLocalMatrix();
            created->UpdateWorldMatrix();

            duplicateMap[source] = created;
            createdObjects.push_back(created);
        }

        // 選択範囲内の親子関係は、複製元ではなく複製先同士へ張り直します。
        for (const auto& [source, created] : duplicateMap) {
            Object3d* sourceParent = source ? source->GetParent() : nullptr;
            const auto parent = duplicateMap.find(sourceParent);
            if (parent != duplicateMap.end()) {
                created->SetParent(parent->second, true);
                created->UpdateWorldMatrix();
            }
        }

        for (Object3d* created : createdObjects) {
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
        for (Object3d* created : createdObjects) {
            AddSelectedObject(created);
        }
        PresetManager::GetInstance()->AssignNewPrefabInstanceId(createdObjects);
        if (selectedObject_) {
            EditorManager::GetInstance()->SetSelectedObject(this);
        }

        DebugConsole::GetInstance()->AddLog("Duplicated Objects: " + std::to_string(createdObjects.size()));
        return;
    }

    // 1. 完全なクローンを作成
    std::unique_ptr<Object3d> newObj = selectedObject_->Clone();
    if (newObj && newObj->IsPrefabInstance()) {
        PresetManager::GetInstance()->AssignNewPrefabInstanceId({ newObj.get() });
    }

    // 2. 名前変更
    static int duplicateCount = 0;
    newObj->SetName(selectedObject_->GetName() + "_Copy" + std::to_string(duplicateCount++));

    // =========================================================
    // Cursor位置からRaycastした配置点へClipboard内容を貼り付けます。
    // =========================================================
    Math math;
    Ray ray = ScreenPointToRay(gameViewMousePos_);
    Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    // A. まず、他のオブジェクトの表面にマウスポインタが乗っているか判定
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        // 自分自身、カーソル、非表示のものは無視
        if (obj.get() == selectedObject_ || obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsRenderVisible()) continue;

        Matrix4x4 wm = obj->GetWorldMatrix();
        Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        Vector3 ws = obj->GetTransform()->scale;
        RayResult tmp;
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
        Matrix4x4 sourceWm = selectedObject_->GetWorldMatrix();
        Vector3 sourcePos = { sourceWm.m[3][0], sourceWm.m[3][1], sourceWm.m[3][2] };

        // 2. カメラ位置(ray.origin)からコピー元までの距離を計算
        float diffX = sourcePos.x - ray.origin.x;
        float diffY = sourcePos.y - ray.origin.y;
        float diffZ = sourcePos.z - ray.origin.z;
        float distToRef = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);

        // 3. マウスクリックしたレイの方向に、その距離だけ進んだ場所を新しい点とする
        Vector3 rayDir = math.Normalize(ray.diff);
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
        newObj->GetTransform()->translate = finalPos;
        DebugConsole::GetInstance()->AddLog("Smart Pasted at Mouse Cursor!");
    }
    else {
        // カーソルが空を向いていた等でレイが当たらなかった場合の救済措置（今まで通り横にずらす）
        newObj->GetTransform()->translate.x += 2.0f;
        DebugConsole::GetInstance()->AddLog("Pasted at offset (Ray missed).");
    }
    // =========================================================

    // 行列更新
    newObj->UpdateWorldMatrix();

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
        std::unordered_set<Object3d*> selectedSet(selectedObjects_.begin(), selectedObjects_.end());
        std::vector<Object3d*> deleteRoots;
        for (Object3d* selected : selectedObjects_) {
            bool hasSelectedAncestor = false;
            for (Object3d* parent = selected ? selected->GetParent() : nullptr; parent; parent = parent->GetParent()) {
                if (selectedSet.find(parent) != selectedSet.end()) {
                    hasSelectedAncestor = true;
                    break;
                }
            }
            if (selected && !hasSelectedAncestor) {
                deleteRoots.push_back(selected);
            }
        }

        std::vector<Object3d*> deleteObjects;
        std::function<void(Object3d*)> collectChildren = [&](Object3d* parent) {
            if (!parent) return;
            deleteObjects.push_back(parent);
            for (const auto& candidate : sceneObjects) {
                if (candidate && candidate->GetParent() == parent) {
                    collectChildren(candidate.get());
                }
            }
        };
        for (Object3d* root : deleteRoots) {
            collectChildren(root);
        }

        std::vector<nlohmann::json> deletedStates;
        deletedStates.reserve(deleteObjects.size());
        for (Object3d* object : deleteObjects) {
            deletedStates.push_back(CaptureObjectState(object));
        }
        for (auto it = deleteObjects.rbegin(); it != deleteObjects.rend(); ++it) {
            RemoveObjectImmediate(*it);
        }

        EditorTransaction transaction;
        transaction.label = "Delete Prefab Child";
        transaction.undo = [this, deletedStates]() {
            Object3d* firstRestored = nullptr;
            for (const auto& state : deletedStates) {
                Object3d* restored = AddObjectFromState(state);
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
                if (Object3d* object = FindObjectByName(name)) {
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

    std::vector<Object3d*> targets = selectedObjects_;
    const bool isGroupDelete = targets.size() > 1;
    int deletedCount = 0;
    std::string deletedName;

    for (Object3d* target : targets) {
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
    switch (command.type) {
    case EditorCommandType::ObjectCreated: {
        if (undo) {
            Object3d* object = FindObjectByPersistentGuid(
                command.afterState.value("guid", std::string()));
            if (!object) object = FindObjectByName(command.afterName);
            if (object) {
                MarkDirtyForObject(object);
                RemoveObjectImmediate(object);
                ClearObjectSelection();
                EditorManager::GetInstance()->ClearSelection();
            }
        } else {
            Object3d* created = AddObjectFromState(command.afterState);
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
            Object3d* restored = AddObjectFromState(command.beforeState);
            if (restored) {
                SetSelectedObject(restored);
                EditorManager::GetInstance()->SetSelectedObject(this);
                MarkDirtyForObject(restored);
            }
        } else {
            Object3d* object = FindObjectByPersistentGuid(
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
        Object3d* object = FindObjectByPersistentGuid(targetState.value("guid", std::string()));
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
    EditorTransactionManager* transactions = EditorTransactionManager::GetInstance();
    const std::string label = transactions->GetRedoLabel();
    if (transactions->Redo()) {
        DebugConsole::GetInstance()->AddLog("Redo: " + label);
    }
}



// マウス位置からワールド空間へのレイを作成
