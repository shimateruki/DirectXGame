#define NOMINMAX
#include "DebugEditor.h"
#include "SceneManager.h"    
#include "BaseScene.h"      
#include "engine/graphics/effect/DecalSystem.h"
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
        if (object && object->IsDecal()) {
            return object->GetDecalSettings().depthOffset;
        }
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

void DebugEditor::SetPreviewObject(std::unique_ptr<Object3d> obj, const std::string& label) {
    previewChildObjects_.clear();
    previewVisualStates_.clear();
    previewObject_ = std::move(obj);
    previewCreateCommandLabel_ = label;
    if (!previewObject_) {
        previewObjectOriginalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
        previewObjectOriginalBlendMode_ = BlendMode::kNormal;
        previewObjectOriginalMaterialType_ = 0;
        previewObjectOriginalSelectedLighting_ = 2;
        previewObjectOriginalEnableLighting_ = 1;
        previewObjectOriginalEmissive_ = 1.0f;
        previewObjectOriginalClassName_.clear();
        previewObjectOriginalModelName_.clear();
        previewObjectOriginalModel_ = nullptr;
        previewObjectOriginalColliderConfig_ = ColliderConfig{};
        previewObjectUsesFallbackModel_ = false;
        hasPreviewPlacementContact_ = false;
        return;
    }

    previewObjectOriginalColor_ = previewObject_->GetColor();
    previewObjectOriginalBlendMode_ = previewObject_->GetBlendMode();
    previewObjectOriginalMaterialType_ = previewObject_->GetMaterialType();
    if (auto* material = previewObject_->GetMaterialData()) {
        previewObjectOriginalSelectedLighting_ = material->selectedLighting;
        previewObjectOriginalEnableLighting_ = material->enableLighting;
    }
    else {
        previewObjectOriginalSelectedLighting_ = 2;
        previewObjectOriginalEnableLighting_ = 1;
    }
    previewObjectOriginalEmissive_ = previewObject_->GetEmissive();
    previewObjectOriginalClassName_ = previewObject_->GetClassName();
    previewObjectOriginalModelName_ = previewObject_->GetModelName();
    previewObjectOriginalModel_ = previewObject_->GetModel();
    previewObjectOriginalColliderConfig_ = previewObject_->GetColliderConfig();
    previewObjectUsesFallbackModel_ = false;
    hasPreviewPlacementContact_ = false;
    ApplyPreviewVisual(previewObject_.get());
}

void DebugEditor::OpenGameViewCreateContextMenu() {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    if (sceneManager_->IsPlaying()) return;
    if (previewObject_ || isPresetBrushMode_ || isPathEditMode_) return;

    gameViewCreateMenuMousePos_ = gameViewMousePos_;
    gameViewCreateMenuScreenPos_ = {
        gameViewOffset_.x + gameViewMousePos_.x,
        gameViewOffset_.y + gameViewMousePos_.y
    };
    requestGameViewCreateMenu_ = true;
#endif
}

