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

    text3DGenerator_.DrawPreview(pointLightResource, spotLightResource);
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
        if (!obj->GetIsRenderVisible()) continue;
        std::string className = obj->GetClassName();
        const char* iconStr = nullptr;
        ImU32 iconColor = IM_COL32(255, 255, 255, 200); // デフォルト白（半透明）

        // ObjectのClassに応じたIconを表示します。
        if (className == "InvisibleBox") {
            // 旧データはClass情報がないため、名前からTriggerとColliderを補完します。
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
        else if (obj->IsCameraObject()) {
            iconStr = ICON_FA_VIDEO;
            iconColor = IM_COL32(255, 100, 255, 200); // ピンク
        }

        // アイコンが設定されたオブジェクトのみ処理
        if (iconStr) {
            // オブジェクトのワールド座標を取得
            Vector3 worldPos = { obj->GetWorldMatrix().m[3][0], obj->GetWorldMatrix().m[3][1], obj->GetWorldMatrix().m[3][2] };

            // カメラの場合は、頭上にアイコンを出すためにY軸を少し上げる
            if (obj->IsCameraObject()) {
                worldPos.y += 1.5f;
            }

            // 3D -> 2D 変換
            Vector3 screenPos = WorldToScreen(worldPos);

            // Zが0以上の時だけ（カメラの前にいる時だけ）描画
            if (screenPos.z >= 0.0f) {
                // 選択中のオブジェクトはアイコンを少し大きく、不透明にする
                const bool isSelected = IsObjectSelected(obj.get());
                float fontSize = isSelected ? ImGui::GetFontSize() * 1.5f : ImGui::GetFontSize() * 1.2f;
                if (isSelected) iconColor = IM_COL32(255, 255, 0, 255); // 選択中は黄色

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
        if (!obj->GetIsRenderVisible()) continue;

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
            if (IsObjectSelected(obj.get())) color = IM_COL32(255, 255, 255, 255); // 選択中は白

            // アウトライン
            drawList->AddText(NULL, 0.0f, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 255), label.c_str());
            // 本体
            drawList->AddText(NULL, 0.0f, pos, color, label.c_str());
        }
    }
#endif
}

// ==========================================
// 選択Objectを直下の床面へ接地させます。
// ==========================================
