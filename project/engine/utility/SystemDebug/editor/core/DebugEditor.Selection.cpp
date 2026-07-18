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

void DebugEditor::SetSelectedObject(Object3d* obj) {
    selectedObjects_.clear();
    if (obj && IsObjectInCurrentScene(obj)) {
        selectedObject_ = obj;
        selectedObjects_.push_back(obj);
    } else {
        selectedObject_ = nullptr;
    }
}

void DebugEditor::SyncObjectSelectionToInspector() {
    EditorManager* editorManager = EditorManager::GetInstance();
    if ((ghostRecorder_ && editorManager->GetSelectedObject() == ghostRecorder_) ||
        (ghostDirector_ && editorManager->GetSelectedObject() == ghostDirector_)) {
        return;
    }

    if (selectedObject_) {
        editorManager->SetSelectedObject(this);
    } else {
        editorManager->ClearSelection();
    }
}

bool DebugEditor::IsObjectSelected(const Object3d* object) const {
    if (!object || !IsObjectInCurrentScene(object)) return false;

    for (Object3d* selected : selectedObjects_) {
        if (selected == object) return true;
    }
    return false;
}

int DebugEditor::GetSelectionOverlayMode() const {
    return static_cast<int>(selectionOverlayMode_);
}

void DebugEditor::SetSelectionOverlayMode(int mode) {
    if (mode < static_cast<int>(SelectionOverlayMode::Compact) ||
        mode > static_cast<int>(SelectionOverlayMode::Hidden)) {
        return;
    }
    selectionOverlayMode_ = static_cast<SelectionOverlayMode>(mode);
}

void DebugEditor::ClearObjectSelection() {
    selectedObject_ = nullptr;
    selectedObjects_.clear();
    groupTransformStartStates_.clear();
}

void DebugEditor::AddSelectedObject(Object3d* object) {
    if (!object || !IsObjectInCurrentScene(object)) return;

    for (Object3d* selected : selectedObjects_) {
        if (selected == object) {
            selectedObject_ = object;
            return;
        }
    }

    selectedObjects_.push_back(object);
    selectedObject_ = object;
}

void DebugEditor::ToggleSelectedObject(Object3d* object) {
    if (!object || !IsObjectInCurrentScene(object)) return;

    for (auto it = selectedObjects_.begin(); it != selectedObjects_.end(); ++it) {
        if (*it == object) {
            selectedObjects_.erase(it);
            selectedObject_ = selectedObjects_.empty() ? nullptr : selectedObjects_.back();
            return;
        }
    }

    AddSelectedObject(object);
}

void DebugEditor::PruneInvalidSelectedObjects() {
    std::vector<Object3d*> validObjects;
    validObjects.reserve(selectedObjects_.size());

    for (Object3d* object : selectedObjects_) {
        if (!object || !IsObjectInCurrentScene(object)) continue;

        bool alreadyAdded = false;
        for (Object3d* added : validObjects) {
            if (added == object) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) {
            validObjects.push_back(object);
        }
    }

    selectedObjects_ = validObjects;
    if (!selectedObject_ || !IsObjectInCurrentScene(selectedObject_)) {
        selectedObject_ = selectedObjects_.empty() ? nullptr : selectedObjects_.back();
    }
}

Object3d* DebugEditor::PickObjectAtGameViewPos(const Vector2& mousePos) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;

    Math math;
    Ray ray = ScreenPointToRay(mousePos);
    RayResult best;
    best.isHit = false;
    best.distance = 1e5f;
    Object3d* hit = nullptr;

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (auto& obj : objects) {
        if (!obj) continue;
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;
        if (!obj->GetIsVisible() || obj->GetIsLocked()) continue;

        Matrix4x4 worldMatrix = obj->GetWorldMatrix();
        Vector3 worldPosition = { worldMatrix.m[3][0], worldMatrix.m[3][1], worldMatrix.m[3][2] };
        Vector3 worldScale = obj->GetTransform()->scale;
        RayResult tmp;

        if (math.IntersectRayAABB(ray, worldPosition - worldScale, worldPosition + worldScale, &tmp)) {
            if (tmp.distance < best.distance) {
                best = tmp;
                hit = obj.get();
            }
        }
    }

    return hit;
}

