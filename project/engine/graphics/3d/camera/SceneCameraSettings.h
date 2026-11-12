#pragma once

#include "engine/utility/math/Math.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <string>

// シーンへ保存するCamera Object専用の設定です。
// CameraManagerは描画用カメラを所有し、この設定は編集・保存・再生の入力データとして扱います。
/// <summary>シーンカメラを通常視点と演出視点のどちらに使うか。</summary>
enum class SceneCameraRole {
    Main = 0,
    Cinematic = 1,
};

/// <summary>視点位置の取得元。</summary>
enum class SceneCameraEyeSource {
    CameraObject = 0,
    SceneObject = 1,
    SceneObjectRelativeToReference = 2,
};

/// <summary>カメラの注視点を決める方法。</summary>
enum class SceneCameraTargetMode {
    FixedPoint = 0,
    SceneObject = 1,
    CameraForward = 2,
};

/// <summary>視点と注視点の追従方法。</summary>
enum class SceneCameraFollowMode {
    Snap = 0,
    Smooth = 1,
};

/// <summary>シーンカメラ切替時の補間曲線。</summary>
enum class SceneCameraEasing {
    Linear = 0,
    EaseIn = 1,
    EaseOut = 2,
    EaseInOut = 3,
    SmootherStep = 4,
};

/// <summary>Camera Objectが自動適用される範囲と編集用の表示色。</summary>
// 適用範囲はワールド座標。視点のTransformや親の回転・拡縮とは独立して編集する。
// 関連：CameraEditor（編集）、CameraManager（領域選択）、Object3D（保存）
struct SceneCameraRegionSettings {
    bool enabled = false;
    engine::Vector3 center = {0.0f, 0.0f, 0.0f};
    engine::Vector3 size = {20.0f, 10.0f, 20.0f};
    engine::Vector4 color = {0.25f, 0.8f, 1.0f, 0.9f};
    int priority = 0;
    float activationDelay = 0.2f;
    float exitMargin = 0.5f;

    // 戦闘用の範囲だけで手前のバリア上部を抑え、足元の境界は残します。
    bool clearForegroundBarriers = false;
    float barrierFloorHeight = 0.0f;
    float barrierEdgeHeight = 0.6f;
    float barrierOpacity = 0.03f; // 領域境界の表示アルファ。
    bool routeEnabled = false;
    std::string endCameraGuid; // 改名に影響されない終点Camera Objectへの非所有参照。
    engine::Vector3 pathStart{}, pathEnd{}; // プレイヤーが歩く道の始点・終点（ワールド座標）。

    /// 半分の大きさに退出余白を加え、境界付近で選択が往復するのを防ぐ。
    bool Contains(const engine::Vector3& position, bool retain = false) const {
        const float margin = retain ? exitMargin : 0.0f;
        return std::abs(position.x - center.x) <= size.x * 0.5f + margin &&
               std::abs(position.y - center.y) <= size.y * 0.5f + margin &&
               std::abs(position.z - center.z) <= size.z * 0.5f + margin;
    }
};

/// <summary>シーンに保存するカメラ設定。描画カメラそのものは所有しない。</summary>
// 関連：CameraEditor（編集）、CameraManager（領域選択）、Object3D（保存）
struct SceneCameraSettings {
    int version = 1;
    bool enabled = true;
    SceneCameraRole role = SceneCameraRole::Cinematic;
    SceneCameraRegionSettings region;

    // Camera::SetFovYと同じく縦画角をラジアンで保持する。クリップ距離はワールド座標の距離。
    float fovY = 0.45f;
    bool blendFov = false;
    float nearClip = 0.1f; // 近クリップ面までの距離。
    float farClip = 1000.0f; // 遠クリップ面までの距離。

    SceneCameraEyeSource eyeSource = SceneCameraEyeSource::CameraObject;

    // kSceneObjectのときだけ名前で参照先を探す。オフセットは参照先の位置へ加算する。
    std::string eyeObjectName;
    // 相対視点ではEye対象から参照対象への方向を前方にする。eyeOffsetは右・上・前の距離。
    std::string eyeReferenceObjectName;
    engine::Vector3 eyeOffset = engine::Vector3::ZERO;
    SceneCameraFollowMode eyeFollowMode = SceneCameraFollowMode::Snap;
    float eyeFollowResponse = 12.0f;

    SceneCameraTargetMode targetMode = SceneCameraTargetMode::CameraForward;

    // kSceneObjectでは名前とオフセット、kFixedPointでは固定のワールド座標を使用する。
    std::string targetObjectName;
    engine::Vector3 targetOffset = engine::Vector3::ZERO;
    engine::Vector3 fixedTarget = engine::Vector3::ZERO;
    float forwardDistance = 10.0f;
    SceneCameraFollowMode targetFollowMode = SceneCameraFollowMode::Snap;
    float targetFollowResponse = 14.0f;

