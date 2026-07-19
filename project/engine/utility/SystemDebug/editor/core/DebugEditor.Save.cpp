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
#include "SceneController.h"
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

std::vector<SceneSerializer::SceneAssetInfo> DebugEditor::GetSceneAssets() const {
    return serializer_.DiscoverSceneAssets();
}

bool DebugEditor::CreateSceneAsset(
    const std::string& sceneId,
    const std::string& displayName,
    const std::string& runtimeScene,
    SceneSerializer::SceneAssetTemplate sceneTemplate,
    std::string& createdFilename,
    std::string& errorMessage) {
    const bool created = serializer_.CreateSceneAsset(
        sceneId,
        displayName,
        runtimeScene,
        sceneTemplate,
        createdFilename,
        errorMessage);
    if (created) {
        DebugConsole::GetInstance()->AddLog("Created Scene Asset: " + createdFilename);
    }
    return created;
}

bool DebugEditor::DuplicateSceneAsset(
    const std::string& sourceFilename,
    const std::string& newSceneId,
    const std::string& displayName,
    std::string& createdFilename,
    std::string& errorMessage) {
    const bool duplicated = serializer_.DuplicateSceneAsset(
        sourceFilename,
        newSceneId,
        displayName,
        createdFilename,
        errorMessage);
    if (duplicated) {
        DebugConsole::GetInstance()->AddLog(
            "Duplicated Scene Asset: " + sourceFilename + " -> " + createdFilename);
    }
    return duplicated;
}

bool DebugEditor::RenameSceneAsset(
    const std::string& sourceFilename,
    const std::string& newSceneId,
    const std::string& displayName,
    std::string& renamedFilename,
    std::string& errorMessage) {
    const bool wasCurrentScene = IsCurrentSceneAsset(sourceFilename);
    const bool renamed = serializer_.RenameSceneAsset(
        sourceFilename,
        newSceneId,
        displayName,
        renamedFilename,
        errorMessage);
    if (!renamed) {
        return false;
    }

    if (wasCurrentScene) {
        SetSceneFilename(renamedFilename);
        if (sceneManager_) {
            sceneManager_->SetEditorSceneAssetPaths(
                serializer_.ResolveSceneAssetObjectPath(renamedFilename),
                serializer_.ResolveSceneAssetSpritePath(renamedFilename));
        }
    }
    DebugConsole::GetInstance()->AddLog(
        "Renamed Scene Asset: " + sourceFilename + " -> " + renamedFilename);
    return true;
}

bool DebugEditor::DeleteSceneAsset(const std::string& filename, std::string& errorMessage) {
    if (IsCurrentSceneAsset(filename)) {
        errorMessage = "開いているScene Assetは削除できません。別のシーンを開いてから削除してください。";
        return false;
    }
    if (!serializer_.DeleteSceneAsset(filename, errorMessage)) {
        return false;
    }
    DebugConsole::GetInstance()->AddLog("Deleted Scene Asset: " + filename);
    return true;
}

bool DebugEditor::SetSceneAssetRuntimeScene(
    const std::string& filename,
    const std::string& runtimeScene,
    std::string& errorMessage) {
    if (!serializer_.SetSceneAssetRuntimeScene(filename, runtimeScene, errorMessage)) {
        return false;
    }
    DebugConsole::GetInstance()->AddLog(
        "Scene Asset runtime changed: " + filename + " -> " + runtimeScene);
    return true;
}

bool DebugEditor::SetSceneAssetRuntimeSettings(
    const std::string& filename,
    const std::string& controllerName,
    const std::string& bgmPath,
    const std::string& lightPath,
    const std::string& cameraPath,
    const std::string& skyboxPath,
    std::string& errorMessage) {
    if (!serializer_.SetSceneAssetRuntimeSettings(
        filename,
        controllerName,
        bgmPath,
        lightPath,
        cameraPath,
        skyboxPath,
        errorMessage)) {
        return false;
    }
    DebugConsole::GetInstance()->AddLog("Updated Scene Asset runtime settings: " + filename);
    return true;
}

