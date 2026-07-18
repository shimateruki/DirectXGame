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
#include "GhostDirector.h"
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
#include <BulletManager.h>
#include <PresetManager.h>
#include <PresetEditor.h>
#include <MeshEffectManager.h>
#include "BuiltInCreatePresetRegistry.h"
#include <unordered_map>
#include <unordered_set>
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

float ToRadians(float degrees) { return degrees * (PI / 180.0f); }
float ToDegrees(float radians) { return radians * (180.0f / PI); }

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
void DebugEditor::Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon) {
    sceneManager_ = sceneManager;
    dxCommon_ = dxCommon;
    selectedObject_ = nullptr;
    selectedObjects_.clear();
    lastUpdatedScene_ = nullptr;
    EditorPropertyRegistry::GetInstance()->InitializeBuiltInProperties();
    EditorTransactionManager::GetInstance()->Clear();
    PresetManager::GetInstance()->Initialize();
    if (sceneManager_ && sceneManager_->GetCurrentScene()) {
        BuiltInCreatePresetRegistry::EnsureRegistered(sceneManager_->GetCurrentScene()->GetObject3dCommon());
    }
    PresetEditor::GetInstance()->Initialize();
    PresetEditor::GetInstance()->SetPlacePresetCallback([this](const std::string& presetName) {
        if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
        if (sceneManager_->IsPlaying() || previewObject_ || isPathEditMode_) return;

        BaseScene* currentScene = sceneManager_->GetCurrentScene();
        Object3dCommon* common = currentScene->GetObject3dCommon();
        if (!common) return;

        auto objects = PresetManager::GetInstance()->CreateObjectsFromPreset(presetName, common);
        if (objects.empty()) return;

        AssignPresetInstanceNames(currentScene, presetName, objects);

        gameViewCreateMenuMousePos_ = gameViewMousePos_;
        StartGameViewCreatePreview(std::move(objects), "Create Preset: " + presetName);
    });
    PresetEditor::GetInstance()->SetBrushPresetCallback([this](const std::string& presetName) {
        StartPresetBrush(presetName);
    });
    hierarchyWindow_.Initialize(this);
    projectWindow_.Initialize(this, dxCommon);
    PresetEditor::GetInstance()->SetThumbnailProvider([this](const std::string& presetName) {
        return projectWindow_.GetPresetThumbnailGpuPtr(presetName);
    });
    inspectorWindow_.Initialize(this);
    serializer_.Initialize(this);
    primitiveDrawer_.Initialize(dxCommon);
    sceneValidator_.Initialize(sceneManager);
    materialPreviewBoard_.Initialize(sceneManager, this);
    EffectPreviewStage::GetInstance()->Initialize(sceneManager, dxCommon);
    animationWorkbench_.Initialize(sceneManager, dxCommon);
    eventLinkGraph_.Initialize(sceneManager, this);
    nodeGraphEditorWindow_.Initialize(this);
    textSpriteGenerator_.Initialize(sceneManager, this);
    text3DGenerator_.Initialize(sceneManager, this);
    modelOptimizerWindow_.Initialize(this);
    assetAuditWindow_.Initialize(this);
    propertyMatrixWindow_.Initialize(this);
    statusTuningWindow_.Initialize(this, sceneManager_);
    gameDataDebugEditor_.Initialize(sceneManager);
    terrainEditorWindow_.Initialize(this);
    jsonBackupWindow_.Initialize(this);
    audioSettingsWindow_.Initialize(this);
    executablePackageWindow_.Initialize(this);
    captureToolWindow_.Initialize(this);
    InitializeCrashRecovery();

    const fs::path notificationLog = "Resources/.cache/dds_cache_notifications.jsonl";
    ddsCacheNotificationReadOffset_ = 0;
    if (fs::exists(notificationLog)) {
        std::error_code ec;
        ddsCacheNotificationReadOffset_ = fs::file_size(notificationLog, ec);
        if (ec) {
            ddsCacheNotificationReadOffset_ = 0;
        }
        else {
            const auto writeTime = fs::last_write_time(notificationLog, ec);
            if (!ec) {
                const auto now = fs::file_time_type::clock::now();
                const auto recentWindow = std::chrono::seconds(30);
                if (writeTime <= now && now - writeTime <= recentWindow) {
                    ddsCacheNotificationReadOffset_ = 0;
                }
            }
        }
    }
}

bool DebugEditor::BeginPrefabEditSession(const std::string& prefabName) {
    if (prefabName.empty() || !sceneManager_ || !sceneManager_->GetCurrentScene() ||
        sceneManager_->IsPlaying() || !PresetManager::GetInstance()->HasPrefab(prefabName)) {
        return false;
    }

    if (prefabEditMode_) {
        if (prefabEditName_ == prefabName) {
            return true;
        }
        CancelPrefabEditSession();
    }

    BaseScene* scene = sceneManager_->GetCurrentScene();
    Object3dCommon* common = scene->GetObject3dCommon();
    if (!common) {
        return false;
    }

    auto prefabObjects = PresetManager::GetInstance()->CreateObjectsFromPrefab(prefabName, common);
    if (prefabObjects.empty() || !prefabObjects.front()) {
        return false;
    }

    ClearObjectSelection();
    EditorManager::GetInstance()->ClearSelection();
    EditorTransactionManager::GetInstance()->Clear();
    prefabEditPreviousVisibility_.clear();
    for (auto& sceneObject : scene->GetObjects()) {
        if (!sceneObject) continue;
        prefabEditPreviousVisibility_[sceneObject.get()] = sceneObject->GetIsVisible();
        sceneObject->SetIsVisible(false);
    }

    prefabEditMode_ = true;
    prefabEditDirty_ = false;
    prefabEditName_ = prefabName;
    prefabEditScene_ = scene;
    prefabEditRoot_ = prefabObjects.front().get();

    auto& sceneObjects = scene->GetObjects();
    for (auto& object : prefabObjects) {
        if (!object) continue;
        CollisionManager::GetInstance()->AddObject(object.get());
        sceneObjects.push_back(std::move(object));
    }

    SetSelectedObject(prefabEditRoot_);
    EditorManager::GetInstance()->SetSelectedObject(this);
    CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
    DebugConsole::GetInstance()->AddLog("Prefab Mode: " + prefabName);
    return true;
}