    float blendInDuration = 0.30f; // このカメラへ切り替えるブレンド秒数。
    float blendOutDuration = 0.30f; // 通常カメラへ戻すブレンド秒数。

    // 切替時間は秒。0は補間なしの即時切替を表す。
    SceneCameraEasing easing = SceneCameraEasing::SmootherStep;
};

/// <summary>シーンカメラの設定を保存用JSONへ変換する。</summary>
inline nlohmann::json SerializeSceneCameraSettings(const SceneCameraSettings& settings) {
    // 保存形式では列挙値を数値にし、視点と注視点の追従設定を同じ階層に揃える。
    nlohmann::json camera;
    camera["version"] = settings.version;
    camera["enabled"] = settings.enabled;
    camera["role"] = static_cast<int>(settings.role);
    camera["fovY"] = settings.fovY;
    camera["blendFov"] = settings.blendFov;
    camera["nearClip"] = settings.nearClip;
    camera["farClip"] = settings.farClip;
    camera["eyeSource"] = static_cast<int>(settings.eyeSource);
    camera["eyeObjectName"] = settings.eyeObjectName;
    camera["eyeReferenceObjectName"] = settings.eyeReferenceObjectName;
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
    const auto& region = settings.region;
    camera["region"] = {
        {"enabled", region.enabled}, {"center", {region.center.x, region.center.y, region.center.z}},
        {"size", {region.size.x, region.size.y, region.size.z}},
        {"color", {region.color.x, region.color.y, region.color.z, region.color.w}},
        {"priority", region.priority}, {"activationDelay", region.activationDelay}, {"exitMargin", region.exitMargin},
        {"clearForegroundBarriers", region.clearForegroundBarriers},
        {"barrierFloorHeight", region.barrierFloorHeight}, {"barrierEdgeHeight", region.barrierEdgeHeight},
        {"barrierOpacity", region.barrierOpacity},
        {"routeEnabled", region.routeEnabled}, {"endCameraGuid", region.endCameraGuid},
        {"pathStart", {region.pathStart.x,region.pathStart.y,region.pathStart.z}},
        {"pathEnd", {region.pathEnd.x,region.pathEnd.y,region.pathEnd.z}}
    };
    return camera;
}

