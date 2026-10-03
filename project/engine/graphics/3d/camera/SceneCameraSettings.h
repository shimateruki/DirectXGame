#pragma once

#include "engine/utility/math/Math.h"
#include "json.hpp"

#include <algorithm>
#include <string>

// シーンへ保存するCamera Object専用の設定です。
// CameraManagerは描画用カメラを所有し、この設定は編集・保存・再生の入力データとして扱います。
/// <summary>シーンカメラを通常視点と演出視点のどちらに使うか。</summary>
enum class SceneCameraRole {
    kMain = 0,
    kCinematic = 1,
};

/// <summary>視点位置の取得元。</summary>
enum class SceneCameraEyeSource {
    kCameraObject = 0,
    kSceneObject = 1,
};

/// <summary>カメラの注視点を決める方法。</summary>
enum class SceneCameraTargetMode {
    kFixedPoint = 0,
    kSceneObject = 1,
    kCameraForward = 2,
};

/// <summary>視点と注視点の追従方法。</summary>
enum class SceneCameraFollowMode {
    kSnap = 0,
    kSmooth = 1,
};

/// <summary>シーンカメラ切替時の補間曲線。</summary>
enum class SceneCameraEasing {
    kLinear = 0,
    kEaseIn = 1,
    kEaseOut = 2,
    kEaseInOut = 3,
    kSmootherStep = 4,
};

/// <summary>シーンに保存する視点、注視点、画角と切替時間の設定。</summary>
struct SceneCameraSettings {
    int version = 1;
    bool enabled = true;
    SceneCameraRole role = SceneCameraRole::kCinematic;

    // Camera::SetFovYと同じくラジアンで保持します。
    float fovY = 0.45f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;

    SceneCameraEyeSource eyeSource = SceneCameraEyeSource::kCameraObject;
    std::string eyeObjectName;
    Vector3 eyeOffset = { 0.0f, 0.0f, 0.0f };
    SceneCameraFollowMode eyeFollowMode = SceneCameraFollowMode::kSnap;
    float eyeFollowResponse = 12.0f;

    SceneCameraTargetMode targetMode = SceneCameraTargetMode::kCameraForward;
    std::string targetObjectName;
    Vector3 targetOffset = { 0.0f, 0.0f, 0.0f };
    Vector3 fixedTarget = { 0.0f, 0.0f, 0.0f };
    float forwardDistance = 10.0f;
    SceneCameraFollowMode targetFollowMode = SceneCameraFollowMode::kSnap;
    float targetFollowResponse = 14.0f;

    float blendInDuration = 0.30f;
    float blendOutDuration = 0.30f;
    SceneCameraEasing easing = SceneCameraEasing::kSmootherStep;
};

/// <summary>シーンカメラの設定を保存用JSONへ変換する。</summary>
inline nlohmann::json SerializeSceneCameraSettings(const SceneCameraSettings& settings) {
    nlohmann::json camera;
    camera["version"] = settings.version;
    camera["enabled"] = settings.enabled;
    camera["role"] = static_cast<int>(settings.role);
    camera["fovY"] = settings.fovY;
    camera["nearClip"] = settings.nearClip;
    camera["farClip"] = settings.farClip;
    camera["eyeSource"] = static_cast<int>(settings.eyeSource);
    camera["eyeObjectName"] = settings.eyeObjectName;
    camera["eyeOffset"] = { settings.eyeOffset.x, settings.eyeOffset.y, settings.eyeOffset.z };
    camera["eyeFollowMode"] = static_cast<int>(settings.eyeFollowMode);
    camera["eyeFollowResponse"] = settings.eyeFollowResponse;
    camera["targetMode"] = static_cast<int>(settings.targetMode);
    camera["targetObjectName"] = settings.targetObjectName;
    camera["targetOffset"] = { settings.targetOffset.x, settings.targetOffset.y, settings.targetOffset.z };
    camera["fixedTarget"] = { settings.fixedTarget.x, settings.fixedTarget.y, settings.fixedTarget.z };
    camera["forwardDistance"] = settings.forwardDistance;
    camera["targetFollowMode"] = static_cast<int>(settings.targetFollowMode);
    camera["targetFollowResponse"] = settings.targetFollowResponse;
    camera["blendInDuration"] = settings.blendInDuration;
    camera["blendOutDuration"] = settings.blendOutDuration;
    camera["easing"] = static_cast<int>(settings.easing);
    return camera;
}

