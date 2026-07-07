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
        return className == "CinematicCamera" ||
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

void DebugEditor::DropToFloor() {
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    Math math;
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();

    // 1. 現在のワールド座標を取得
    Matrix4x4 wm = selectedObject_->GetWorldMatrix();
    Vector3 currentPos = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };

    // 2. 真下に向かってレイ(光線)を飛ばす
    Ray ray;
    ray.origin = currentPos;
    ray.diff = { 0.0f, -1000.0f, 0.0f }; // 下方向へ1000メートル

    RayResult bestHit;
    bestHit.isHit = false;
    bestHit.distance = 1e5f;

    // 3. めり込み防止のための「足元までのオフセット(高さの半分)」を計算
    float yOffset = 0.0f;
    ColliderType type = selectedObject_->GetColliderType();
    if (type == ColliderType::kAABB || type == ColliderType::kOBB) {
        yOffset = selectedObject_->GetColliderConfig().size.y;
    }
    else if (type == ColliderType::kSphere) {
        yOffset = selectedObject_->GetColliderConfig().size.x; // 球体はXを半径としている想定
    }
    else {
        // コライダーが無い場合はスケールのYを基準にする
        yOffset = selectedObject_->GetTransform()->scale.y;
    }

    // 4. 真下にある足場（他のオブジェクト）を探す
    for (auto& obj : objects) {
        if (obj.get() == selectedObject_) continue; // 自分自身は無視
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;
        if (!obj->GetIsVisible()) continue; // 非表示オブジェクトはすり抜ける

        Matrix4x4 targetWm = obj->GetWorldMatrix();
        Vector3 wp = { targetWm.m[3][0], targetWm.m[3][1], targetWm.m[3][2] };
        Vector3 ws = obj->GetTransform()->scale;

        RayResult tmp;
        // AABBで簡易的に衝突判定
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < bestHit.distance) {
                bestHit = tmp;
            }
        }
    }

    // 5. Undo(Ctrl+Z) 用に移動前の状態を保存
    nlohmann::json beforeState = CaptureObjectState(selectedObject_);

    // 6. 実際の移動処理
    if (bestHit.isHit) {
        // 真下にオブジェクトがあった場合、その表面に乗る
        selectedObject_->GetTransform()->translate.y = bestHit.point.y + yOffset;
        DebugConsole::GetInstance()->AddLog("Dropped to Object!");
    }
    else {
        // 真下に何もない場合は、Y=0 の床に乗る
        selectedObject_->GetTransform()->translate.y = yOffset;
        DebugConsole::GetInstance()->AddLog("Dropped to Floor (Y=0)!");
    }

    selectedObject_->UpdateLocalMatrix();
    selectedObject_->UpdateWorldMatrix();
    RegisterObjectEdited(selectedObject_, beforeState, "Drop To Floor");
}
// 指定したモデルをマウス位置(GameView)に配置
// ========================================================================
void DebugEditor::SplitSelectedModelIntoMeshChildren() {
    if (!selectedObject_ || !sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return;
    }

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    Object3dCommon* objectCommon = currentScene->GetObject3dCommon();
    Model* sourceModel = selectedObject_->GetModel();
    const std::string sourceModelName = selectedObject_->GetModelName();

    if (!objectCommon || !sourceModel || sourceModelName.empty()) {
        DebugConsole::GetInstance()->AddLog("Mesh split skipped: selected object has no model.");
        return;
    }

    const uint32_t meshCount = sourceModel->GetMeshCount();
    if (meshCount <= 1) {
        DebugConsole::GetInstance()->AddLog("Mesh split skipped: model has only one mesh.");
        return;
    }

    if (selectedObject_->IsMeshDrawFiltered()) {
        DebugConsole::GetInstance()->AddLog("Mesh split skipped: selected object is already a mesh part.");
        return;
    }

    Object3d* rootObject = selectedObject_;
    const std::string rootName = rootObject->GetName().empty() ? "MeshRoot" : rootObject->GetName();
    const nlohmann::json beforeState = CaptureObjectState(rootObject);

    const std::string saveCategory = rootObject->GetSaveCategory();
    const Vector4 color = rootObject->GetColor();
    const BlendMode blendMode = rootObject->GetBlendMode();
    const int32_t materialType = rootObject->GetMaterialType();
    const float metallic = rootObject->GetMetallic();
    const float roughness = rootObject->GetRoughness();
    const bool enableNormalMap = rootObject->GetEnableNormalMap();
    const std::string normalMapPath = rootObject->GetNormalMapPath();
    const std::string ormMapPath = rootObject->GetOrmMapPath();
    const std::string texturePath = rootObject->GetTexturePath();
    const Vector2 textureTiling = rootObject->GetTextureTiling();
    const bool autoTextureTiling = rootObject->GetAutoTextureTiling();
    const bool enableLighting = rootObject->GetEnableLighting();
    const bool enableEnvMap = rootObject->GetEnableEnvMap();
    const float envIntensity = rootObject->GetEnvIntensity();
    const float emissive = rootObject->GetEmissive();

    bool hasWaterParam = false;
    MeshRenderer::WaterParamForGPU waterParam{};
    if (rootObject->GetMeshRenderer() && rootObject->GetMeshRenderer()->GetWaterParamData()) {
        waterParam = *rootObject->GetMeshRenderer()->GetWaterParamData();
        hasWaterParam = true;
    }

    std::vector<std::unique_ptr<Object3d>> meshChildren;
    std::vector<std::string> reservedNames;
    meshChildren.reserve(meshCount);

    for (uint32_t meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
        auto child = std::make_unique<Object3d>();
        child->Initialize(objectCommon);

        const std::string childBaseName = rootName + "_mesh" + std::to_string(meshIndex);
        const std::string childName = MakeUniquePresetObjectName(currentScene, childBaseName, reservedNames);
        reservedNames.push_back(childName);

        child->SetName(childName);
        child->SetClassName("MeshPart");
        child->SetSaveCategory(saveCategory);
        child->SetModel(sourceModelName);
        child->SetMeshDrawIndex(static_cast<int>(meshIndex));
        child->SetColor(color);
        child->SetBlendMode(blendMode);
        child->SetMaterialType(materialType);
        child->SetMetallic(metallic);
        child->SetRoughness(roughness);
        child->SetEnableNormalMap(enableNormalMap);
        child->SetNormalMap(normalMapPath);
        child->SetOrmMap(ormMapPath);
        child->SetTexture(texturePath);
        child->SetTextureTiling(textureTiling);
        child->SetAutoTextureTiling(autoTextureTiling);
        child->SetEnableLighting(enableLighting);
        child->SetEnableEnvMap(enableEnvMap);
        child->SetEnvIntensity(envIntensity);
        child->SetEmissive(emissive);
        if (hasWaterParam && child->GetMeshRenderer() && child->GetMeshRenderer()->GetWaterParamData()) {
            *child->GetMeshRenderer()->GetWaterParamData() = waterParam;
        }

        child->SetColliderType(ColliderType::kNone);
        child->SetCollisionAttribute(0);
        child->SetCollisionMask(0);
        child->SetStatic(rootObject->IsStatic());
        child->SetTranslate({ 0.0f, 0.0f, 0.0f });
        child->SetRotation({ 0.0f, 0.0f, 0.0f });
        child->SetScale({ 1.0f, 1.0f, 1.0f });
        child->SetParent(rootObject);
        child->UpdateLocalMatrix();
        child->UpdateWorldMatrix();

        meshChildren.push_back(std::move(child));
    }

    rootObject->SetClassName("MeshRoot");
    rootObject->SetModel(nullptr);
    rootObject->SetMeshDrawIndex(-1);
    rootObject->SetColliderType(ColliderType::kNone);
    rootObject->SetCollisionAttribute(0);
    rootObject->SetCollisionMask(0);
    rootObject->UpdateLocalMatrix();
    rootObject->UpdateWorldMatrix();
    RegisterObjectEdited(rootObject, beforeState, "Split Model Root");

    AddEditorObjects(std::move(meshChildren), "Split Model Mesh Parts");
    SetSelectedObject(rootObject);
    EditorManager::GetInstance()->SetSelectedObject(this);
    MarkDirtyForObject(rootObject);

    DebugConsole::GetInstance()->AddLog("Split model into mesh children: " + rootName + " (" + std::to_string(meshCount) + " meshes)");
}