SceneSerializer::SceneAssetValidationResult DebugEditor::ValidateSceneAsset(
    const std::string& filename) const {
    SceneSerializer::SceneAssetValidationResult result = serializer_.ValidateSceneAsset(filename);
    const std::vector<SceneSerializer::SceneAssetInfo> assets = serializer_.DiscoverSceneAssets();
    const auto asset = std::find_if(assets.begin(), assets.end(), [&](const SceneSerializer::SceneAssetInfo& candidate) {
        return candidate.filename == filename;
    });
    if (asset != assets.end() && asset->runtimeScene == "GAMEPLAY" &&
        !SceneControllerFactory::GetInstance()->IsRegistered(asset->controllerName)) {
        result.errors.push_back("Scene Controllerが登録されていません: " + asset->controllerName);
    }
    return result;
}

std::vector<std::string> DebugEditor::GetRegisteredSceneNames() const {
    return sceneManager_ ? sceneManager_->GetRegisteredSceneNames() : std::vector<std::string>{};
}

bool DebugEditor::OpenSceneAsset(
    const std::string& filename,
    bool discardUnsavedChanges,
    std::string& errorMessage) {
    errorMessage.clear();
    if (!sceneManager_) {
        errorMessage = "SceneManagerが初期化されていません。";
        return false;
    }
    if (sceneManager_->IsPlaying()) {
        errorMessage = "実行中はScene Assetを切り替えられません。停止してから開いてください。";
        return false;
    }
    if (prefabEditMode_) {
        errorMessage = "Prefab Modeを終了してからScene Assetを開いてください。";
        return false;
    }
    if (HasAnyDirty() && !discardUnsavedChanges) {
        errorMessage = "未保存の変更があります。保存するか、破棄して開く操作を選んでください。";
        return false;
    }

    const std::string objectPath = serializer_.ResolveSceneAssetObjectPath(filename);
    const std::string spritePath = serializer_.ResolveSceneAssetSpritePath(filename);
    const std::vector<SceneSerializer::SceneAssetInfo> assets = serializer_.DiscoverSceneAssets();
    const auto asset = std::find_if(assets.begin(), assets.end(), [&](const SceneSerializer::SceneAssetInfo& candidate) {
        return candidate.filename == filename;
    });
    if (asset == assets.end()) {
        errorMessage = "Scene Assetが見つかりません: " + filename;
        return false;
    }
    if (!sceneManager_->IsSceneRegistered(asset->runtimeScene)) {
        errorMessage = "実行クラスがSceneFactoryへ登録されていません: " + asset->runtimeScene;
        return false;
    }
    const SceneSerializer::SceneAssetValidationResult validation = ValidateSceneAsset(filename);
    if (!validation.IsValid()) {
        errorMessage = validation.errors.front();
        for (size_t index = 1; index < validation.errors.size(); ++index) {
            errorMessage += "\n" + validation.errors[index];
        }
        return false;
    }
    for (const std::string& warning : validation.warnings) {
        DebugConsole::GetInstance()->AddLog("Scene validation warning: " + warning);
    }

    SceneLoadContext context;
    context.sceneAssetId = asset->id;
    context.displayName = asset->displayName;
    context.runtimeScene = asset->runtimeScene;
    context.objectLayoutPath = objectPath;
    context.spriteLayoutPath = spritePath;
    context.controllerName = asset->controllerName;
    context.bgmPath = asset->bgmPath;
    context.lightPath = asset->lightPath;
    context.cameraPath = asset->cameraPath;
    context.skyboxPath = asset->skyboxPath;
    if (!sceneManager_->OpenSceneAsset(context)) {
        errorMessage = "シーン遷移中のためScene Assetを開けませんでした。";
        return false;
    }

    if (ghostRecorder_) {
        ghostRecorder_->ClearTarget();
    }
    ClearObjectSelection();
    EditorManager::GetInstance()->ClearSelection();
    EditorTransactionManager::GetInstance()->Clear();
    pendingSceneAssetOpenAfterSave_.clear();
    ClearDirty(SaveMode::All);
    SetSceneFilename(filename);
    DebugConsole::GetInstance()->AddLog("Opening Scene Asset: " + filename);
    return true;
}

void DebugEditor::SaveThenOpenSceneAsset(const std::string& filename) {
    pendingSceneAssetOpenAfterSave_ = filename;
    SaveScene(SaveMode::All);
}

bool DebugEditor::IsCurrentSceneAsset(const std::string& filename) const {
    if (!sceneManager_ || !sceneManager_->HasActiveSceneAsset()) {
        return false;
    }
    std::string leafName = filename;
    const size_t slash = leafName.find_last_of("/\\");
    if (slash != std::string::npos) {
        leafName = leafName.substr(slash + 1);
    }
    return leafName == currentSceneFilename_;
}

