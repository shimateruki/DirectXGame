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
void DebugEditor::Initialize(SceneManager* sceneManager, DirectXCommon* dxCommon) {
    sceneManager_ = sceneManager;
    dxCommon_ = dxCommon;
    selectedObject_ = nullptr;
    lastUpdatedScene_ = nullptr;
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
    textSpriteGenerator_.Initialize(sceneManager, this);
    text3DGenerator_.Initialize(sceneManager, this);
    modelOptimizerWindow_.Initialize(this);
    assetAuditWindow_.Initialize(this);
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
        selectedObject_ = nullptr;
        previewObject_ = nullptr;
        previewChildObjects_.clear();
        brushPreviewObjects_.clear();
        isPresetBrushMode_ = false;
        brushPresetName_.clear();
        hasLastBrushStamp_ = false;
        previewCreateCommandLabel_ = "Place Preview Object";
        lastUpdatedScene_ = currentScene;
        undoStack_.clear();
        redoStack_.clear();
        ClearDirty(SaveMode::All);
        hasInspectorEditStart_ = false;
        inspectorEditTarget_ = nullptr;
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
            selectedObject_ = obj;
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
            if (input->IsKeyTriggered(DIK_F) && selectedObject_) {
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
        if (!isPathEditMode_ && isGameViewHovered_ && !isCameraEditorSelected && !ImGuizmo::IsOver()) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                Ray ray = ScreenPointToRay(gameViewMousePos_);
                auto& objects = currentScene->GetObjects();
                RayResult best; best.isHit = false; best.distance = 1e5f; Object3d* hit = nullptr;

                for (auto& obj : objects) {
                    if (obj->GetName() == "Cursor" || obj->GetName() == "Line") continue;
                    if (!obj->GetIsVisible() || obj->GetIsLocked()) continue;

                    Matrix4x4 wm = obj->GetWorldMatrix();
                    Vector3 wp = { wm.m[3][0], wm.m[3][1], wm.m[3][2] };
                    Vector3 ws = obj->GetTransform()->scale;
                    RayResult tmp;

                    if (math.IntersectRayAABB(ray, wp - ws, wp + ws, &tmp)) {
                        if (tmp.distance < best.distance) { best = tmp; hit = obj.get(); }
                    }
                }

                if (hit) {
                    selectedObject_ = hit;
                    EditorManager::GetInstance()->SetSelectedObject(this);
                }
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
                        }

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

                    }
                    else if (isDraggingTransform_) {
                        isDraggingTransform_ = false;
                        RegisterObjectEdited(selectedObject_, tempObjectStateStart_, "Gizmo Transform");
                    }
                }
            }
        }
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
    FinalizeCrashRecovery();
    animationWorkbench_.Finalize();
    primitiveDrawer_.Finalize();
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
}
// ==========================================================================================
// 2. 右パネル：Inspector (選択したオブジェクトの詳細設定)
// ==========================================================================================
void DebugEditor::DrawImGui() {
    Object3d* beforeTarget = selectedObject_;
    nlohmann::json beforeState;
    if (beforeTarget) {
        beforeState = CaptureObjectState(beforeTarget);
    }

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
        }
    }

    TrackInspectorEdit(beforeTarget, beforeState);
#endif
}



#ifdef USE_IMGUI
void DebugEditor::DrawProjectWindow() {
    projectWindow_.Draw();
}
#endif

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

void DebugEditor::InitializeCrashRecovery() {
    LoadCrashRecoveryCandidate();

    crashRecoverySessionId_ = "session_" + MakeCrashRecoveryStamp();
    crashRecoverySessionDir_ = ToGenericPath(fs::path(kCrashRecoveryRoot) / crashRecoverySessionId_);
    crashRecoveryDraftDir_ = ToGenericPath(fs::path(crashRecoverySessionDir_) / "draft");
    crashRecoveryLastFiles_ = nlohmann::json::array();
    crashRecoveryAutosaveTimer_ = kCrashRecoveryAutosaveInterval;
    crashRecoveryHeartbeatTimer_ = kCrashRecoveryHeartbeatInterval;
    crashRecoveryLastDirty_ = false;
    crashRecoverySessionActive_ = true;
    WriteCrashRecoverySession(false, false);
}

void DebugEditor::UpdateCrashRecovery(float deltaTime) {
#ifdef USE_IMGUI
    if (!crashRecoverySessionActive_) {
        return;
    }

    const bool dirty = HasAnyDirty();
    if (dirty && !crashRecoveryLastDirty_) {
        crashRecoveryAutosaveTimer_ = kCrashRecoveryAutosaveInterval;
    }

    crashRecoveryAutosaveTimer_ += deltaTime;
    crashRecoveryHeartbeatTimer_ += deltaTime;

    if (dirty && crashRecoveryAutosaveTimer_ >= kCrashRecoveryAutosaveInterval) {
        SaveCrashRecoveryDraft();
        crashRecoveryAutosaveTimer_ = 0.0f;
        crashRecoveryHeartbeatTimer_ = 0.0f;
    }
    else if (!dirty && (crashRecoveryLastDirty_ || crashRecoveryHeartbeatTimer_ >= kCrashRecoveryHeartbeatInterval)) {
        crashRecoveryLastFiles_ = nlohmann::json::array();
        WriteCrashRecoverySession(false, false);
        crashRecoveryHeartbeatTimer_ = 0.0f;
    }

    crashRecoveryLastDirty_ = dirty;
#endif
}