void DebugEditor::InstantiateModelAtCursor(const std::string& modelName) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    // 1. オブジェクト生成＆モデル読み込み
    auto newObj = std::make_unique<Object3d>();
    newObj->Initialize(currentScene->GetObject3dCommon());
    ModelManager::GetInstance()->LoadModel(modelName);
    newObj->SetModel(modelName);
    newObj->SetClassName("Model");
    newObj->SetName(modelName + "_" + std::to_string(currentScene->GetObjects().size()));

    // 2. マウス座標からレイキャスト
    Math math;
    Ray ray = ScreenPointToRay(gameViewMousePos_);
    Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    // 他のオブジェクトの上に乗せられるかチェック
    auto& objects = currentScene->GetObjects();
    RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsVisible()) continue;
        Matrix4x4 wm = obj->GetWorldMatrix();
        Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        Vector3 ws = obj->GetTransform()->scale;
        RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    // 高さとカメラからの距離を考慮して配置場所を決定
    float yOffset = newObj->GetColliderConfig().size.y;
    if (yOffset == 0.0f) yOffset = newObj->GetTransform()->scale.y;

    if (best.isHit) {
        // オブジェクトに当たった場合はその上に乗せる
        finalPos = best.point;
        finalPos.y += yOffset;
        found = true;
    }
    else {
        // 当たらなかった場合、地面（Y=0）との交点を計算
        if (IntersectRayPlane(ray, finalPos)) {
            // カメラから交点までの「距離」を計算
            float dx = finalPos.x - ray.origin.x;
            float dy = finalPos.y - ray.origin.y;
            float dz = finalPos.z - ray.origin.z;
            float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            // 距離が遠すぎる（20m以上先）なら、強制的にカメラの前方10mの空中に配置
            if (distance > 20.0f) {
                finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            }
            else {
                finalPos.y = yOffset; // 近ければ地面に乗せる
            }
            found = true;
        }
        else {
            // 空を向いていた場合は、カメラの前方10mに置く
            finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            found = true;
        }
    }

    // 座標の確定とスナップ適用
    if (found) {
        if (isGridSnapEnabled_) {
            finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
            finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
        }
        newObj->GetTransform()->translate = finalPos;
    }

    newObj->UpdateLocalMatrix();
    newObj->UpdateWorldMatrix();

    // 4. シーンに追加して、即座に選択状態にする
    AddEditorObject(std::move(newObj), "Drop 3D Model");

    DebugConsole::GetInstance()->AddLog("Dropped 3D Model: " + modelName);
}