bool DebugEditor::SavePrefabEditSession() {
    if (!prefabEditMode_ || !prefabEditRoot_ || !prefabEditScene_ ||
        !prefabEditScene_->IsAlive(prefabEditRoot_)) {
        return false;
    }

    // 一時Objectを指すUndoを先に破棄し、Asset更新だけをUndo履歴へ残します。
    EditorTransactionManager::GetInstance()->Clear();
    const std::string savedPrefabName = prefabEditName_;
    PresetManager* prefabManager = PresetManager::GetInstance();
    const auto beforePrefabs = prefabManager->GetPrefabs();
    if (!prefabManager->UpdatePrefabFromObject(savedPrefabName, prefabEditRoot_)) {
        return false;
    }
    const int synchronizedObjects = prefabManager->SynchronizePrefabInstances(
        beforePrefabs,
        prefabEditScene_->GetObjects(),
        prefabEditScene_->GetObject3dCommon());

    EndPrefabEditSession(false);
    if (synchronizedObjects > 0) {
        // 現行Scene形式はPrefab Instanceの実値も保存するため、同期結果をScene保存対象にします。
        MarkDirty(SaveMode::All);
    }
    DebugConsole::GetInstance()->AddLog(
        "Saved Prefab: " + savedPrefabName +
        " / synchronized " + std::to_string(synchronizedObjects) + " objects");
    return true;
}

void DebugEditor::CancelPrefabEditSession() {
    if (!prefabEditMode_) {
        return;
    }
    const std::string cancelledPrefabName = prefabEditName_;
    EndPrefabEditSession(true);
    DebugConsole::GetInstance()->AddLog("Discarded Prefab Mode: " + cancelledPrefabName);
}

bool DebugEditor::IsPrefabEditObject(const Object3d* object) const {
    if (!prefabEditMode_ || !object || !prefabEditRoot_) {
        return false;
    }

    const Object3d* current = object;
    for (int depth = 0; current && depth < 512; ++depth) {
        if (current == prefabEditRoot_) {
            return true;
        }
        current = current->GetParent();
    }
    return false;
}

void DebugEditor::EndPrefabEditSession(bool clearTransactions) {
    BaseScene* scene = prefabEditScene_;
    std::unordered_set<Object3d*> editObjects;
    if (scene) {
        for (auto& object : scene->GetObjects()) {
            if (object && IsPrefabEditObject(object.get())) {
                editObjects.insert(object.get());
            }
        }
        for (Object3d* object : editObjects) {
            CollisionManager::GetInstance()->RemoveObject(object);
        }

        auto& objects = scene->GetObjects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [&editObjects](const auto& object) {
            return object && editObjects.find(object.get()) != editObjects.end();
        }), objects.end());

        for (const auto& [object, wasVisible] : prefabEditPreviousVisibility_) {
            if (scene->IsAlive(object)) {
                object->SetIsVisible(wasVisible);
            }
        }
    }

    ClearObjectSelection();
    EditorManager::GetInstance()->ClearSelection();
    prefabEditMode_ = false;
    prefabEditDirty_ = false;
    prefabEditName_.clear();
    prefabEditRoot_ = nullptr;
    prefabEditScene_ = nullptr;
    prefabEditPreviousVisibility_.clear();
    if (clearTransactions) {
        EditorTransactionManager::GetInstance()->Clear();
    }
}

void DebugEditor::ResetPrefabEditSessionForSceneChange() {
    prefabEditMode_ = false;
    prefabEditDirty_ = false;
    prefabEditName_.clear();
    prefabEditRoot_ = nullptr;
    prefabEditScene_ = nullptr;
    prefabEditPreviousVisibility_.clear();
    EditorTransactionManager::GetInstance()->Clear();
}