void DebugEditor::FinalizeCrashRecovery() {
    if (!crashRecoverySessionActive_) {
        return;
    }

    WriteCrashRecoverySession(true, HasAnyDirty(), crashRecoveryLastFiles_);
    crashRecoverySessionActive_ = false;
}

void DebugEditor::SaveCrashRecoveryDraft() {
    if (!crashRecoverySessionActive_ || !sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return;
    }

    auto targets = serializer_.BuildSceneSaveTargets(currentSceneFilename_, SaveMode::All);
    if (targets.empty()) {
        return;
    }

    nlohmann::json files = nlohmann::json::array();
    const fs::path draftRoot = fs::path(crashRecoveryDraftDir_);

    for (const auto& target : targets) {
        if (!IsCrashRecoverySourcePathAllowed(target.path)) {
            continue;
        }

        const fs::path draftPath = draftRoot / fs::path(target.path);
        if (!IsPathInside(draftPath, draftRoot)) {
            continue;
        }

        if (!WriteJsonFileSafe(draftPath, target.data)) {
            continue;
        }

        files.push_back({
            { "label", target.label },
            { "source", fs::path(target.path).generic_string() },
            { "draft", ToGenericPath(draftPath) },
            { "metadata", target.isMetadata }
        });
    }

    if (files.empty()) {
        return;
    }

    crashRecoveryLastFiles_ = files;
    WriteCrashRecoverySession(false, true, crashRecoveryLastFiles_);
}

void DebugEditor::WriteCrashRecoverySession(bool cleanExit, bool dirty, const nlohmann::json& files) {
    if (crashRecoverySessionDir_.empty()) {
        return;
    }

    nlohmann::json session;
    const fs::path sessionPath = fs::path(crashRecoverySessionDir_) / kCrashRecoverySessionFile;
    ReadJsonFileSafe(sessionPath, session);
    if (!session.is_object()) {
        session = nlohmann::json::object();
    }

    session["version"] = 1;
    session["sessionId"] = crashRecoverySessionId_;
    session["updatedAt"] = NowIsoString();
    if (!session.contains("startedAt")) {
        session["startedAt"] = NowIsoString();
    }
    session["cleanExit"] = cleanExit;
    session["dirty"] = dirty;
    session["dirtySummary"] = GetDirtySummaryText();
    session["sceneFile"] = std::string(currentSceneFilename_);
    session["sessionDir"] = crashRecoverySessionDir_;
    session["draftRoot"] = crashRecoveryDraftDir_;
    session["files"] = files.is_array() ? files : nlohmann::json::array();
    if (!session.contains("resolved")) {
        session["resolved"] = false;
    }

    WriteJsonFileSafe(sessionPath, session);
}

void DebugEditor::LoadCrashRecoveryCandidate() {
    crashRecoveryPending_ = false;
    crashRecoveryPromptOpened_ = false;
    crashRecoveryPendingManifest_.clear();

    const fs::path root = kCrashRecoveryRoot;
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        return;
    }

    fs::file_time_type bestTime{};
    bool hasBest = false;
    nlohmann::json bestManifest;

    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }

        const fs::path manifestPath = entry.path() / kCrashRecoverySessionFile;
        nlohmann::json manifest;
        if (!ReadJsonFileSafe(manifestPath, manifest)) {
            continue;
        }

        if (manifest.value("cleanExit", true) ||
            manifest.value("resolved", false) ||
            !manifest.value("dirty", false) ||
            !manifest.contains("files") ||
            !manifest["files"].is_array() ||
            manifest["files"].empty()) {
            continue;
        }

        const fs::file_time_type writeTime = fs::last_write_time(manifestPath, ec);
        if (ec) {
            continue;
        }
        if (!hasBest || writeTime > bestTime) {
            bestTime = writeTime;
            bestManifest = manifest;
            bestManifest["_manifestPath"] = ToGenericPath(manifestPath);
            hasBest = true;
        }
    }

    if (hasBest) {
        crashRecoveryPendingManifest_ = bestManifest;
        crashRecoveryPending_ = true;
        crashRecoveryStatus_.clear();
    }
}