void DebugEditor::OpenCreateSceneAssetDialog() {
    hierarchyWindow_.OpenCreateSceneDialog();
}

void DebugEditor::RequestOpenSceneAssetFromMenu(const std::string& filename) {
    hierarchyWindow_.OpenSceneAssetFromMenu(filename);
}

void DebugEditor::SaveScene(SaveMode mode) {
    if (prefabEditMode_) {
        SavePrefabEditSession();
        return;
    }
#ifdef USE_IMGUI
    auto targets = serializer_.BuildSceneSaveTargets(currentSceneFilename_, mode);
    if (targets.empty()) {
        DebugConsole::GetInstance()->AddLog("Save Preview: 保存対象がありません。");
        return;
    }

    sceneSavePreview_.Build(targets, MakeSavePreviewTitle(mode));
    pendingSaveMode_ = mode;
    pendingSaveIsSingleObject_ = false;
    sceneSavePreview_.Open();
#else
    serializer_.SaveScene(currentSceneFilename_, mode);
#endif
}
void DebugEditor::SaveSingleObject() {
    if (prefabEditMode_) {
        SavePrefabEditSession();
        return;
    }
    if (!selectedObject_) return;

#ifdef USE_IMGUI
    auto targets = serializer_.BuildSingleObjectSaveTargets(selectedObject_, std::string(currentSceneFilename_));
    if (targets.empty()) {
        DebugConsole::GetInstance()->AddLog("Save Preview: 単体保存の対象を作成できませんでした。");
        return;
    }

    sceneSavePreview_.Build(targets, "単体保存: " + selectedObject_->GetName());
    pendingSaveMode_ = selectedObject_->IsCameraObject() ? SaveMode::Camera : SaveMode::Object;
    pendingSaveIsSingleObject_ = true;
    sceneSavePreview_.Open();
#else
    serializer_.UpdateObjectInSceneJSON(selectedObject_, std::string(currentSceneFilename_));
#endif
}

void DebugEditor::DrawSavePreview() {
#ifdef USE_IMGUI
    SceneSavePreview::Action action = sceneSavePreview_.Draw();
    if (action == SceneSavePreview::Action::None) {
        return;
    }

    if (action == SceneSavePreview::Action::Confirm) {
        std::string savedFiles = serializer_.SaveTargets(sceneSavePreview_.GetTargets());
        if (!pendingSaveIsSingleObject_) {
            ClearDirty(pendingSaveMode_);
        }
        std::string notificationName = sceneSavePreview_.GetTitle();
        if (!savedFiles.empty()) {
            notificationName += " (" + savedFiles + ")";
        }
        DebugConsole::GetInstance()->AddLog("Save Preview: 保存を確定しました。");
        TriggerSaveNotification(notificationName);

        if (!pendingSceneAssetOpenAfterSave_.empty() && !HasAnyDirty()) {
            const std::string filenameToOpen = pendingSceneAssetOpenAfterSave_;
            pendingSceneAssetOpenAfterSave_.clear();
            std::string openError;
            if (!OpenSceneAsset(filenameToOpen, false, openError)) {
                DebugConsole::GetInstance()->AddLog("Scene Asset Open Error: " + openError);
            }
        }
    }
    else {
        DebugConsole::GetInstance()->AddLog("Save Preview: 保存をキャンセルしました。");
        pendingSceneAssetOpenAfterSave_.clear();
    }

    sceneSavePreview_.Close();
    pendingSaveIsSingleObject_ = false;
#endif
}

std::string DebugEditor::MakeSavePreviewTitle(SaveMode mode) const {
    std::string baseName = currentSceneFilename_;
    size_t extPos = baseName.find(".json");
    if (extPos != std::string::npos) baseName = baseName.substr(0, extPos);

    switch (mode) {
    case SaveMode::Player:
        return "シーン保存: " + baseName + " / Player";
    case SaveMode::Enemy:
        return "シーン保存: " + baseName + " / Enemy";
    case SaveMode::Object:
        return "シーン保存: " + baseName + " / Object";
    case SaveMode::Camera:
        return "シーン保存: " + baseName + " / Camera";
    case SaveMode::All:
    default:
        return "シーン全体保存: " + baseName;
    }
}