// ========================================================================
// 更新 (ImGui処理)
// ========================================================================
// ========================================================================
// 更新 (ImGui処理 / エディタ操作のコア)
// ========================================================================
void DebugEditor::Update() {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    EffectPreviewStage::GetInstance()->Update();
    animationWorkbench_.Update(1.0f / 60.0f);
    textSpriteGenerator_.Update();
    text3DGenerator_.Update();

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    InputManager* input = InputManager::GetInstance();
    Math math;

    // =========================================================
    // 1. シーン変更リセット (最優先で行う: ダングリングポインタ防止)
    // =========================================================
    if (lastUpdatedScene_ != currentScene) {
        if (prefabEditMode_) {
            ResetPrefabEditSessionForSceneChange();
        }
        ClearObjectSelection();
        isSelectionRectDragging_ = false;
        isSelectionRectReady_ = false;
        previewObject_ = nullptr;
        previewChildObjects_.clear();
        brushPreviewObjects_.clear();
        isPresetBrushMode_ = false;
        brushPresetName_.clear();
        hasLastBrushStamp_ = false;
        previewCreateCommandLabel_ = "Place Preview Object";
        lastUpdatedScene_ = currentScene;
        EditorTransactionManager::GetInstance()->Clear();
        ClearDirty(SaveMode::All);
        hasInspectorEditStart_ = false;
        inspectorEditStartStates_.clear();
        requestGameViewCreateMenu_ = false;
        EditorManager::GetInstance()->ClearSelection();
    }

    // =========================================================
    // 2. エディタ状態の同期と更新
    // =========================================================
    IEditable* current = EditorManager::GetInstance()->GetSelectedObject();
    static std::string s_lastSyncedSceneFilename = "";
    std::string currentLoadedName = currentScene->GetLoadedFilename();

    // ファイル名が前フレームから変わった時「だけ」同期する
    if (!currentLoadedName.empty() && s_lastSyncedSceneFilename != currentLoadedName) {
        SetSceneFilename(currentLoadedName);
        s_lastSyncedSceneFilename = currentLoadedName;
    }

    ClearInvalidSelectedObject();

    CameraEditor::GetInstance()->SetGameViewHovered(isGameViewHovered_);

    // 選択対象が「DebugEditor自身」である間は、以前選んだオブジェクトを保持し続ける
    if (current != nullptr && current != this) {
        Object3d* obj = dynamic_cast<Object3d*>(current);
        if (obj) {
            SetSelectedObject(obj);
        }
    }

    // --- カメラ制御 (設置モード用) ---
    bool isPreviewActive = (previewObject_ != nullptr);
    if (isPreviewActive && !wasPreviewActive_) {
        CameraEditor* camEditor = CameraEditor::GetInstance();
        previousCameraMode_ = (int)camEditor->GetMode();
        camEditor->SetMode(CameraEditor::Mode::Editor);
        // カメラを強制的に上空へ移動させる機能を削除し、現在の視点を維持
    }
    else if (!isPreviewActive && wasPreviewActive_) {
        CameraEditor::GetInstance()->SetMode((CameraEditor::Mode)previousCameraMode_);
    }
    wasPreviewActive_ = isPreviewActive;

    // =========================================================
    //  モードA: 古い設置モード (Hierarchy等からのプレビュー配置)
    // =========================================================
    if (previewObject_) {
        UpdatePreviewPlacement();

        if (isGameViewHovered_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            ConfirmPreviewPlacement();
        }

        // 右クリック または Eキー で配置モードキャンセル
        if (input->IsKeyTriggered(DIK_E) || (isGameViewHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            CancelPreviewPlacement();
        }
    }
    else if (isPresetBrushMode_) {
        UpdatePresetBrush();
    }
    // =========================================================
    //  モードB: 通常選択・編集モード
    // =========================================================
    else {
        if (!ImGui::GetIO().WantCaptureKeyboard) {

            // --- ショートカットキー処理 ---
            if (input->IsKeyTriggered(DIK_DELETE)) {
                // パス編集モード中なら「点」を消す！
                if (isPathEditMode_ && selectedObject_ && selectedObject_->recorder_ && selectedObject_->recorder_->IsPinSelected()) {
                    selectedObject_->recorder_->DeleteSelectedPin();
                }
                // 通常モードなら「オブジェクト」を消す！
                else if (!isPathEditMode_) {
                    DeleteSelected();
                }
            }

            if ((input->IsKeyPressed(DIK_LCONTROL)) && input->IsKeyTriggered(DIK_C)) DuplicateSelected();
            if ((input->IsKeyPressed(DIK_LCONTROL)) && input->IsKeyTriggered(DIK_Z)) PerformUndo();
            if ((input->IsKeyPressed(DIK_LCONTROL)) && input->IsKeyTriggered(DIK_Y)) PerformRedo();
            if (input->IsKeyTriggered(DIK_END)) DropToFloor();

            // カメラフォーカス機能
            if (input->IsKeyTriggered(DIK_F) && current == CameraEditor::GetInstance()) {
                CameraEditor::GetInstance()->FocusSelectedCameraObject();
            }
            else if (input->IsKeyTriggered(DIK_F) && selectedObject_) {
                Vector3 targetPos = { selectedObject_->GetWorldMatrix().m[3][0],
                                      selectedObject_->GetWorldMatrix().m[3][1],
                                      selectedObject_->GetWorldMatrix().m[3][2] };

                // オブジェクトの少し手前・斜め上にカメラをワープさせる
                Vector3 newCamPos = { targetPos.x, targetPos.y + 5.0f, targetPos.z - 10.0f };
                Vector3 newCamRot = { ToRadians(20.0f), 0.0f, 0.0f };
                CameraEditor::GetInstance()->SetEditorCameraTransform(newCamPos, newCamRot);
            }
        }

        CameraEditor* cameraEditor = CameraEditor::GetInstance();
        const bool isCameraEditorSelected = current == cameraEditor;
        if (isCameraEditorSelected) {
            cameraEditor->DrawOrbitCenterGizmo(gameViewOffset_, gameViewSize_, isGridSnapEnabled_, snapValue_);
        }

        // --- マウス選択処理 (ギズモを触っていない ＆ パス編集中じゃない時) ---
        const bool canSelectInGameView = !isPathEditMode_ && !isCameraEditorSelected && !ImGuizmo::IsOver();
        if (canSelectInGameView && (isGameViewHovered_ || isSelectionRectDragging_)) {
            const bool isShiftPressed = ImGui::GetIO().KeyShift;
            constexpr float kSelectionDragThreshold = 6.0f;

            if (isGameViewHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                selectionRectStart_ = gameViewMousePos_;
                selectionRectEnd_ = gameViewMousePos_;
                isSelectionRectDragging_ = true;
                isSelectionRectReady_ = false;
            }

            if (isSelectionRectDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                selectionRectEnd_ = gameViewMousePos_;
                const float diffX = selectionRectEnd_.x - selectionRectStart_.x;
                const float diffY = selectionRectEnd_.y - selectionRectStart_.y;
                const float absX = diffX < 0.0f ? -diffX : diffX;
                const float absY = diffY < 0.0f ? -diffY : diffY;
                isSelectionRectReady_ = absX >= kSelectionDragThreshold || absY >= kSelectionDragThreshold;
            }

            if (isSelectionRectDragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                if (isSelectionRectReady_) {
                    SelectObjectsInGameViewRect(selectionRectStart_, selectionRectEnd_, isShiftPressed);
                } else {
                    Object3d* hit = PickObjectAtGameViewPos(gameViewMousePos_);
                    if (hit) {
                        if (isShiftPressed) {
                            ToggleSelectedObject(hit);
                        } else {
                            SetSelectedObject(hit);
                        }
                        SyncObjectSelectionToInspector();
                    } else if (!isShiftPressed) {
                        ClearObjectSelection();
                        SyncObjectSelectionToInspector();
                    }
                }

                isSelectionRectDragging_ = false;
                isSelectionRectReady_ = false;
            }
        }

        // --- ギズモ (ImGuizmo) 操作 ---
        if (selectedObject_) {
            if (!isPathEditMode_ && !selectedObject_->GetIsLocked()) {
                static ImGuizmo::OPERATION curOp = ImGuizmo::TRANSLATE;
                if (!ImGui::GetIO().WantCaptureKeyboard && !ImGui::GetIO().WantTextInput) {
                    if (input->IsKeyTriggered(DIK_T)) curOp = ImGuizmo::TRANSLATE;
                    if (input->IsKeyTriggered(DIK_R)) curOp = ImGuizmo::ROTATE;
                    if (input->IsKeyTriggered(DIK_S)) curOp = ImGuizmo::SCALE;
                }

                Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
                if (cam) {
                    ImGuizmo::SetDrawlist();
                    ImGuizmo::SetRect(gameViewOffset_.x, gameViewOffset_.y, gameViewSize_.x, gameViewSize_.y);

                    Matrix4x4 view = cam->GetViewMatrix();
                    Matrix4x4 proj = cam->GetProjectionMatrix();
                    Transform* tr = selectedObject_->GetTransform();

                    selectedObject_->UpdateWorldMatrix();
                    Matrix4x4 world = selectedObject_->GetWorldMatrix();

                    float snapVal = isGridSnapEnabled_ ? snapValue_ : 0.0f;
                    float snapArr[3] = { snapVal, snapVal, snapVal };

                    Matrix4x4 gizmoWorld = world;
                    Vector3 gizmoPivotWorld = { world.m[3][0], world.m[3][1], world.m[3][2] };
                    bool useTranslatedPivot = curOp == ImGuizmo::TRANSLATE && gizmoPivotMode_ != 0;

                    if (useTranslatedPivot) {
                        AABB pivotAabb{};
                        bool hasPivotAabb = false;

                        if (gizmoPivotMode_ == 1) {
                            pivotAabb = selectedObject_->GetModelWorldAABB();
                            hasPivotAabb = IsValidAabb(pivotAabb);
                        }
                        else if (gizmoPivotMode_ == 2) {
                            pivotAabb = selectedObject_->GetAABB();
                            hasPivotAabb = IsValidAabb(pivotAabb);
                        }

                        if (hasPivotAabb) {
                            gizmoPivotWorld = GetAabbCenter(pivotAabb);
                            gizmoWorld.m[3][0] = gizmoPivotWorld.x;
                            gizmoWorld.m[3][1] = gizmoPivotWorld.y;
                            gizmoWorld.m[3][2] = gizmoPivotWorld.z;
                        }
                        else {
                            useTranslatedPivot = false;
                        }
                    }

                    ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], curOp, ImGuizmo::WORLD, &gizmoWorld.m[0][0], nullptr, isGridSnapEnabled_ ? snapArr : nullptr);

                    if (ImGuizmo::IsUsing()) {
                        if (!isDraggingTransform_) {
                            isDraggingTransform_ = true;
                            tempTransformStart_ = *tr;
                            tempObjectStateStart_ = CaptureObjectState(selectedObject_);
                            groupTransformStartStates_.clear();
                            for (Object3d* object : selectedObjects_) {
                                if (!object || !IsObjectInCurrentScene(object)) continue;
                                ObjectStateSnapshot snapshot;
                                snapshot.object = object;
                                snapshot.beforeState = CaptureObjectState(object);
                                groupTransformStartStates_.push_back(snapshot);
                            }
                        }

                        Matrix4x4 primaryWorldBefore = world;
                        Matrix4x4 editedWorld = gizmoWorld;
                        if (useTranslatedPivot) {
                            Vector3 editedPivotWorld = {
                                gizmoWorld.m[3][0],
                                gizmoWorld.m[3][1],
                                gizmoWorld.m[3][2]
                            };
                            Vector3 pivotDelta = {
                                editedPivotWorld.x - gizmoPivotWorld.x,
                                editedPivotWorld.y - gizmoPivotWorld.y,
                                editedPivotWorld.z - gizmoPivotWorld.z
                            };

                            editedWorld = world;
                            editedWorld.m[3][0] += pivotDelta.x;
                            editedWorld.m[3][1] += pivotDelta.y;
                            editedWorld.m[3][2] += pivotDelta.z;
                        }

                        Matrix4x4 newLocalMat = editedWorld;
                        if (selectedObject_->GetParent()) {
                            Matrix4x4 parentWorldInv = math.Inverse(selectedObject_->GetParent()->GetWorldMatrix());
                            newLocalMat = math.Multiply(editedWorld, parentWorldInv);
                        }

                        Vector3 s, rDeg, t;
                        ImGuizmo::DecomposeMatrixToComponents(&newLocalMat.m[0][0], &t.x, &rDeg.x, &s.x);

                        tr->translate = t;
                        tr->scale = s;
                        tr->quaternion = math_.MatrixToQuaternion(newLocalMat);
                        tr->isQuaternionMaster = true; // クォータニオン優先モードにする
                        tr->rotate = { ToRadians(rDeg.x), ToRadians(rDeg.y), ToRadians(rDeg.z) };

                        selectedObject_->UpdateLocalMatrix();
                        selectedObject_->UpdateWorldMatrix();

                        Matrix4x4 primaryWorldAfter = selectedObject_->GetWorldMatrix();
                        Matrix4x4 groupDelta = math.Multiply(math.Inverse(primaryWorldBefore), primaryWorldAfter);

                        for (Object3d* object : selectedObjects_) {
                            if (!object || object == selectedObject_ || !IsObjectInCurrentScene(object) || object->GetIsLocked()) continue;

                            bool hasSelectedAncestor = false;
                            for (Object3d* parent = object->GetParent(); parent != nullptr; parent = parent->GetParent()) {
                                if (IsObjectSelected(parent)) {
                                    hasSelectedAncestor = true;
                                    break;
                                }
                            }
                            if (hasSelectedAncestor) {
                                continue;
                            }

                            object->UpdateWorldMatrix();
                            Matrix4x4 objectWorldBefore = object->GetWorldMatrix();
                            Matrix4x4 objectWorldAfter = math.Multiply(objectWorldBefore, groupDelta);
                            Matrix4x4 objectLocalAfter = objectWorldAfter;
                            if (object->GetParent()) {
                                Matrix4x4 parentWorldInv = math.Inverse(object->GetParent()->GetWorldMatrix());
                                objectLocalAfter = math.Multiply(objectWorldAfter, parentWorldInv);
                            }

                            Vector3 otherScale, otherRotDeg, otherTranslate;
                            ImGuizmo::DecomposeMatrixToComponents(&objectLocalAfter.m[0][0], &otherTranslate.x, &otherRotDeg.x, &otherScale.x);

                            Transform* otherTransform = object->GetTransform();
                            otherTransform->translate = otherTranslate;
                            otherTransform->scale = otherScale;
                            otherTransform->quaternion = math_.MatrixToQuaternion(objectLocalAfter);
                            otherTransform->isQuaternionMaster = true;
                            otherTransform->rotate = { ToRadians(otherRotDeg.x), ToRadians(otherRotDeg.y), ToRadians(otherRotDeg.z) };
                            object->UpdateLocalMatrix();
                            object->UpdateWorldMatrix();
                        }

                    }
                    else if (isDraggingTransform_) {
                        isDraggingTransform_ = false;
                        if (!groupTransformStartStates_.empty()) {
                            RegisterObjectsEdited(groupTransformStartStates_,
                                groupTransformStartStates_.size() > 1 ? "Gizmo Group Transform" : "Gizmo Transform");
                            groupTransformStartStates_.clear();
                        } else {
                            RegisterObjectEdited(selectedObject_, tempObjectStateStart_, "Gizmo Transform");
                        }
                        if (ghostDirector_ &&
                            EditorManager::GetInstance()->GetSelectedObject() == ghostDirector_ &&
                            ghostDirector_->IsAutoKeyEnabled()) {
                            for (Object3d* object : selectedObjects_) {
                                if (object && IsObjectInCurrentScene(object)) {
                                    ghostDirector_->RecordTransformKey(object);
                                }
                            }
                        }
                    }
                }
            }
        }
        DrawSelectionRectangleOverlay();
        DrawSelectedObjectBoundsOverlay();
    }

    // =========================================================
    // 3. UI描画関連 (最前面)
    // =========================================================
    PollDDSCacheNotifications();
    UpdateCrashRecovery(1.0f / 60.0f);
    DrawSaveNotification();
    DrawSavePreview();
    DrawCrashRecoveryPrompt();
    Draw3DIcons();
    DrawEventIDOverlay();
    DrawPreviewMarker();
    DrawPresetBrushOverlay();
    textSpriteGenerator_.DrawPreview();