Vector3 DebugEditor::GetObjectWorldPositionForSelection(Object3d* object) {
    if (!object) return { 0.0f, 0.0f, 0.0f };

    AABB modelAabb = object->GetModelWorldAABB();
    if (IsValidAabb(modelAabb)) {
        return {
            (modelAabb.min.x + modelAabb.max.x) * 0.5f,
            (modelAabb.min.y + modelAabb.max.y) * 0.5f,
            (modelAabb.min.z + modelAabb.max.z) * 0.5f
        };
    }

    Matrix4x4 worldMatrix = object->GetWorldMatrix();
    return { worldMatrix.m[3][0], worldMatrix.m[3][1], worldMatrix.m[3][2] };
}

void DebugEditor::SelectObjectsInGameViewRect(const Vector2& start, const Vector2& end, bool additive) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    const float localLeft = start.x < end.x ? start.x : end.x;
    const float localRight = start.x < end.x ? end.x : start.x;
    const float localTop = start.y < end.y ? start.y : end.y;
    const float localBottom = start.y < end.y ? end.y : start.y;

    const float screenLeft = gameViewOffset_.x + localLeft;
    const float screenRight = gameViewOffset_.x + localRight;
    const float screenTop = gameViewOffset_.y + localTop;
    const float screenBottom = gameViewOffset_.y + localBottom;

    if (!additive) {
        ClearObjectSelection();
    }

    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (auto& obj : objects) {
        if (!obj) continue;
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;
        if (!obj->GetIsVisible() || obj->GetIsLocked()) continue;

        Vector3 screenPosition = WorldToScreen(GetObjectWorldPositionForSelection(obj.get()));
        if (screenPosition.z < 0.0f) continue;

        if (screenPosition.x >= screenLeft && screenPosition.x <= screenRight &&
            screenPosition.y >= screenTop && screenPosition.y <= screenBottom) {
            AddSelectedObject(obj.get());
        }
    }

    if (selectedObject_ || !additive) {
        SyncObjectSelectionToInspector();
    }
}

void DebugEditor::DrawSelectionRectangleOverlay() {
#ifdef USE_IMGUI
    if (!isSelectionRectDragging_ || !isSelectionRectReady_) return;

    const float minX = selectionRectStart_.x < selectionRectEnd_.x ? selectionRectStart_.x : selectionRectEnd_.x;
    const float maxX = selectionRectStart_.x < selectionRectEnd_.x ? selectionRectEnd_.x : selectionRectStart_.x;
    const float minY = selectionRectStart_.y < selectionRectEnd_.y ? selectionRectStart_.y : selectionRectEnd_.y;
    const float maxY = selectionRectStart_.y < selectionRectEnd_.y ? selectionRectEnd_.y : selectionRectStart_.y;

    ImVec2 rectMin = ImVec2(gameViewOffset_.x + minX, gameViewOffset_.y + minY);
    ImVec2 rectMax = ImVec2(gameViewOffset_.x + maxX, gameViewOffset_.y + maxY);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    drawList->AddRectFilled(rectMin, rectMax, IM_COL32(80, 160, 255, 45));
    drawList->AddRect(rectMin, rectMax, IM_COL32(80, 190, 255, 230), 0.0f, 0, 2.0f);
#endif
}