void DebugEditor::InstantiatePresetAtCursor(const std::string& presetName) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    Object3dCommon* common = currentScene->GetObject3dCommon();
    if (!common) return;

    auto presetObjects = PresetManager::GetInstance()->CreateObjectsFromPreset(presetName, common);
    if (presetObjects.empty()) return;

    AssignPresetInstanceNames(currentScene, presetName, presetObjects);
    Object3d* rootObject = presetObjects.front().get();

    Math math;
    Ray ray = ScreenPointToRay(gameViewMousePos_);
    Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    auto& objects = currentScene->GetObjects();
    RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsVisible()) continue;
        Matrix4x4 wm = obj->GetWorldMatrix();
        Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        Vector3 ws = obj->GetTransform()->scale;
        RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    float yOffset = rootObject->GetColliderConfig().size.y;
    if (yOffset == 0.0f) yOffset = rootObject->GetTransform()->scale.y;

    if (best.isHit) {
        finalPos = best.point;
        finalPos.y += yOffset;
        found = true;
    }
    else {
        if (IntersectRayPlane(ray, finalPos)) {
            float dx = finalPos.x - ray.origin.x;
            float dy = finalPos.y - ray.origin.y;
            float dz = finalPos.z - ray.origin.z;
            float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (distance > 20.0f) {
                finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            } else {
                finalPos.y = yOffset;
            }
            found = true;
        } else {
            finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            found = true;
        }
    }

    if (found && isGridSnapEnabled_) {
        finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
        finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
    }
    rootObject->GetTransform()->translate = finalPos;
    rootObject->UpdateWorldMatrix();

    AddEditorObjects(std::move(presetObjects), "Drop Preset");

    DebugConsole::GetInstance()->AddLog("Dropped Preset: " + presetName);
}