void DebugEditor::DrawCrashRecoveryPrompt() {
#ifdef USE_IMGUI
    if (!crashRecoveryPending_) {
        return;
    }

    const char* popupName = "クラッシュ復元###CrashRecoveryPrompt";
    if (!crashRecoveryPromptOpened_) {
        ImGui::OpenPopup(popupName);
        crashRecoveryPromptOpened_ = true;
    }

    if (ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("前回のEditorが正常終了していません。未保存の自動ドラフトがあります。");
        ImGui::TextWrapped("復元すると、現在のJSONは退避してからドラフトを Resources/json に戻します。");
        ImGui::Separator();

        ImGui::Text("シーン: %s", crashRecoveryPendingManifest_.value("sceneFile", std::string("-")).c_str());
        ImGui::Text("保存時刻: %s", crashRecoveryPendingManifest_.value("updatedAt", std::string("-")).c_str());

        const auto& files = crashRecoveryPendingManifest_.value("files", nlohmann::json::array());
        if (ImGui::BeginTable("CrashRecoveryFiles", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("復元先");
            ImGui::TableSetupColumn("ドラフト");
            ImGui::TableHeadersRow();
            for (const auto& file : files) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", file.value("source", std::string("-")).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", file.value("draft", std::string("-")).c_str());
            }
            ImGui::EndTable();
        }

        if (!crashRecoveryStatus_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "%s", crashRecoveryStatus_.c_str());
        }

        if (ImGui::Button("復元する", ImVec2(120.0f, 0.0f))) {
            if (RestoreCrashRecoveryDraft()) {
                ResolveCrashRecoveryCandidate("restored");
                crashRecoveryPending_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("今回は使わない", ImVec2(140.0f, 0.0f))) {
            ResolveCrashRecoveryCandidate("dismissed");
            crashRecoveryPending_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
#endif
}

bool DebugEditor::RestoreCrashRecoveryDraft() {
    if (!crashRecoveryPendingManifest_.is_object() ||
        !crashRecoveryPendingManifest_.contains("files") ||
        !crashRecoveryPendingManifest_["files"].is_array()) {
        crashRecoveryStatus_ = "復元データを読み込めません。";
        return false;
    }

    const fs::path projectRoot = fs::current_path();
    const fs::path jsonRoot = projectRoot / "Resources/json";
    const fs::path recoveryRoot = projectRoot / fs::path(kCrashRecoveryRoot);
    const fs::path backupRoot = projectRoot / fs::path(kCrashRecoveryRoot) / ("pre_restore_" + MakeCrashRecoveryStamp());
    int restoredCount = 0;
    int errorCount = 0;

    for (const auto& file : crashRecoveryPendingManifest_["files"]) {
        const std::string sourceText = file.value("source", std::string(""));
        const std::string draftText = file.value("draft", std::string(""));
        if (sourceText.empty() || draftText.empty() || !IsCrashRecoverySourcePathAllowed(sourceText)) {
            ++errorCount;
            continue;
        }

        const std::string draftRootText = crashRecoveryPendingManifest_.value("draftRoot", std::string(""));
        const fs::path source = projectRoot / fs::path(sourceText);
        const fs::path draft = projectRoot / fs::path(draftText);
        const fs::path draftRoot = projectRoot / fs::path(draftRootText);
        if (draftRootText.empty() ||
            !IsPathInside(draftRoot, recoveryRoot) ||
            !IsPathInside(source, jsonRoot) ||
            !IsPathInside(draft, draftRoot) ||
            !fs::exists(draft)) {
            ++errorCount;
            continue;
        }

        std::error_code ec;
        if (fs::exists(source, ec)) {
            const fs::path currentBackup = backupRoot / fs::path(sourceText);
            fs::create_directories(currentBackup.parent_path(), ec);
            fs::copy_file(source, currentBackup, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                ++errorCount;
                continue;
            }
        }

        fs::create_directories(source.parent_path(), ec);
        if (ec) {
            ++errorCount;
            continue;
        }

        fs::copy_file(draft, source, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            ++errorCount;
            continue;
        }
        ++restoredCount;
    }

    if (restoredCount <= 0) {
        crashRecoveryStatus_ = "復元できるファイルがありませんでした。";
        return false;
    }

    crashRecoveryStatus_ = "クラッシュ復元: " + std::to_string(restoredCount) + "件を復元しました。";
    if (errorCount > 0) {
        crashRecoveryStatus_ += " " + std::to_string(errorCount) + "件は失敗しました。";
    }
    DebugConsole::GetInstance()->AddLog(crashRecoveryStatus_);
    TriggerSaveNotification(crashRecoveryStatus_);

    if (sceneManager_ && !sceneManager_->GetCurrentSceneName().empty() && !sceneManager_->IsTransitioning()) {
        sceneManager_->ChangeScene(sceneManager_->GetCurrentSceneName());
    }
    return true;
}

void DebugEditor::ResolveCrashRecoveryCandidate(const std::string& resolution) {
    if (!crashRecoveryPendingManifest_.is_object()) {
        return;
    }

    const std::string manifestPathText = crashRecoveryPendingManifest_.value("_manifestPath", std::string(""));
    if (manifestPathText.empty()) {
        return;
    }

    nlohmann::json manifest;
    if (!ReadJsonFileSafe(manifestPathText, manifest)) {
        manifest = crashRecoveryPendingManifest_;
        manifest.erase("_manifestPath");
    }
    manifest["resolved"] = true;
    manifest["resolution"] = resolution;
    manifest["resolvedAt"] = NowIsoString();
    WriteJsonFileSafe(manifestPathText, manifest);
}



void DebugEditor::SaveScene(SaveMode mode) {
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
    if (!selectedObject_) return;

#ifdef USE_IMGUI
    auto targets = serializer_.BuildSingleObjectSaveTargets(selectedObject_, std::string(currentSceneFilename_));
    if (targets.empty()) {
        DebugConsole::GetInstance()->AddLog("Save Preview: 単体保存の対象を作成できませんでした。");
        return;
    }

    sceneSavePreview_.Build(targets, "単体保存: " + selectedObject_->GetName());
    pendingSaveMode_ = SaveMode::Object;
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
    }
    else {
        DebugConsole::GetInstance()->AddLog("Save Preview: 保存をキャンセルしました。");
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
    case SaveMode::All:
    default:
        return "シーン全体保存: " + baseName;
    }
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
    if (!selectedObject_ || IsObjectInCurrentScene(selectedObject_)) {
        return;
    }

    const bool editorWasSelected = EditorManager::GetInstance()->GetSelectedObject() == this;
    selectedObject_ = nullptr;
    isDraggingTransform_ = false;
    hasInspectorEditStart_ = false;
    inspectorEditTarget_ = nullptr;
    inspectorEditStartState_.clear();
    tempObjectStateStart_.clear();
    if (editorWasSelected) {
        EditorManager::GetInstance()->ClearSelection();
    }
}