#endif
}
// ========================================================================
// 終了処理
// ========================================================================
void DebugEditor::Finalize() {
    if (prefabEditMode_) {
        EndPrefabEditSession(true);
    }
    FinalizeCrashRecovery();
    animationWorkbench_.Finalize();
    primitiveDrawer_.Finalize();
    EditorTransactionManager::GetInstance()->Clear();
}


// ========================================================================
// コライダー描画処理 
// ========================================================================
void DebugEditor::DrawDebug(ID3D12GraphicsCommandList* commandList) {
    if (sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (!currentScene) return;

    // PrimitiveDrawer にパイプライン設定を委譲
    primitiveDrawer_.PreDraw(commandList);

    int instanceCount = 0;
    const int kMaxDrawLimit = 2048; // 描画上限（PrimitiveDrawerのバッファサイズと合わせる）
    Math math;

    // =========================================================
    // 1. シーン内のオブジェクトを描画 (統合版)
    // =========================================================
    auto& objects = currentScene->GetObjects();

    for (const auto& obj : objects) {
        if (!obj) continue;
        if (!obj->GetIsVisible()) continue;
        // インスタンス描画の上限チェック
        if (instanceCount >= kMaxDrawLimit) break;

        ColliderType type = obj->GetColliderType();
        bool isInvisibleObj = (obj->GetClassName() == "InvisibleBox");

        // --- 描画判定 ---
        // コライダーがなく、かつ「見える物体（モデルあり）」ならデバッグ線は不要
        if (type == ColliderType::kNone && !isInvisibleObj) continue;

        // コライダー表示OFF設定の時、「見える物体」のコライダーは消すが、
        // 「見えない物体(透明な壁など)」は編集用に表示したままにする
        if (!drawColliders_ && !isInvisibleObj) continue;

        // --- 行列計算 (サイズと位置) ---
        Matrix4x4 drawWorldMatrix = math.MakeIdentity4x4();

        // コライダーがある場合は、その形状データ(Size/Center/Rotation)に合わせて枠を変形させる
        if (type != ColliderType::kNone) {
            // コライダー設定を直接取得 (ImGuiでの変更を即座に反映させるため)
            Object3d::ColliderConfig config = obj->GetColliderConfig();

            if (type == ColliderType::kOBB) {
                // OBB (回転ありボックス) の計算
                Matrix4x4 matScale = math.MakeScaleMatrix(config.size * 2.0f);
                Matrix4x4 matRotX = math.MakeRotateXMatrix(config.rotation.x);
                Matrix4x4 matRotY = math.MakeRotateYMatrix(config.rotation.y);
                Matrix4x4 matRotZ = math.MakeRotateZMatrix(config.rotation.z);
                Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                Matrix4x4 matColliderLocal = math.Multiply(matScale, math.Multiply(matRot, matTrans));
                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());

            }
            else if (type == ColliderType::kAABB || type == ColliderType::kTerrain) {
                // AABB (軸平行ボックス) の計算
                Matrix4x4 matScale = math.MakeScaleMatrix(config.size * 2.0f);
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());

            }
            else if (type == ColliderType::kSphere) {
                // Sphere (球) の計算
                float radius = config.size.x; // Sphereはxを半径とする
                Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());
            }
            else if (type == ColliderType::kCylinder) {
                // size.x を半径、size.y を高さ(の半分)として扱っている想定
                float radius = config.size.x;
                float height = config.size.y;
                Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, height * 2.0f, radius * 2.0f });
                Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                drawWorldMatrix = math.Multiply(matColliderLocal, obj->GetWorldMatrix());
            }

        }
        else {
            // コライダー未設定の「見えない箱」の場合の救済措置
            // Transformそのままで表示（これがないと選択すらできなくなる）
            drawWorldMatrix = obj->GetWorldMatrix();
        }

        // --- 色の決定 ---
        Vector4 color;
        if (isInvisibleObj) { // isInvisible ではなく isInvisibleObj を使用
            // 見えないオブジェクトは「紫」固定
            color = { 0.6f, 0.0f, 0.8f, 1.0f };
        }
        else {
            // 通常オブジェクトはコライダー種別ごとの色
            switch (type) {
            case ColliderType::kOBB:    color = { 1.0f, 0.2f, 0.2f, 1.0f }; break; // 赤
            case ColliderType::kAABB:   color = { 0.0f, 1.0f, 0.0f, 1.0f }; break; // 緑
            case ColliderType::kTerrain: color = { 0.25f, 1.0f, 0.65f, 1.0f }; break;
            case ColliderType::kSphere: color = { 0.0f, 0.5f, 1.0f, 1.0f }; break; // 青
            case ColliderType::kCylinder: color = { 1.0f, 0.5f, 0.0f, 1.0f }; break; // オレンジ
            case ColliderType::kRing:   color = { 1.0f, 1.0f, 0.0f, 1.0f }; break; // 黄色
            default:                    color = { 1.0f, 1.0f, 1.0f, 1.0f }; break; // 白
            }
        }

        // PrimitiveDrawer で描画実行
        if (type == ColliderType::kSphere) {
            primitiveDrawer_.DrawWireSphere(commandList, drawWorldMatrix, color, instanceCount);
        }
        else if (type == ColliderType::kCylinder) {
            primitiveDrawer_.DrawWireCylinder(commandList, drawWorldMatrix, color, instanceCount);
        }
        else if (type == ColliderType::kRing) {
            // リング形状の描画 (クリーン版)
            Ring ring = obj->GetCollider()->GetRing();
            float outerR = ring.outerRadius;
            float innerR = ring.innerRadius;

            float halfH = (obj->GetColliderConfig().size.y > 0.01f) ? obj->GetColliderConfig().size.y : 0.1f;
            halfH *= std::abs(obj->GetTransform()->scale.y);

            // 基準行列 (回転 * 平行移動 * オブジェクトのワールド(スケール抜き))
            Matrix4x4 matRotX = math.MakeRotateXMatrix(obj->GetColliderConfig().rotation.x);
            Matrix4x4 matRotY = math.MakeRotateYMatrix(obj->GetColliderConfig().rotation.y);
            Matrix4x4 matRotZ = math.MakeRotateZMatrix(obj->GetColliderConfig().rotation.z);
            Matrix4x4 matCollRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));
            
            Matrix4x4 matObjWorldNoScale = obj->GetWorldMatrix();
            for (int i = 0; i < 3; ++i) {
                Vector3 axis = math.Normalize({ matObjWorldNoScale.m[i][0], matObjWorldNoScale.m[i][1], matObjWorldNoScale.m[i][2] });
                matObjWorldNoScale.m[i][0] = axis.x; matObjWorldNoScale.m[i][1] = axis.y; matObjWorldNoScale.m[i][2] = axis.z;
            }
            Matrix4x4 matRingBase = math.Multiply(math.Multiply(matCollRot, math.MakeTranslateMatrix(obj->GetColliderConfig().center)), matObjWorldNoScale);

            // 1. 外周・内周のワイヤーフレーム (上下の円)
            if (instanceCount < kMaxDrawLimit) {
                // 外周
                Matrix4x4 matOuter = math.Multiply(math.MakeScaleMatrix({ outerR * 2.0f, halfH * 2.0f, outerR * 2.0f }), matRingBase);
                primitiveDrawer_.DrawWireCylinder(commandList, matOuter, color, instanceCount++);
            }
            if (innerR > 0.01f && instanceCount < kMaxDrawLimit) {
                // 内周
                Matrix4x4 matInner = math.Multiply(math.MakeScaleMatrix({ innerR * 2.0f, halfH * 2.0f, innerR * 2.0f }), matRingBase);
                primitiveDrawer_.DrawWireCylinder(commandList, matInner, { color.x, color.y * 0.5f, 0, 1 }, instanceCount++);
            }

            // 2. 断面をつなぐ線 (上下それぞれ8方向)
            const int kLineCount = 8;
            for (int i = 0; i < kLineCount && instanceCount < kMaxDrawLimit; ++i) {
                float angle = (2.0f * 3.14159265f / kLineCount) * i;
                float s = std::sin(angle);
                float c = std::cos(angle);

                // 上下の断面をつなぐ線 (内径から外径へ)
                for (float h : {-halfH, halfH}) {
                    Vector3 pInner = { innerR * c, h, innerR * s };
                    Vector3 pOuter = { outerR * c, h, outerR * s };
                    
                    // 線を細い箱で代用 (PrimitiveDrawerにDrawLineがあればそちらが良いが、無ければ極小Box)
                    Vector3 center = (pInner + pOuter) * 0.5f;
                    Vector3 diff = pOuter - pInner;
                    float len = math.Length(diff);
                    
                    Matrix4x4 matL = math.MakeScaleMatrix({ 0.02f, 0.02f, len });
                    Matrix4x4 matLR = math.MakeRotateYMatrix(angle);
                    Matrix4x4 matLT = math.MakeTranslateMatrix(center);
                    Matrix4x4 lineWorld = math.Multiply(math.Multiply(matL, math.Multiply(matLR, matLT)), matRingBase);
                    
                    primitiveDrawer_.DrawWireCube(commandList, lineWorld, color, instanceCount++);
                }
            }
            continue;
        }
        else {
            primitiveDrawer_.DrawWireCube(commandList, drawWorldMatrix, color, instanceCount);
        }
        instanceCount++; // あたり判定を描画したのでカウントを進める

        // 検知範囲の可視化 (敵のみ)
        if (obj->GetClassName() == "Enemy") {
            float range = 0.0f;
            if (obj->param_.has_value()) {
                range = obj->param_->detectionRange;
            }

            // インスタンス描画の上限チェック
            if (instanceCount < kMaxDrawLimit && range > 0.0f) {
                Matrix4x4 matScale = math.MakeScaleMatrix({ range * 2.0f, range * 2.0f, range * 2.0f });
                Matrix4x4 matTrans = math.MakeTranslateMatrix(obj->GetWorldPosition());
                Matrix4x4 rangeWorldMatrix = math.Multiply(matScale, matTrans);
                
                // 検知範囲は薄い赤色
                Vector4 rangeColor = { 1.0f, 0.0f, 0.0f, 0.3f };
                primitiveDrawer_.DrawWireSphere(commandList, rangeWorldMatrix, rangeColor, instanceCount);
                instanceCount++; // 検知範囲を描画したのでカウントを進める
            }
        }

    }

    // =========================================================
    // 2. 弾のコライダー描画
    // =========================================================
    CameraEditor::GetInstance()->DrawOrbitGuide(primitiveDrawer_, commandList, instanceCount, kMaxDrawLimit);
    DrawPreviewWire(commandList, instanceCount, kMaxDrawLimit);

    if (drawColliders_) {
        const auto& bullets = BulletManager::GetInstance()->GetBullets();

        for (const auto& bullet : bullets) {
            if (!bullet || bullet->IsDead()) continue;
            if (instanceCount >= kMaxDrawLimit) break;

            ColliderType type = bullet->GetColliderType();
            if (type == ColliderType::kNone) continue;

            // 弾は黄色固定
            Vector4 color = { 1.0f, 1.0f, 0.0f, 1.0f };
            Matrix4x4 drawWorldMatrix = math.MakeIdentity4x4();

            // 弾の場合は物理挙動の結果(GetOBB)をそのまま信じて描画する
            if (type == ColliderType::kOBB) {
                OBB obb = bullet->GetOBB();
                Matrix4x4 matScale = math.MakeScaleMatrix(obb.size * 2.0f);

                // OBBの軸から回転行列を復元
                Matrix4x4 matRot = math.MakeIdentity4x4();
                matRot.m[0][0] = obb.orientations[0].x; matRot.m[0][1] = obb.orientations[0].y; matRot.m[0][2] = obb.orientations[0].z;
                matRot.m[1][0] = obb.orientations[1].x; matRot.m[1][1] = obb.orientations[1].y; matRot.m[1][2] = obb.orientations[1].z;
                matRot.m[2][0] = obb.orientations[2].x; matRot.m[2][1] = obb.orientations[2].y; matRot.m[2][2] = obb.orientations[2].z;

                Matrix4x4 matTrans = math.MakeTranslateMatrix(obb.center);
                drawWorldMatrix = math.Multiply(matScale, math.Multiply(matRot, matTrans));

            }
            else if (type == ColliderType::kAABB) {
                AABB aabb = bullet->GetAABB();
                Vector3 center = (aabb.min + aabb.max) * 0.5f;
                Vector3 size = aabb.max - aabb.min;
                Matrix4x4 matScale = math.MakeScaleMatrix(size);
                Matrix4x4 matTrans = math.MakeTranslateMatrix(center);
                drawWorldMatrix = math.Multiply(matScale, matTrans);

            }
            else if (type == ColliderType::kSphere) {
                float radius = bullet->GetCollisionRadius();
                Vector3 center = bullet->GetWorldPosition();
                Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
                Matrix4x4 matTrans = math.MakeTranslateMatrix(center);
                drawWorldMatrix = math.Multiply(matScale, matTrans);
            }

            // PrimitiveDrawer で弾も描画実行
            if (type == ColliderType::kSphere) {
                primitiveDrawer_.DrawWireSphere(commandList, drawWorldMatrix, color, instanceCount);
            }
            else if (type == ColliderType::kCylinder) {
                primitiveDrawer_.DrawWireCylinder(commandList, drawWorldMatrix, color, instanceCount);
            }
            else {
                primitiveDrawer_.DrawWireCube(commandList, drawWorldMatrix, color, instanceCount);
            }
            instanceCount++;
        }
        // =========================================================
    // 3. エフェクトのコライダー描画
    // =========================================================
        if (drawColliders_) {
            // ゲーム中のエフェクト ＋ エディタのプレビューエフェクトを両方収集
            std::vector<EffectObject3d*> effectsToDraw;

            for (const auto& eff : MeshEffectManager::GetInstance()->GetActiveEffects()) {
                if (eff) effectsToDraw.push_back(eff.get());
            }
            if (EffectObject3d* preview = MeshEffectManager::GetInstance()->GetPreviewEffectForDebug()) {
                effectsToDraw.push_back(preview);
            }

            // 集めたエフェクトを描画！
            for (EffectObject3d* effect : effectsToDraw) {
                if (instanceCount >= kMaxDrawLimit) break;

                ColliderType type = effect->GetColliderType();
                if (type == ColliderType::kNone) continue;

                // エフェクトの判定枠はシアン（水色）にして区別
                Vector4 color = { 0.0f, 1.0f, 1.0f, 1.0f };
                Matrix4x4 drawWorldMatrix = math.MakeIdentity4x4();

                Object3d::ColliderConfig config = effect->GetColliderConfig();

                if (type == ColliderType::kOBB) {
                    Matrix4x4 matScale = math.MakeScaleMatrix(config.size * 2.0f);
                    Matrix4x4 matRotX = math.MakeRotateXMatrix(config.rotation.x);
                    Matrix4x4 matRotY = math.MakeRotateYMatrix(config.rotation.y);
                    Matrix4x4 matRotZ = math.MakeRotateZMatrix(config.rotation.z);
                    Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));
                    Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                    Matrix4x4 matColliderLocal = math.Multiply(matScale, math.Multiply(matRot, matTrans));
                    drawWorldMatrix = math.Multiply(matColliderLocal, effect->GetWorldMatrix());

                }
                else if (type == ColliderType::kAABB) {
                    Matrix4x4 matScale = math.MakeScaleMatrix(config.size * 2.0f);
                    Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                    Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                    drawWorldMatrix = math.Multiply(matColliderLocal, effect->GetWorldMatrix());

                }
                else if (type == ColliderType::kSphere) {
                    float radius = config.size.x;
                    Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, radius * 2.0f, radius * 2.0f });
                    Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                    Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                    drawWorldMatrix = math.Multiply(matColliderLocal, effect->GetWorldMatrix());

                }
                else if (type == ColliderType::kCylinder) {
                    float radius = config.size.x;
                    float height = config.size.y;
                    Matrix4x4 matScale = math.MakeScaleMatrix({ radius * 2.0f, height * 2.0f, radius * 2.0f });
                    Matrix4x4 matTrans = math.MakeTranslateMatrix(config.center);
                    Matrix4x4 matColliderLocal = math.Multiply(matScale, matTrans);
                    drawWorldMatrix = math.Multiply(matColliderLocal, effect->GetWorldMatrix());
                }

                // Ring形状の場合
                if (type == ColliderType::kRing) {
                    Ring ring;
                    if (effect->GetCollider()) {
                        ring = effect->GetCollider()->GetRing();
                    } else {
                        // コライダーがない場合(エフェクト単体)の救済
                        ring.center = effect->GetWorldPosition();
                        ring.normal = { 0, 1, 0 };
                        ring.innerRadius = effect->editRingInnerRadius_;
                        ring.outerRadius = effect->editRingOuterRadius_;
                    }
                    float outerR = ring.outerRadius;
                    float innerR = ring.innerRadius;
                    if (innerR > outerR) std::swap(innerR, outerR);

                    float halfH = (config.size.y > 0.01f) ? config.size.y : 0.1f;
                    halfH *= std::abs(effect->GetTransform()->scale.y);

                    // リングの基準行列
                    Matrix4x4 matCollRot = math.MakeIdentity4x4();
                    if (effect->GetCollider()) {
                        Matrix4x4 mRX = math.MakeRotateXMatrix(config.rotation.x);
                        Matrix4x4 mRY = math.MakeRotateYMatrix(config.rotation.y);
                        Matrix4x4 mRZ = math.MakeRotateZMatrix(config.rotation.z);
                        matCollRot = math.Multiply(mRZ, math.Multiply(mRX, mRY));
                    }
                    
                    Matrix4x4 matObjWorldNoScale = effect->GetWorldMatrix();
                    for (int i = 0; i < 3; ++i) {
                        Vector3 axis = math.Normalize({ matObjWorldNoScale.m[i][0], matObjWorldNoScale.m[i][1], matObjWorldNoScale.m[i][2] });
                        matObjWorldNoScale.m[i][0] = axis.x; matObjWorldNoScale.m[i][1] = axis.y; matObjWorldNoScale.m[i][2] = axis.z;
                    }
                    Matrix4x4 matRingBase = math.Multiply(math.Multiply(matCollRot, math.MakeTranslateMatrix(config.center)), matObjWorldNoScale);

                    Vector4 ringColor = { 1.0f, 1.0f, 0.0f, 1.0f };

                    // 1. シリンダー (外周・内周)
                    if (instanceCount < kMaxDrawLimit) {
                        Matrix4x4 matOuter = math.Multiply(math.MakeScaleMatrix({ outerR * 2.0f, halfH * 2.0f, outerR * 2.0f }), matRingBase);
                        primitiveDrawer_.DrawWireCylinder(commandList, matOuter, { 1.0f, 0.9f, 0.0f, 0.8f }, instanceCount++);
                    }
                    if (innerR > 0.01f && instanceCount < kMaxDrawLimit) {
                        Matrix4x4 matInner = math.Multiply(math.MakeScaleMatrix({ innerR * 2.0f, halfH * 2.0f, innerR * 2.0f }), matRingBase);
                        primitiveDrawer_.DrawWireCylinder(commandList, matInner, { 1.0f, 0.5f, 0.0f, 0.8f }, instanceCount++);
                    }

                    // 2. 断面をつなぐ線
                    const int kLineCount = 8;
                    for (int i = 0; i < kLineCount && instanceCount < kMaxDrawLimit; ++i) {
                        float angle = (2.0f * 3.14159265f / kLineCount) * i;
                        float s = std::sin(angle);
                        float c = std::cos(angle);
                        for (float h : { -halfH, halfH }) {
                            Vector3 pInner = { innerR * c, h, innerR * s };
                            Vector3 pOuter = { outerR * c, h, outerR * s };
                            Vector3 center = (pInner + pOuter) * 0.5f;
                            Vector3 diff = pOuter - pInner;
                            float len = math.Length(diff);
                            Matrix4x4 matL = math.MakeScaleMatrix({ 0.02f, 0.02f, len });
                            Matrix4x4 matLR = math.MakeRotateYMatrix(angle);
                            Matrix4x4 matLT = math.MakeTranslateMatrix(center);
                            Matrix4x4 lineWorld = math.Multiply(math.Multiply(matL, math.Multiply(matLR, matLT)), matRingBase);
                            primitiveDrawer_.DrawWireCube(commandList, lineWorld, ringColor, instanceCount++);
                        }
                    }
                    continue;
                }

                if (type == ColliderType::kSphere) {
                    primitiveDrawer_.DrawWireSphere(commandList, drawWorldMatrix, color, instanceCount);
                }
                else if (type == ColliderType::kCylinder) {
                    primitiveDrawer_.DrawWireCylinder(commandList, drawWorldMatrix, color, instanceCount);
                }
                else {
                    primitiveDrawer_.DrawWireCube(commandList, drawWorldMatrix, color, instanceCount);
                }
                instanceCount++;
            }
        }
    }

}