/// <summary>JSONにある項目だけを既存設定へ読み込み、数値を有効範囲に収める。</summary>
/// <remarks>入力がオブジェクトでなければ何もしない。通常項目は現在値を維持し、region未設定は旧データとの互換性のため無効へ戻す。</remarks>
inline void DeserializeSceneCameraSettings(const nlohmann::json& camera, SceneCameraSettings& settings) {
    // 部分的な設定変更を許すため、存在しない項目は既存値のままにする。
    if (!camera.is_object()) {
        return;
    }

    auto readVector3 = [&camera](const char* key, engine::Vector3& value) {
        if (camera.contains(key) && camera[key].is_array() && camera[key].size() >= 3) {
            value = { camera[key][0], camera[key][1], camera[key][2] };
        }
    };

    if (camera.contains("version"))
        settings.version = camera["version"].get<int>();
    if (camera.contains("enabled"))
        settings.enabled = camera["enabled"].get<bool>();
    if (camera.contains("role"))
        settings.role = static_cast<SceneCameraRole>(std::clamp(camera["role"].get<int>(), 0, 1));
    if (camera.contains("fovY"))
        settings.fovY = camera["fovY"].get<float>();
    if (camera.contains("blendFov") && camera["blendFov"].is_boolean())
        settings.blendFov = camera["blendFov"].get<bool>();
    if (camera.contains("nearClip"))
        settings.nearClip = camera["nearClip"].get<float>();
    if (camera.contains("farClip"))
        settings.farClip = camera["farClip"].get<float>();
    if (camera.contains("eyeSource"))
        settings.eyeSource = static_cast<SceneCameraEyeSource>(std::clamp(camera["eyeSource"].get<int>(), 0, 2));
    if (camera.contains("eyeObjectName") && camera["eyeObjectName"].is_string())
        settings.eyeObjectName = camera["eyeObjectName"].get<std::string>();
    if (camera.contains("eyeReferenceObjectName") && camera["eyeReferenceObjectName"].is_string())
        settings.eyeReferenceObjectName = camera["eyeReferenceObjectName"].get<std::string>();
    readVector3("eyeOffset", settings.eyeOffset);
    if (camera.contains("eyeFollowMode"))
        settings.eyeFollowMode = static_cast<SceneCameraFollowMode>(std::clamp(camera["eyeFollowMode"].get<int>(), 0, 1));
    if (camera.contains("eyeFollowResponse"))
        settings.eyeFollowResponse = camera["eyeFollowResponse"].get<float>();
    if (camera.contains("targetMode"))
        settings.targetMode = static_cast<SceneCameraTargetMode>(std::clamp(camera["targetMode"].get<int>(), 0, 2));
    if (camera.contains("targetObjectName") && camera["targetObjectName"].is_string())
        settings.targetObjectName = camera["targetObjectName"].get<std::string>();
    readVector3("targetOffset", settings.targetOffset);
    readVector3("fixedTarget", settings.fixedTarget);
    if (camera.contains("forwardDistance"))
        settings.forwardDistance = camera["forwardDistance"].get<float>();
    if (camera.contains("targetFollowMode"))
        settings.targetFollowMode = static_cast<SceneCameraFollowMode>(std::clamp(camera["targetFollowMode"].get<int>(), 0, 1));
    if (camera.contains("targetFollowResponse"))
        settings.targetFollowResponse = camera["targetFollowResponse"].get<float>();
    if (camera.contains("blendInDuration"))
        settings.blendInDuration = camera["blendInDuration"].get<float>();
    if (camera.contains("blendOutDuration"))
        settings.blendOutDuration = camera["blendOutDuration"].get<float>();
    if (camera.contains("easing"))
        settings.easing = static_cast<SceneCameraEasing>(std::clamp(camera["easing"].get<int>(), 0, 4));

    // 古いJSONと項目追加前へのUndoでは、範囲だけを既定値へ戻す。
    settings.region = {};
    if (camera.contains("region") && camera["region"].is_object()) {
        const auto& data = camera["region"];
        auto& region = settings.region;
        if (data.contains("enabled") && data["enabled"].is_boolean()) region.enabled = data["enabled"].get<bool>();
        if (data.contains("priority") && data["priority"].is_number_integer())
            region.priority = std::clamp(data["priority"].get<int>(), -10000, 10000);
        auto number = [&](const char* key, float fallback, float minimum, float maximum) {
            if (!data.contains(key) || !data[key].is_number()) return fallback;
            const float value = data[key].get<float>();
            return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
        };
        if (data.contains("clearForegroundBarriers") && data["clearForegroundBarriers"].is_boolean())
            region.clearForegroundBarriers = data["clearForegroundBarriers"].get<bool>();
        region.barrierFloorHeight = number("barrierFloorHeight", 0.0f, -1000000.0f, 1000000.0f);
        region.barrierEdgeHeight = number("barrierEdgeHeight", 0.6f, 0.0f, 5.0f);
        region.barrierOpacity = number("barrierOpacity", 0.03f, 0.0f, 1.0f);
        region.activationDelay = number("activationDelay", 0.2f, 0.0f, 10.0f);
        region.exitMargin = number("exitMargin", 0.5f, 0.0f, 20.0f);
        auto vector = [&](const char* key, float* destination, int count, float minimum, float maximum) {
            if (!data.contains(key) || !data[key].is_array() || data[key].size() != count) return;
            for (int i = 0; i < count; ++i) {
                if (!data[key][i].is_number()) continue;
                const float value = data[key][i].get<float>();
                if (std::isfinite(value)) destination[i] = std::clamp(value, minimum, maximum);
            }
        };
        if (data.contains("routeEnabled") && data["routeEnabled"].is_boolean()) region.routeEnabled = data["routeEnabled"].get<bool>();
        if (data.contains("endCameraGuid") && data["endCameraGuid"].is_string()) region.endCameraGuid = data["endCameraGuid"].get<std::string>();
        vector("pathStart", &region.pathStart.x, 3, -1000000.0f, 1000000.0f);
        vector("pathEnd", &region.pathEnd.x, 3, -1000000.0f, 1000000.0f);
        vector("center", &region.center.x, 3, -1000000.0f, 1000000.0f);
        vector("size", &region.size.x, 3, 0.1f, 100000.0f);
        vector("color", &region.color.x, 4, 0.0f, 1.0f);
    }

    // 射影行列や追従補間が破綻しない範囲へ、読み取り後にまとめて補正する。
    settings.fovY = std::clamp(settings.fovY, 0.05f, 3.0f);
    settings.nearClip = (std::max)(settings.nearClip, 0.001f);
    settings.farClip = (std::max)(settings.farClip, settings.nearClip + 0.01f);
    settings.eyeFollowResponse = (std::max)(settings.eyeFollowResponse, 0.01f);
    settings.targetFollowResponse = (std::max)(settings.targetFollowResponse, 0.01f);
    settings.forwardDistance = (std::max)(settings.forwardDistance, 0.01f);
    settings.blendInDuration = (std::max)(settings.blendInDuration, 0.0f);
    settings.blendOutDuration = (std::max)(settings.blendOutDuration, 0.0f);
}