nlohmann::json DebugEditor::CaptureObjectState(Object3d* object) const {
    nlohmann::json state = nlohmann::json::object();
    if (!object) return state;
    if (object == selectedObject_ && !IsObjectInCurrentScene(object)) return state;

    state = object->ExportToJson();
    state["name"] = object->GetName();
    state["parentName"] = object->GetParent() ? object->GetParent()->GetName() : "";
    state["isStatic"] = object->IsStatic();
    return state;
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
    if (state.contains("parentName")) {
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

Object3d* DebugEditor::AddObjectFromState(const nlohmann::json& state) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene() || !state.is_object()) return nullptr;

    Object3dCommon* common = sceneManager_->GetCurrentScene()->GetObject3dCommon();
    if (!common) return nullptr;

    auto object = std::make_unique<Object3d>();
    object->Initialize(common);
    ApplyObjectState(object.get(), state);

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
    undoStack_.push_back(command);
    redoStack_.clear();
    constexpr size_t kMaxUndoCount = 128;
    while (undoStack_.size() > kMaxUndoCount) {
        undoStack_.pop_front();
    }
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

void DebugEditor::AddEditorObject(std::unique_ptr<Object3d> object, const std::string& label) {
    if (!object || !sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    Object3d* raw = object.get();
    ApplyGroundDefaultsForGeneratedModel(raw);
    nlohmann::json afterState = CaptureObjectState(raw);

    CollisionManager::GetInstance()->AddObject(raw);
    sceneManager_->GetCurrentScene()->GetObjects().push_back(std::move(object));
    selectedObject_ = raw;
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
        selectedObject_ = root;
        EditorManager::GetInstance()->SetSelectedObject(this);
    }
}

void DebugEditor::TrackInspectorEdit(Object3d* beforeTarget, const nlohmann::json& beforeState) {
#ifdef USE_IMGUI
    if (!beforeTarget || beforeTarget != selectedObject_ || !beforeState.is_object()) {
        return;
    }
    if (!IsObjectInCurrentScene(beforeTarget)) {
        ClearInvalidSelectedObject();
        return;
    }

    nlohmann::json afterState = CaptureObjectState(selectedObject_);
    bool changedThisFrame = beforeState != afterState;
    bool active = ImGui::IsAnyItemActive();

    if (changedThisFrame && !hasInspectorEditStart_) {
        inspectorEditStartState_ = beforeState;
        inspectorEditTarget_ = selectedObject_;
        hasInspectorEditStart_ = true;
    }

    if (changedThisFrame) {
        MarkDirtyForObject(selectedObject_);
    }

    if (!active && hasInspectorEditStart_) {
        if (inspectorEditTarget_ == selectedObject_) {
            RegisterObjectEdited(selectedObject_, inspectorEditStartState_, "Inspector Edit");
        }
        hasInspectorEditStart_ = false;
        inspectorEditTarget_ = nullptr;
        inspectorEditStartState_.clear();
    }
#endif
}

void DebugEditor::MarkDirtyForObject(Object3d* object) {
    if (!object) {
        MarkDirty(SaveMode::Object);
        return;
    }
    MarkDirtyForCategory(object->GetSaveCategory());
}

void DebugEditor::MarkDirtyForCategory(const std::string& category) {
    if (category == "Player") dirtyPlayer_ = true;
    else if (category == "Enemy") dirtyEnemy_ = true;
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
    case SaveMode::All:
    default:
        dirtyPlayer_ = true;
        dirtyEnemy_ = true;
        dirtyObject_ = true;
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
    case SaveMode::All:
    default:
        dirtyPlayer_ = false;
        dirtyEnemy_ = false;
        dirtyObject_ = false;
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
    case SaveMode::All:
    default:
        return HasAnyDirty();
    }
}