void DebugEditor::StartGameViewCreatePreview(std::unique_ptr<Object3d> object, const std::string& label) {
#ifdef USE_IMGUI
    if (!object || !sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    if (sceneManager_->IsPlaying()) return;

    if (isPresetBrushMode_) {
        StopPresetBrush();
    }

    gameViewMousePos_ = gameViewCreateMenuMousePos_;
    SetPreviewObject(std::move(object), label);

    if (previewObject_) {
        PlacementResult placement = CalculateGameViewPlacement(previewObject_.get(), gameViewCreateMenuMousePos_, false);
        ApplyGameViewPlacement(previewObject_.get(), placement, true);
        ApplyPreviewVisual(previewObject_.get());
        DebugConsole::GetInstance()->AddLog("Preview Create: " + previewObject_->GetName());
    }
#else
    (void)object;
    (void)label;
#endif
}

void DebugEditor::StartGameViewCreatePreview(std::vector<std::unique_ptr<Object3d>> objects, const std::string& label) {
#ifdef USE_IMGUI
    if (objects.empty()) return;

    std::unique_ptr<Object3d> root = std::move(objects.front());
    StartGameViewCreatePreview(std::move(root), label);
    if (!previewObject_) return;

    previewChildObjects_.reserve(objects.size() > 0 ? objects.size() - 1 : 0);
    for (size_t index = 1; index < objects.size(); ++index) {
        if (objects[index]) {
            previewChildObjects_.push_back(std::move(objects[index]));
            ApplyPreviewVisual(previewChildObjects_.back().get());
        }
    }

    previewObject_->UpdateLocalMatrix();
    previewObject_->UpdateWorldMatrix();
#else
    (void)objects;
    (void)label;
#endif
}

void DebugEditor::DrawGameViewCreateContextMenu() {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    constexpr const char* popupName = "GameViewCreateContextMenu";
    if (requestGameViewCreateMenu_) {
        ImGui::OpenPopup(popupName);
        requestGameViewCreateMenu_ = false;
    }

    ImGui::SetNextWindowPos(
        ImVec2(gameViewCreateMenuScreenPos_.x, gameViewCreateMenuScreenPos_.y),
        ImGuiCond_Appearing);

    if (ImGui::BeginPopup(popupName)) {
        hierarchyWindow_.DrawCreateContextMenu(sceneManager_->GetCurrentScene(), true);
        if (playFromPositionCallback_) {
            ImGui::Separator();
            if (ImGui::MenuItem("プレイヤーをここから開始")) {
                Vector3 position = CalculateGameViewCreatePosition(nullptr);
                position.y += 1.0f;
                playFromPositionCallback_(position, "GameView指定位置");
            }
            if (selectedObject_ && ImGui::MenuItem("選択Object付近から開始")) {
                Vector3 position = selectedObject_->GetWorldPosition();
                position.y += 1.0f;
                position.z += 2.0f;
                playFromPositionCallback_(position, "選択Object: " + selectedObject_->GetName());
            }
            if (ImGui::BeginMenu("チェックポイントから開始")) {
                bool found = false;
                for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
                    if (!object || object->GetEventType() != EventType::Checkpoint) continue;
                    found = true;
                    Vector3 position = object->GetWorldPosition();
                    position.y += 1.0f;
                    if (ImGui::MenuItem(object->GetName().c_str())) {
                        playFromPositionCallback_(position, "Checkpoint: " + object->GetName());
                    }
                }
                if (!found) ImGui::TextDisabled("Checkpoint EventのObjectがありません");
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }
#endif
}

Vector3 DebugEditor::CalculateGameViewCreatePosition(const Object3d* object) {
    PlacementResult placement = CalculateGameViewPlacement(object, gameViewCreateMenuMousePos_, false);
    if (placement.found) return placement.position;
    return { 0.0f, GetCreateYOffsetForObject(object), 0.0f };
}

DebugEditor::PlacementResult DebugEditor::CalculateGameViewPlacement(const Object3d* object, const Vector2& mousePos, bool useGridSnap) {
    PlacementResult result;
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        result.position = { 0.0f, GetCreateYOffsetForObject(object), 0.0f };
        result.found = true;
        return result;
    }

    Math math;
    Ray ray = ScreenPointToRay(mousePos);
    if (IsNearlyZero(ray.diff)) {
        return result;
    }

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    RayResult best;
    best.isHit = false;
    best.distance = 1e5f;

    for (auto& obj : objects) {
        if (!obj) continue;
        if (obj.get() == object) continue;
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;
        if (!obj->GetIsVisible()) continue;

        AABB aabb = obj->GetAABB();
        RayResult tmp;
        if (math.IntersectRayAABB(ray, aabb.min, aabb.max, &tmp)) {
            if (tmp.distance < best.distance) {
                best = tmp;
            }
        }
    }

    float surfaceOffset = 0.0f;

    if (best.isHit) {
        result.normal = NormalizeOrUp(best.normal);
        result.contactPosition = best.point;
        surfaceOffset = GetSurfaceOffset(object, result.normal);
        result.position = result.contactPosition + result.normal * surfaceOffset;
        result.found = true;
        result.hasSurface = true;
    }
    else if (IntersectRayPlane(ray, result.contactPosition)) {
        result.normal = { 0.0f, 1.0f, 0.0f };
        surfaceOffset = GetSurfaceOffset(object, result.normal);
        result.position = result.contactPosition + result.normal * surfaceOffset;
        result.found = true;
        result.hasSurface = true;
    }
    else {
        result.normal = { 0.0f, 1.0f, 0.0f };
        result.position = ray.origin + math.Normalize(ray.diff) * 10.0f;
        result.contactPosition = result.position;
        result.found = true;
        result.hasSurface = false;
    }

    ApplyGridSnap(result.position, result.normal, result.hasSurface, useGridSnap && isGridSnapEnabled_, snapValue_);
    result.contactPosition = result.hasSurface ? result.position - result.normal * surfaceOffset : result.position;
    return result;
}

void DebugEditor::ApplyGameViewPlacement(Object3d* object, const PlacementResult& placement, bool alignToSurface) {
    if (!object || !placement.found) return;

    object->SetTranslate(placement.position);
    if (alignToSurface && placement.hasSurface && object->IsDecal()) {
        DecalSystem::AlignToSurface(
            *object, placement.contactPosition, placement.normal, 0.0f,
            object->GetDecalSettings().depthOffset);
        if (object == previewObject_.get()) {
            previewPlacementContactPosition_ = placement.contactPosition;
            hasPreviewPlacementContact_ = placement.found;
        }
        return;
    }

    if (alignToSurface && placement.hasSurface) {
        object->SetRotation(GetSurfaceAlignedRotation(object->GetTransform()->rotate, placement.normal));
    }
    if (object == previewObject_.get()) {
        previewPlacementContactPosition_ = placement.contactPosition;
        hasPreviewPlacementContact_ = placement.found;
    }
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();
    if (object->GetMeshRenderer()) {
        object->GetMeshRenderer()->Update();
    }
}

void DebugEditor::UpdatePreviewPlacement() {
#ifdef USE_IMGUI
    if (!previewObject_ || !isGameViewHovered_) return;

    PlacementResult placement = CalculateGameViewPlacement(previewObject_.get(), gameViewMousePos_, false);
    ApplyGameViewPlacement(previewObject_.get(), placement, true);
    ApplyPreviewVisual(previewObject_.get());
#endif
}

void DebugEditor::ConfirmPreviewPlacement() {
    if (!previewObject_) return;

    std::string createdName = previewObject_->GetName();
    RestorePreviewVisual(previewObject_.get());
    previewObject_->UpdateLocalMatrix();
    previewObject_->UpdateWorldMatrix();

    std::vector<std::unique_ptr<Object3d>> createdObjects;
    createdObjects.reserve(previewChildObjects_.size() + 1);
    createdObjects.push_back(std::move(previewObject_));
    for (auto& child : previewChildObjects_) {
        if (child) {
            RestorePreviewVisual(child.get());
            child->UpdateLocalMatrix();
            child->UpdateWorldMatrix();
            createdObjects.push_back(std::move(child));
        }
    }
    previewChildObjects_.clear();

    AddEditorObjects(std::move(createdObjects), previewCreateCommandLabel_);
    DebugConsole::GetInstance()->AddLog("Create: " + createdName);
    previewCreateCommandLabel_ = "Place Preview Object";
    previewVisualStates_.clear();
}

void DebugEditor::CancelPreviewPlacement() {
    if (!previewObject_) return;

    DebugConsole::GetInstance()->AddLog("Cancel Preview Create: " + previewObject_->GetName());
    previewChildObjects_.clear();
    previewObject_ = nullptr;
    previewVisualStates_.clear();
    previewCreateCommandLabel_ = "Place Preview Object";
}

void DebugEditor::StartPresetBrush(const std::string& presetName) {
#ifdef USE_IMGUI
    if (presetName.empty() || !sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    if (sceneManager_->IsPlaying() || isPathEditMode_) return;

    if (previewObject_) {
        CancelPreviewPlacement();
    }

    brushPresetName_ = presetName;
    isPresetBrushMode_ = true;
    hasLastBrushStamp_ = false;
    RebuildPresetBrushPreview();
    if (!isPresetBrushMode_) return;

    DebugConsole::GetInstance()->AddLog("Brush Start: " + brushPresetName_);
#else
    (void)presetName;
#endif
}

void DebugEditor::StopPresetBrush() {
    if (!isPresetBrushMode_) return;

    DebugConsole::GetInstance()->AddLog("Brush Stop: " + brushPresetName_);
    isPresetBrushMode_ = false;
    brushPresetName_.clear();
    brushPreviewObjects_.clear();
    hasLastBrushStamp_ = false;
}

void DebugEditor::RebuildPresetBrushPreview() {
    brushPreviewObjects_.clear();
    if (!sceneManager_ || !sceneManager_->GetCurrentScene() || brushPresetName_.empty()) return;

    BaseScene* scene = sceneManager_->GetCurrentScene();
    Object3dCommon* common = scene->GetObject3dCommon();
    if (!common) return;

    brushPreviewObjects_ = PresetManager::GetInstance()->CreateObjectsFromPreset(brushPresetName_, common);
    if (brushPreviewObjects_.empty()) {
        StopPresetBrush();
        return;
    }

    AssignPresetInstanceNames(scene, brushPresetName_, brushPreviewObjects_);
    for (auto& object : brushPreviewObjects_) {
        ApplyBrushPreviewVisual(object.get());
    }
}

void DebugEditor::UpdatePresetBrush() {
#ifdef USE_IMGUI
    if (!isPresetBrushMode_) return;
    if (!sceneManager_ || !sceneManager_->GetCurrentScene() || sceneManager_->IsPlaying()) {
        StopPresetBrush();
        return;
    }

    InputManager* input = InputManager::GetInstance();
    if (input && input->IsKeyTriggered(DIK_E)) {
        StopPresetBrush();
        return;
    }
    if (isGameViewHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        StopPresetBrush();
        return;
    }

    if (brushPreviewObjects_.empty()) {
        RebuildPresetBrushPreview();
    }
    if (brushPreviewObjects_.empty()) return;

    Object3d* root = brushPreviewObjects_.front().get();
    PlacementResult placement = CalculateGameViewPlacement(root, gameViewMousePos_, true);
    ApplyGameViewPlacement(root, placement, true);
    for (auto& object : brushPreviewObjects_) {
        ApplyBrushPreviewVisual(object.get());
    }

    if (!isGameViewHovered_ || ImGuizmo::IsOver() || ImGuizmo::IsUsing()) {
        return;
    }

    bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool dragging = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (!clicked && !dragging) return;

    Vector3 stampPosition = root->GetWorldPosition();
    float spacing = std::max(brushSpacing_, 0.0f);
    bool farEnough = !hasLastBrushStamp_ || Distance(stampPosition, lastBrushStampPosition_) >= spacing;
    if (clicked || farEnough) {
        StampPresetBrush();
    }
#endif
}

void DebugEditor::StampPresetBrush() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene() || brushPresetName_.empty() || brushPreviewObjects_.empty()) return;

    BaseScene* scene = sceneManager_->GetCurrentScene();
    Object3dCommon* common = scene->GetObject3dCommon();
    if (!common) return;

    auto objects = PresetManager::GetInstance()->CreateObjectsFromPreset(brushPresetName_, common);
    if (objects.empty()) return;

    AssignPresetInstanceNames(scene, brushPresetName_, objects);
    Object3d* root = objects.front().get();
    Object3d* previewRoot = brushPreviewObjects_.front().get();
    Vector3 stampPosition = previewRoot->GetWorldPosition();

    root->SetTranslate(previewRoot->GetTranslate());
    root->SetRotation(previewRoot->GetRotation());
    root->UpdateLocalMatrix();
    root->UpdateWorldMatrix();

    AddEditorObjects(std::move(objects), "Brush Preset " + brushPresetName_);
    lastBrushStampPosition_ = stampPosition;
    hasLastBrushStamp_ = true;
}