// ==========================================================================================
// 1. 左パネル：Hierarchy (階層構造) と 生成メニュー
// ==========================================================================================
void DebugEditor::DrawHierarchy() {
    hierarchyWindow_.Draw();
    propertyMatrixWindow_.DrawWindow();
}
// ==========================================================================================
// 2. 右パネル：Inspector (選択したオブジェクトの詳細設定)
// ==========================================================================================
void DebugEditor::DrawImGui() {
    const std::vector<ObjectStateSnapshot> beforeStates = CaptureObjectStates(selectedObjects_);

    inspectorWindow_.Draw();

#ifdef USE_IMGUI
    if (selectedObject_) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("ギズモ設定", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* pivotModes[] = { "原点", "モデル中心", "コリジョン中心" };
            ImGui::Combo("ギズモ基準", &gizmoPivotMode_, pivotModes, IM_ARRAYSIZE(pivotModes));
            if (gizmoPivotMode_ != 0) {
                ImGui::TextDisabled("移動ギズモの表示位置だけを補正します。回転とスケールは原点基準です。");
            }

            ImGui::SeparatorText("選択表示");
            int overlayMode = static_cast<int>(selectionOverlayMode_);
            const char* overlayModes[] = { "簡易", "詳細", "非表示" };
            if (ImGui::Combo("複数選択の可視化", &overlayMode, overlayModes, IM_ARRAYSIZE(overlayModes))) {
                selectionOverlayMode_ = static_cast<SelectionOverlayMode>(overlayMode);
            }

            const std::string activeName = selectedObject_->GetName().empty() ? "Selected" : selectedObject_->GetName();
            ImGui::Text("選択: %zu個 / 操作対象: %s", selectedObjects_.size(), activeName.c_str());
            if (selectionOverlayMode_ == SelectionOverlayMode::Compact && selectedObjects_.size() > 1) {
                ImGui::TextDisabled("副選択は小さいマーカーで表示します。Altを押している間は外枠を表示します。");
            } else if (selectionOverlayMode_ == SelectionOverlayMode::Hidden) {
                ImGui::TextDisabled("選択枠を隠し、ImGuizmoだけを表示します。");
            }
        }
    }

    TrackInspectorEdit(beforeStates);
#endif
}



#ifdef USE_IMGUI
void DebugEditor::DrawProjectWindow() {
    if (!projectWindowVisible_) {
        return;
    }
    projectWindow_.Draw();
}
#endif