bool DebugEditor::HasAnyDirty() const {
    return dirtyPlayer_ || dirtyEnemy_ || dirtyObject_;
}

std::string DebugEditor::GetDirtySummaryText() const {
    if (!HasAnyDirty()) return "保存済み";

    std::string text = "未保存:";
    if (dirtyPlayer_) text += " Player";
    if (dirtyEnemy_) text += " Enemy";
    if (dirtyObject_) text += " Object";
    return text;
}

// 複製 (スマート・コピペ版)
void DebugEditor::DuplicateSelected() {
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    // 1. 完全なクローンを作成
    std::unique_ptr<Object3d> newObj = selectedObject_->Clone();

    // 2. 名前変更
    static int duplicateCount = 0;
    newObj->SetName(selectedObject_->GetName() + "_Copy" + std::to_string(duplicateCount++));

    // =========================================================
    //  マウスカーソルの位置(レイキャスト)を計算してペースト！
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
        if (obj.get() == selectedObject_ || obj->GetName() == "Cursor" || obj->GetName() == "Line" || !obj->GetIsVisible()) continue;

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
    if (!selectedObject_ || !sceneManager_->GetCurrentScene()) return;

    Object3d* target = selectedObject_;
    nlohmann::json beforeState = CaptureObjectState(target);
    std::string name = target->GetName();
    MarkDirtyForObject(target);
    sceneManager_->GetCurrentScene()->DestroyObject(target);

    EditorCommand command;
    command.type = EditorCommandType::ObjectDeleted;
    command.label = "Delete Object";
    command.beforeState = beforeState;
    command.beforeName = beforeState.value("name", name);
    RegisterCommand(command);

    // 重要：削除したポインタを持ち続けないようにする
    selectedObject_ = nullptr;
    EditorManager::GetInstance()->ClearSelection();
    DebugConsole::GetInstance()->AddLog("Deleted Object: " + name);
}
// ==========================================
//  Undo処理 
// ==========================================
void DebugEditor::PerformUndo() {
    if (undoStack_.empty()) return;

    EditorCommand cmd = undoStack_.back();
    undoStack_.pop_back();
    redoStack_.push_back(cmd);

    switch (cmd.type) {
    case EditorCommandType::ObjectCreated: {
        Object3d* object = FindObjectByName(cmd.afterName);
        if (object) {
            MarkDirtyForObject(object);
            RemoveObjectImmediate(object);
            selectedObject_ = nullptr;
            EditorManager::GetInstance()->ClearSelection();
        }
        break;
    }
    case EditorCommandType::ObjectDeleted: {
        Object3d* restored = AddObjectFromState(cmd.beforeState);
        if (restored) {
            selectedObject_ = restored;
            EditorManager::GetInstance()->SetSelectedObject(this);
            MarkDirtyForObject(restored);
        }
        break;
    }
    case EditorCommandType::ObjectEdited:
    default: {
        Object3d* object = FindObjectByName(cmd.afterName);
        if (!object) object = FindObjectByName(cmd.beforeName);
        if (object) {
            ApplyObjectState(object, cmd.beforeState);
            selectedObject_ = object;
            EditorManager::GetInstance()->SetSelectedObject(this);
            MarkDirtyForObject(object);
        }
        break;
    }
    }
    DebugConsole::GetInstance()->AddLog("Undo: " + cmd.label);
}

// ==========================================
//  Redo処理
// ==========================================
void DebugEditor::PerformRedo() {
    if (redoStack_.empty()) return;

    EditorCommand cmd = redoStack_.back();
    redoStack_.pop_back();
    undoStack_.push_back(cmd);

    switch (cmd.type) {
    case EditorCommandType::ObjectCreated: {
        Object3d* created = AddObjectFromState(cmd.afterState);
        if (created) {
            selectedObject_ = created;
            EditorManager::GetInstance()->SetSelectedObject(this);
            MarkDirtyForObject(created);
        }
        break;
    }
    case EditorCommandType::ObjectDeleted: {
        Object3d* object = FindObjectByName(cmd.beforeName);
        if (object) {
            MarkDirtyForObject(object);
            RemoveObjectImmediate(object);
            selectedObject_ = nullptr;
            EditorManager::GetInstance()->ClearSelection();
        }
        break;
    }
    case EditorCommandType::ObjectEdited:
    default: {
        Object3d* object = FindObjectByName(cmd.beforeName);
        if (!object) object = FindObjectByName(cmd.afterName);
        if (object) {
            ApplyObjectState(object, cmd.afterState);
            selectedObject_ = object;
            EditorManager::GetInstance()->SetSelectedObject(this);
            MarkDirtyForObject(object);
        }
        break;
    }
    }
    DebugConsole::GetInstance()->AddLog("Redo: " + cmd.label);
}