/// <summary>JSONにある項目だけを既存設定へ読み込み、数値を有効範囲に収める。</summary>
inline void DeserializeSceneCameraSettings(const nlohmann::json& camera, SceneCameraSettings& settings) {
    if (!camera.is_object()) {
        return;
    }

    auto readVector3 = [&camera](const char* key, Vector3& value) {
        if (camera.contains(key) && camera[key].is_array() && camera[key].size() >= 3) {
            value = { camera[key][0], camera[key][1], camera[key][2] };
        }
    };

    if (camera.contains("version")) settings.version = camera["version"].get<int>();
    if (camera.contains("enabled")) settings.enabled = camera["enabled"].get<bool>();
    if (camera.contains("role")) settings.role = static_cast<SceneCameraRole>(std::clamp(camera["role"].get<int>(), 0, 1));
    if (camera.contains("fovY")) settings.fovY = camera["fovY"].get<float>();
    if (camera.contains("nearClip")) settings.nearClip = camera["nearClip"].get<float>();
    if (camera.contains("farClip")) settings.farClip = camera["farClip"].get<float>();
    if (camera.contains("eyeSource")) settings.eyeSource = static_cast<SceneCameraEyeSource>(std::clamp(camera["eyeSource"].get<int>(), 0, 1));
    if (camera.contains("eyeObjectName") && camera["eyeObjectName"].is_string()) settings.eyeObjectName = camera["eyeObjectName"].get<std::string>();
    readVector3("eyeOffset", settings.eyeOffset);
    if (camera.contains("eyeFollowMode")) settings.eyeFollowMode = static_cast<SceneCameraFollowMode>(std::clamp(camera["eyeFollowMode"].get<int>(), 0, 1));
    if (camera.contains("eyeFollowResponse")) settings.eyeFollowResponse = camera["eyeFollowResponse"].get<float>();
    if (camera.contains("targetMode")) settings.targetMode = static_cast<SceneCameraTargetMode>(std::clamp(camera["targetMode"].get<int>(), 0, 2));
    if (camera.contains("targetObjectName") && camera["targetObjectName"].is_string()) settings.targetObjectName = camera["targetObjectName"].get<std::string>();
    readVector3("targetOffset", settings.targetOffset);
    readVector3("fixedTarget", settings.fixedTarget);
    if (camera.contains("forwardDistance")) settings.forwardDistance = camera["forwardDistance"].get<float>();
    if (camera.contains("targetFollowMode")) settings.targetFollowMode = static_cast<SceneCameraFollowMode>(std::clamp(camera["targetFollowMode"].get<int>(), 0, 1));
    if (camera.contains("targetFollowResponse")) settings.targetFollowResponse = camera["targetFollowResponse"].get<float>();
    if (camera.contains("blendInDuration")) settings.blendInDuration = camera["blendInDuration"].get<float>();
    if (camera.contains("blendOutDuration")) settings.blendOutDuration = camera["blendOutDuration"].get<float>();
    if (camera.contains("easing")) settings.easing = static_cast<SceneCameraEasing>(std::clamp(camera["easing"].get<int>(), 0, 4));

    settings.fovY = std::clamp(settings.fovY, 0.05f, 3.0f);
    settings.nearClip = (std::max)(settings.nearClip, 0.001f);
    settings.farClip = (std::max)(settings.farClip, settings.nearClip + 0.01f);
    settings.eyeFollowResponse = (std::max)(settings.eyeFollowResponse, 0.01f);
    settings.targetFollowResponse = (std::max)(settings.targetFollowResponse, 0.01f);
    settings.forwardDistance = (std::max)(settings.forwardDistance, 0.01f);
    settings.blendInDuration = (std::max)(settings.blendInDuration, 0.0f);
    settings.blendOutDuration = (std::max)(settings.blendOutDuration, 0.0f);
}
