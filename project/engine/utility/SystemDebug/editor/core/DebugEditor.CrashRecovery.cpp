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