// マウス位置からワールド空間へのレイを作成
Ray DebugEditor::ScreenPointToRay(const Vector2& mousePos) {
    const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return Ray{};

    Matrix4x4 matView = camera->GetViewMatrix();
    Matrix4x4 matProj = camera->GetProjectionMatrix();
    Matrix4x4 matViewProj = math_.Multiply(matView, matProj);
    Matrix4x4 matInverseVP = math_.Inverse(matViewProj);


    float localMouseX = mousePos.x;
    float localMouseY = mousePos.y;

    // 2. 範囲外チェック
    if (localMouseX < 0 || localMouseX > gameViewSize_.x ||
        localMouseY < 0 || localMouseY > gameViewSize_.y) {
        return Ray{ {0,0,0}, {0,0,0} };
    }

    // 3. NDC変換
    Vector3 nearPos, farPos;
    nearPos.x = (2.0f * localMouseX) / gameViewSize_.x - 1.0f;
    nearPos.y = 1.0f - (2.0f * localMouseY) / gameViewSize_.y;
    nearPos.z = 0.0f;

    farPos = nearPos;
    farPos.z = 1.0f;

    // 4. ワールド変換
    Vector3 worldNear = math_.Transform(nearPos, matInverseVP);
    Vector3 worldFar = math_.Transform(farPos, matInverseVP);

    Ray ray;
    ray.origin = worldNear;
    ray.diff = worldFar - worldNear;

    return ray;
}
// ---------------------------------------------------------
// レイと平面(Y=0)の交点を計算する関数
// 戻り値: 交差したら true, その座標を intersectOut に入れる
// ---------------------------------------------------------
bool DebugEditor::IntersectRayPlane(const Ray& ray, Vector3& intersectOut) {
    // 計算用のMathクラス
    Math math;

    // 平面の定義（上向きの法線, 高さ0）
    Vector3 planeNormal = { 0.0f, 1.0f, 0.0f };
    float planeHeight = 0.0f;

    // 平行かどうかチェック (内積が0に近い＝平行)
    // レイの方向ベクトルと平面の法線の内積をとる
    float denominator = math.Dot(ray.diff, planeNormal);

    // レイが水平に近いなら判定しない (0除算防止)
    if (std::abs(denominator) < 0.0001f) {
        return false;
    }

    // 交点までの距離 t を求める公式
    // t = (PlaneHeight - Dot(RayOrigin, PlaneNormal)) / Dot(RayDir, PlaneNormal)

    // まずレイの方向を正規化する
    Vector3 rayDir = math.Normalize(ray.diff);
    float dotDir = math.Dot(rayDir, planeNormal);

    // 念のため再チェック
    if (std::abs(dotDir) < 0.0001f) return false;

    // 距離tの計算
    float t = (planeHeight - math.Dot(ray.origin, planeNormal)) / dotDir;

    // tがマイナス＝カメラの後ろ側にあるので無視
    if (t < 0.0f) {
        return false;
    }

    // 交点座標 = 原点 + 方向 * 距離
    // intersectOut = ray.origin + (rayDir * t)
    Vector3 travel = { rayDir.x * t, rayDir.y * t, rayDir.z * t };
    intersectOut = { ray.origin.x + travel.x, ray.origin.y + travel.y, ray.origin.z + travel.z };

    return true;
}





void DebugEditor::DrawPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    auto drawPreviewObject = [&](Object3d* object) {
        if (!object) return;

        std::string originalClassName;
        bool useClassOverride = ShouldUsePreviewDrawClassOverride(object);
        if (useClassOverride) {
            originalClassName = object->GetClassName();
            object->SetClassName("Model");
        }
        object->Draw(pointLightResource, spotLightResource);
        if (useClassOverride) {
            object->SetClassName(originalClassName);
        }
    };

    if (previewObject_) {
        drawPreviewObject(previewObject_.get());
        for (auto& child : previewChildObjects_) {
            drawPreviewObject(child.get());
        }
    }

    for (auto& object : brushPreviewObjects_) {
        drawPreviewObject(object.get());
    }
}

// ========================================================================
// セーブ通知のトリガー
// ========================================================================
void DebugEditor::TriggerSaveNotification(const std::string& filename) {
#ifdef USE_IMGUI
    saveNotificationTimer_ = 1.0f;
    saveNotificationMsg_ = "[ " + filename + " ] にセーブしました";
#endif
}

void DebugEditor::PollDDSCacheNotifications() {
#ifdef USE_IMGUI
    const fs::path notificationLog = "Resources/.cache/dds_cache_notifications.jsonl";
    if (!fs::exists(notificationLog)) {
        ddsCacheNotificationReadOffset_ = 0;
        return;
    }

    std::error_code ec;
    const std::uintmax_t fileSize = fs::file_size(notificationLog, ec);
    if (ec) {
        return;
    }
    if (fileSize < ddsCacheNotificationReadOffset_) {
        ddsCacheNotificationReadOffset_ = 0;
    }
    if (fileSize == ddsCacheNotificationReadOffset_) {
        return;
    }

    std::ifstream ifs(notificationLog, std::ios::binary);
    if (!ifs) {
        return;
    }

    ifs.seekg(static_cast<std::streamoff>(ddsCacheNotificationReadOffset_), std::ios::beg);

    std::string line;
    bool hasCompletedConversion = false;
    while (std::getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }

        try {
            nlohmann::json j = nlohmann::json::parse(line);
            if (j.value("status", "") != "built") {
                continue;
            }

            hasCompletedConversion = true;
        }
        catch (...) {
        }
    }

    ddsCacheNotificationReadOffset_ = fileSize;
    if (hasCompletedConversion) {
        saveNotificationTimer_ = 1.5f;
        saveNotificationMsg_ = "DDS変換が完了しました";
    }