void DebugEditor::DrawPresetBrushOverlay() {
#ifdef USE_IMGUI
    if (!isPresetBrushMode_) return;

    ImGui::SetNextWindowPos(ImVec2(gameViewOffset_.x + 12.0f, gameViewOffset_.y + 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("プリセットブラシ", nullptr, flags)) {
        ImGui::Text("対象: %s", brushPresetName_.c_str());
        ImGui::SliderFloat("配置間隔", &brushSpacing_, 0.0f, 10.0f, "%.2f");
        ImGui::TextDisabled("左ドラッグ: 配置 / 右クリック or E: 終了");
        if (ImGui::Button("ブラシ終了")) {
            StopPresetBrush();
        }
    }
    ImGui::End();
#endif
}

void DebugEditor::ApplyBrushPreviewVisual(Object3d* object) {
    if (!object) return;

    if (!object->GetModel()) {
        object->SetModel("Primitives/cube");
    }
    object->SetClassName("Model");
    object->SetMaterialType(0);
    object->SetBlendMode(BlendMode::kNormal);
    Vector4 color = object->GetColor();
    object->SetColor({ color.x, color.y, color.z, 0.45f });
    object->SetEmissive(std::max(object->GetEmissive(), 1.4f));
    ApplyEditorPreviewLightOverride(object);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();
    if (object->GetMeshRenderer()) {
        object->GetMeshRenderer()->Update();
    }
}

void DebugEditor::ApplyPreviewVisual(Object3d* object) {
    if (!object) return;

    if (!previewObjectUsesFallbackModel_ && !object->GetModel()) {
        object->SetModel("Primitives/cube");
        object->SetColliderConfig(previewObjectOriginalColliderConfig_);
        previewObjectUsesFallbackModel_ = true;
    }

    if (previewVisualStates_.find(object) == previewVisualStates_.end()) {
        PreviewVisualState state;
        state.className = object->GetClassName();
        state.color = object->GetColor();
        state.blendMode = object->GetBlendMode();
        state.materialType = object->GetMaterialType();
        state.emissive = object->GetEmissive();
        if (auto* material = object->GetMaterialData()) {
            state.selectedLighting = material->selectedLighting;
            state.enableLighting = material->enableLighting;
        }
        previewVisualStates_[object] = state;
    }

    object->SetMaterialType(0);
    object->SetBlendMode(BlendMode::kNormal);
    object->SetEmissive(1.25f);
    object->SetColor({ 0.45f, 1.0f, 0.68f, 0.46f });
    ApplyEditorPreviewLightOverride(object);
}

void DebugEditor::RestorePreviewVisual(Object3d* object) {
    if (!object) return;

    if (previewObjectUsesFallbackModel_) {
        if (!previewObjectOriginalModelName_.empty()) {
            object->SetModel(previewObjectOriginalModelName_);
        }
        else {
            object->SetModel(previewObjectOriginalModel_);
        }
        object->SetColliderConfig(previewObjectOriginalColliderConfig_);
        previewObjectUsesFallbackModel_ = false;
    }

    const auto stateIt = previewVisualStates_.find(object);
    if (stateIt != previewVisualStates_.end()) {
        const PreviewVisualState& state = stateIt->second;
        object->SetClassName(state.className);
        object->SetMaterialType(state.materialType);
        object->SetBlendMode(state.blendMode);
        if (auto* material = object->GetMaterialData()) {
            material->selectedLighting = state.selectedLighting;
            material->enableLighting = state.enableLighting;
        }
        object->SetEmissive(state.emissive);
        object->SetColor(state.color);
        return;
    }

    object->SetClassName(previewObjectOriginalClassName_);
    object->SetMaterialType(previewObjectOriginalMaterialType_);
    object->SetBlendMode(previewObjectOriginalBlendMode_);
    if (auto* material = object->GetMaterialData()) {
        material->selectedLighting = previewObjectOriginalSelectedLighting_;
        material->enableLighting = previewObjectOriginalEnableLighting_;
    }
    object->SetEmissive(previewObjectOriginalEmissive_);
    object->SetColor(previewObjectOriginalColor_);
}

void DebugEditor::DrawPreviewWire(ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit) {
    if (!previewObject_ || !commandList || instanceCount >= maxDrawLimit) return;

    AABB aabb = previewObject_->GetAABB();
    if (!IsValidAabb(aabb)) {
        aabb = previewObject_->GetModelWorldAABB();
    }
    if (!IsValidAabb(aabb)) return;

    Vector3 size = aabb.max - aabb.min;
    Vector3 center = (aabb.max + aabb.min) * 0.5f;

    Math math;
    Matrix4x4 world =
        math.Multiply(
            math.MakeScaleMatrix(size),
            math.MakeTranslateMatrix(center));

    primitiveDrawer_.DrawWireCube(commandList, world, { 0.2f, 1.0f, 0.45f, 1.0f }, instanceCount);
    instanceCount++;
}

void DebugEditor::DrawPreviewMarker() {
#ifdef USE_IMGUI
    if (!previewObject_) return;

    Vector3 markerWorld = hasPreviewPlacementContact_
        ? previewPlacementContactPosition_
        : previewObject_->GetWorldPosition();
    Vector3 screen = WorldToScreen(markerWorld);
    if (screen.z < 0.0f) return;

    Vector3 objectScreen = WorldToScreen(previewObject_->GetWorldPosition());
    bool canDrawObjectLine = objectScreen.z >= 0.0f;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImVec2 center(screen.x, screen.y);
    ImU32 outerColor = IM_COL32(40, 255, 120, 210);
    ImU32 innerColor = IM_COL32(255, 255, 255, 220);
    constexpr float kRadius = 13.0f;

    if (canDrawObjectLine) {
        ImVec2 objectCenter(objectScreen.x, objectScreen.y);
        float dx = objectCenter.x - center.x;
        float dy = objectCenter.y - center.y;
        if ((dx * dx + dy * dy) > 36.0f) {
            drawList->AddLine(center, objectCenter, IM_COL32(40, 255, 120, 120), 1.5f);
            drawList->AddCircle(objectCenter, 5.0f, IM_COL32(255, 255, 255, 150), 16, 1.5f);
        }
    }

    drawList->AddCircleFilled(center, kRadius, IM_COL32(40, 255, 120, 35), 32);
    drawList->AddCircle(center, kRadius, outerColor, 32, 2.5f);
    drawList->AddLine(ImVec2(center.x - kRadius - 5.0f, center.y), ImVec2(center.x - 4.0f, center.y), outerColor, 2.0f);
    drawList->AddLine(ImVec2(center.x + 4.0f, center.y), ImVec2(center.x + kRadius + 5.0f, center.y), outerColor, 2.0f);
    drawList->AddLine(ImVec2(center.x, center.y - kRadius - 5.0f), ImVec2(center.x, center.y - 4.0f), outerColor, 2.0f);
    drawList->AddLine(ImVec2(center.x, center.y + 4.0f), ImVec2(center.x, center.y + kRadius + 5.0f), outerColor, 2.0f);
    drawList->AddCircleFilled(center, 3.0f, innerColor);
    drawList->AddText(ImVec2(center.x + 16.0f, center.y - 8.0f), outerColor, "Place");
#endif
}