void DebugEditor::InstantiatePrefabAtCursor(const std::string& prefabName) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    Object3dCommon* common = currentScene->GetObject3dCommon();
    if (!common) return;

    auto prefabObjects = PresetManager::GetInstance()->CreateObjectsFromPrefab(prefabName, common);
    if (prefabObjects.empty()) return;

    AssignPresetInstanceNames(currentScene, prefabName, prefabObjects);
    Object3d* rootObject = prefabObjects.front().get();

    Math math;
    Ray ray = ScreenPointToRay(gameViewMousePos_);
    Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    auto& objects = currentScene->GetObjects();
    RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsVisible()) continue;
        Matrix4x4 wm = obj->GetWorldMatrix();
        Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        Vector3 ws = obj->GetTransform()->scale;
        RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    float yOffset = rootObject->GetColliderConfig().size.y;
    if (yOffset == 0.0f) yOffset = rootObject->GetTransform()->scale.y;

    if (best.isHit) {
        finalPos = best.point;
        finalPos.y += yOffset;
        found = true;
    }
    else {
        if (IntersectRayPlane(ray, finalPos)) {
            float dx = finalPos.x - ray.origin.x;
            float dy = finalPos.y - ray.origin.y;
            float dz = finalPos.z - ray.origin.z;
            float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (distance > 20.0f) {
                finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            } else {
                finalPos.y = yOffset;
            }
            found = true;
        } else {
            finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            found = true;
        }
    }

    if (found && isGridSnapEnabled_) {
        finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
        finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
    }
    rootObject->GetTransform()->translate = finalPos;
    rootObject->UpdateWorldMatrix();

    AddEditorObjects(std::move(prefabObjects), "Drop Prefab");

    DebugConsole::GetInstance()->AddLog("Dropped Prefab: " + prefabName);
}


void DebugEditor::InstantiateParticleAtCursor(const std::string& particleName) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;
    BaseScene* currentScene = sceneManager_->GetCurrentScene();

    // 1. オブジェクト生成とパーティクル設定
    auto newObj = std::make_unique<Object3d>();
    newObj->Initialize(currentScene->GetObject3dCommon());
    
    newObj->SetModel("Stages/block"); 
    newObj->SetName("VFX_" + particleName + "_" + std::to_string(currentScene->GetObjects().size())); 
    newObj->SetClassName("GPUParticle");
    newObj->SetGPUParticleName(particleName);
    newObj->SetColor({1.0f, 0.5f, 0.0f, 0.5f});
    newObj->SetBlendMode(BlendMode::kNormal);

    // 2. マウス座標からのレイキャスト（配置場所の決定）
    Math math;
    Ray ray = ScreenPointToRay(gameViewMousePos_);
    Vector3 finalPos = { 0, 0, 0 };
    bool found = false;

    auto& objects = currentScene->GetObjects();
    RayResult best; best.isHit = false; best.distance = 1e5f;
    for (auto& obj : objects) {
        if (obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsVisible()) continue;
        Matrix4x4 wm = obj->GetWorldMatrix();
        Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
        Vector3 ws = obj->GetTransform()->scale;
        RayResult tmp;
        if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
            if (tmp.distance < best.distance) best = tmp;
        }
    }

    float yOffset = newObj->GetColliderConfig().size.y;
    if (yOffset == 0.0f) yOffset = newObj->GetTransform()->scale.y;

    if (best.isHit) {
        finalPos = best.point;
        finalPos.y += yOffset;
        found = true;
    }
    else {
        if (IntersectRayPlane(ray, finalPos)) {
            float dx = finalPos.x - ray.origin.x;
            float dy = finalPos.y - ray.origin.y;
            float dz = finalPos.z - ray.origin.z;
            float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (distance > 20.0f) {
                finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            } else {
                finalPos.y = yOffset;
            }
            found = true;
        } else {
            finalPos = ray.origin + math.Normalize(ray.diff) * 10.0f;
            found = true;
        }
    }

    // 3. 座標確定とスナップ
    if (found && isGridSnapEnabled_) {
        finalPos.x = std::round(finalPos.x / snapValue_) * snapValue_;
        finalPos.z = std::round(finalPos.z / snapValue_) * snapValue_;
    }
    newObj->GetTransform()->translate = finalPos;
    newObj->UpdateWorldMatrix();

    // 4. シーンに追加
    AddEditorObject(std::move(newObj), "Drop Particle");

    DebugConsole::GetInstance()->AddLog("Dropped Particle: " + particleName);

}