#endif
}


// ========================================================================
// セーブ通知の描画処理 (フラッシュ & メッセージ)
// ========================================================================
void DebugEditor::DrawSaveNotification() {
#ifdef USE_IMGUI
    if (saveNotificationTimer_ <= 0.0f) return;

    // タイマー減算 (ImGuiのDeltaTimeを利用してフレームレート非依存に)
    saveNotificationTimer_ -= ImGui::GetIO().DeltaTime;

    // フェードアウトの割合 (1.0 -> 0.0 に向かって減っていく)
    float fadeRatio = saveNotificationTimer_ / 1.5f;
    if (fadeRatio > 1.0f) fadeRatio = 1.0f;

    // 最前面レイヤーを取得
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // 1. 全画面の白いフラッシュ描画
    int whiteAlpha = (int)(fadeRatio * 80.0f);
    drawList->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(255, 255, 255, whiteAlpha));

    // 2. 文字の描画 (同じく最前面レイヤーに直接描く)
    float fontSize = ImGui::GetFontSize() * 2.0f; // 文字を2倍サイズに
    const char* text = saveNotificationMsg_.c_str();

    // 文字のピクセルサイズを計算して、画面中央の座標を求める
    ImVec2 baseTextSize = ImGui::CalcTextSize(text);
    ImVec2 textSize = ImVec2(baseTextSize.x * 2.0f, baseTextSize.y * 2.0f);
    ImVec2 textPos = ImVec2((displaySize.x - textSize.x) * 0.5f, (displaySize.y - textSize.y) * 0.5f);

    // 文字のアルファ値 (フェードに合わせて透明になっていく)
    int textAlpha = (int)(fadeRatio * 255.0f);
    ImU32 textColor = IM_COL32(40, 40, 40, textAlpha);         // 濃いグレー
    ImU32 outlineColor = IM_COL32(255, 255, 255, textAlpha);   // 白いフチ取り

    // 少し文字を見やすくするために白い縁取り（シャドウ）を先に描画
    drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(textPos.x + 2, textPos.y + 2), outlineColor, text);
    // メインの文字を描画
    drawList->AddText(ImGui::GetFont(), fontSize, textPos, textColor, text);
#endif
}


// ========================================================================
// 3D座標をGameViewのスクリーン座標に変換
// ========================================================================
Vector3 DebugEditor::WorldToScreen(const Vector3& worldPos) {
    Camera* cam = CameraManager::GetInstance()->GetActiveCamera();
    if (!cam) return { 0, 0, -1 }; // カメラが無い場合はZを-1(無効)にして返す

    Matrix4x4 viewProj = math_.Multiply(cam->GetViewMatrix(), cam->GetProjectionMatrix());

    // NDC座標系（-1.0 ～ 1.0）への変換
    Vector3 ndc = math_.Transform(worldPos, viewProj);

    // カメラの「後ろ」にある場合はZが0未満、またはW除算の関係で範囲外になる
    if (ndc.z < 0.0f || ndc.z > 1.0f) {
        return { 0, 0, -1 }; // 描画しないフラグとしてZに-1を入れる
    }

    // NDCからGameViewのピクセル座標へ変換
    float screenX = gameViewOffset_.x + (ndc.x + 1.0f) * 0.5f * gameViewSize_.x;
    float screenY = gameViewOffset_.y + (1.0f - ndc.y) * 0.5f * gameViewSize_.y;

    return { screenX, screenY, ndc.z };
}