void DebugEditor::DrawSelectedObjectBoundsOverlay() {
#ifdef USE_IMGUI
    PruneInvalidSelectedObjects();
    if (selectedObjects_.empty() || selectionOverlayMode_ == SelectionOverlayMode::Hidden) return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const std::size_t selectedCount = selectedObjects_.size();
    const bool showTemporaryDetails =
        selectionOverlayMode_ == SelectionOverlayMode::Compact && ImGui::GetIO().KeyAlt;

    for (Object3d* object : selectedObjects_) {
        if (!object || !IsObjectInCurrentScene(object)) continue;

        AABB aabb = object->GetModelWorldAABB();
        Vector3 minPos;
        Vector3 maxPos;
        if (IsValidAabb(aabb)) {
            minPos = aabb.min;
            maxPos = aabb.max;
        } else {
            Vector3 center = GetObjectWorldPositionForSelection(object);
            Vector3 scale = object->GetTransform()->scale;
            minPos = center - scale;
            maxPos = center + scale;
        }

        const bool isPrimary = object == selectedObject_;
        const bool showBounds = isPrimary ||
            selectionOverlayMode_ == SelectionOverlayMode::Detailed ||
            showTemporaryDetails;

        if (!showBounds) {
            const Vector3 center = {
                (minPos.x + maxPos.x) * 0.5f,
                (minPos.y + maxPos.y) * 0.5f,
                (minPos.z + maxPos.z) * 0.5f
            };
            const Vector3 screen = WorldToScreen(center);
            if (screen.z < 0.0f) continue;

            const ImVec2 markerCenter = ImVec2(screen.x, screen.y);
            constexpr float markerRadius = 4.5f;
            drawList->AddCircleFilled(markerCenter, markerRadius + 1.5f, IM_COL32(10, 24, 36, 170));
            drawList->AddCircle(markerCenter, markerRadius, IM_COL32(80, 210, 255, 235), 0, 1.8f);
            drawList->AddLine(
                ImVec2(markerCenter.x - 2.0f, markerCenter.y),
                ImVec2(markerCenter.x + 2.0f, markerCenter.y),
                IM_COL32(180, 240, 255, 235), 1.2f);
            drawList->AddLine(
                ImVec2(markerCenter.x, markerCenter.y - 2.0f),
                ImVec2(markerCenter.x, markerCenter.y + 2.0f),
                IM_COL32(180, 240, 255, 235), 1.2f);
            continue;
        }

        Vector3 corners[8] = {
            { minPos.x, minPos.y, minPos.z },
            { maxPos.x, minPos.y, minPos.z },
            { minPos.x, maxPos.y, minPos.z },
            { maxPos.x, maxPos.y, minPos.z },
            { minPos.x, minPos.y, maxPos.z },
            { maxPos.x, minPos.y, maxPos.z },
            { minPos.x, maxPos.y, maxPos.z },
            { maxPos.x, maxPos.y, maxPos.z },
        };

        bool hasScreenPoint = false;
        ImVec2 rectMin = ImVec2(0.0f, 0.0f);
        ImVec2 rectMax = ImVec2(0.0f, 0.0f);

        for (const Vector3& corner : corners) {
            Vector3 screen = WorldToScreen(corner);
            if (screen.z < 0.0f) continue;

            if (!hasScreenPoint) {
                rectMin = ImVec2(screen.x, screen.y);
                rectMax = rectMin;
                hasScreenPoint = true;
            } else {
                if (screen.x < rectMin.x) rectMin.x = screen.x;
                if (screen.y < rectMin.y) rectMin.y = screen.y;
                if (screen.x > rectMax.x) rectMax.x = screen.x;
                if (screen.y > rectMax.y) rectMax.y = screen.y;
            }
        }

        if (!hasScreenPoint) continue;

        ImU32 frameColor = isPrimary ? IM_COL32(255, 235, 80, 255) : IM_COL32(80, 210, 255, 235);
        ImU32 fillColor = isPrimary ? IM_COL32(255, 235, 80, 22) : IM_COL32(80, 210, 255, 18);
        const float thickness = isPrimary ? 2.4f : 1.7f;

        drawList->AddRectFilled(rectMin, rectMax, fillColor, 3.0f);
        drawList->AddRect(rectMin, rectMax, frameColor, 3.0f, 0, thickness);

        if (isPrimary) {
            std::string label = object->GetName().empty() ? "Selected" : object->GetName();
            if (selectedCount > 1) {
                label += " (+" + std::to_string(selectedCount - 1) + ")";
            }
            ImVec2 labelPos = ImVec2(rectMin.x, rectMin.y - ImGui::GetFontSize() - 4.0f);
            drawList->AddText(ImVec2(labelPos.x + 1.0f, labelPos.y + 1.0f), IM_COL32(0, 0, 0, 220), label.c_str());
            drawList->AddText(labelPos, IM_COL32(255, 245, 150, 255), label.c_str());
        }
    }
#endif
}

bool DebugEditor::IsObjectInCurrentScene(const Object3d* object) const {
    if (!object || !sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return false;
    }

    const auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    for (const auto& candidate : objects) {
        if (candidate.get() == object) {
            return true;
        }
    }
    return false;
}

void DebugEditor::ClearInvalidSelectedObject() {
    const bool editorWasSelected = EditorManager::GetInstance()->GetSelectedObject() == this;
    PruneInvalidSelectedObjects();
    if (selectedObject_) {
        return;
    }

    ClearObjectSelection();
    isDraggingTransform_ = false;
    hasInspectorEditStart_ = false;
    inspectorEditStartStates_.clear();
    tempObjectStateStart_.clear();
    if (editorWasSelected) {
        EditorManager::GetInstance()->ClearSelection();
    }
}