// ========================================================================
// 3D空間へのアイコンオーバーレイ描画
// ========================================================================
void DebugEditor::Draw3DIcons() {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    // 最前面に描画するためのImGuiレイヤーを取得
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();

    for (const auto& obj : objects) {
        if (!obj->GetIsVisible()) continue;
        std::string className = obj->GetClassName();
        const char* iconStr = nullptr;
        ImU32 iconColor = IM_COL32(255, 255, 255, 200); // デフォルト白（半透明）

        // クラスごとに表示するアイコンを変える！
        if (className == "InvisibleBox") {
            //  名前でトリガーか当たり判定かを区別する！
            if (obj->GetName().find("Trigger") != std::string::npos) {
                iconStr = ICON_FA_BOLT; // 雷マーク (トリガー用)
                iconColor = IM_COL32(255, 255, 0, 200); // 黄色
            }
            else {
                iconStr = ICON_FA_SHIELD_ALT; // 盾マーク (当たり判定用)
                iconColor = IM_COL32(100, 200, 255, 200); // 水色
            }
        }
        else if (className == "Spawner") {
            iconStr = ICON_FA_BOX_OPEN;
            iconColor = IM_COL32(255, 150, 50, 200);  // オレンジ
        }
        else if (className == "CinematicCamera") {
            iconStr = ICON_FA_VIDEO;
            iconColor = IM_COL32(255, 100, 255, 200); // ピンク
        }

        // アイコンが設定されたオブジェクトのみ処理
        if (iconStr) {
            // オブジェクトのワールド座標を取得
            Vector3 worldPos = { obj->GetWorldMatrix().m[3][0], obj->GetWorldMatrix().m[3][1], obj->GetWorldMatrix().m[3][2] };

            // カメラの場合は、頭上にアイコンを出すためにY軸を少し上げる
            if (className == "CinematicCamera") {
                worldPos.y += 1.5f;
            }

            // 3D -> 2D 変換
            Vector3 screenPos = WorldToScreen(worldPos);

            // Zが0以上の時だけ（カメラの前にいる時だけ）描画
            if (screenPos.z >= 0.0f) {
                // 選択中のオブジェクトはアイコンを少し大きく、不透明にする
                float fontSize = (selectedObject_ == obj.get()) ? ImGui::GetFontSize() * 1.5f : ImGui::GetFontSize() * 1.2f;
                if (selectedObject_ == obj.get()) iconColor = IM_COL32(255, 255, 0, 255); // 選択中は黄色

                // 影（アウトライン）を黒で描画して見やすくする
                ImVec2 pos = ImVec2(screenPos.x, screenPos.y);
                drawList->AddText(ImGui::GetFont(), fontSize, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 255), iconStr);
                // 本体の描画
                drawList->AddText(ImGui::GetFont(), fontSize, pos, iconColor, iconStr);
            }
        }
    }
#endif
}

// ========================================================================
// イベントIDのオーバーレイ描画
// ========================================================================
void DebugEditor::DrawEventIDOverlay() {
#ifdef USE_IMGUI
    if (!drawEventIDs_ || !sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    auto& objects = sceneManager_->GetCurrentScene()->GetObjects();

    // 1. EventIDを持つオブジェクトをマップに集める (線を描画するため)
    std::unordered_map<int, std::vector<Object3d*>> eventMap;
    for (const auto& obj : objects) {
        int eID = obj->GetEventID();
        if (eID != -1) eventMap[eID].push_back(obj.get());
    }

    for (const auto& obj : objects) {
        if (!obj->GetIsVisible()) continue;

        // イベント関連のIDを取得
        int myID = obj->GetEventID();
        int targetID = obj->GetTargetID();

        // --- 2. ID間のリレーション線を描画 ---
        if (targetID != -1 && eventMap.count(targetID)) {
            Vector3 startPos = obj->GetWorldPosition();
            Vector3 startScreen = WorldToScreen(startPos);

            if (startScreen.z >= 0.0f) {
                for (Object3d* targetObj : eventMap[targetID]) {
                    Vector3 endPos = targetObj->GetWorldPosition();
                    Vector3 endScreen = WorldToScreen(endPos);

                    if (endScreen.z >= 0.0f) {
                        // 線を描画 (少し透明な水色)
                        drawList->AddLine(
                            ImVec2(startScreen.x, startScreen.y),
                            ImVec2(endScreen.x, endScreen.y),
                            IM_COL32(0, 255, 255, 150),
                            2.0f
                        );

                        // 終点に小さな丸を描画して方向を示す
                        drawList->AddCircleFilled(ImVec2(endScreen.x, endScreen.y), 4.0f, IM_COL32(0, 255, 255, 200));
                    }
                }
            }
        }

        // --- 3. IDラベルの描画 (既存処理) ---
        std::string label = "";
        if (myID != -1) label += ICON_FA_FINGERPRINT " [ID: " + std::to_string(myID) + "] ";
        if (targetID != -1) label += ICON_FA_BULLSEYE " [Target: " + std::to_string(targetID) + "]";

        if (label.empty()) continue;

        // 3D座標の取得と変換（頭上に表示）
        Vector3 worldPos = obj->GetWorldPosition();
        worldPos.y += obj->GetTransform()->scale.y + 0.5f;

        Vector3 screenPos = WorldToScreen(worldPos);

        if (screenPos.z >= 0.0f) {
            ImVec2 pos = ImVec2(screenPos.x, screenPos.y);
            ImU32 color = (myID != -1) ? IM_COL32(255, 255, 0, 255) : IM_COL32(100, 200, 255, 255);
            if (selectedObject_ == obj.get()) color = IM_COL32(255, 255, 255, 255); // 選択中は白

            // アウトライン
            drawList->AddText(NULL, 0.0f, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 255), label.c_str());
            // 本体
            drawList->AddText(NULL, 0.0f, pos, color, label.c_str());
        }
    }
#endif
}

// ==========================================
//  一発・床ピタッ！ (接地機能)
// ==========================================
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
    selectedObject_ = rootObject;
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
