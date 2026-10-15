#include "EditorPropertyRegistry.h"
#include "EditorPropertyRegistry.BuiltIn.h"
#include "Object3d.h"

#include <algorithm>

using nlohmann::json;
using EditorPropertyBuiltIn::CommonFlags;

void EditorPropertyRegistry::RegisterBuiltInCameraAndGameplayProperties() {
    // Cameraオブジェクトと、汎用EntityParameterを持つゲーム用Objectに編集項目を登録する。
    const auto cameraOnly = [](const engine::Object3d& object) {
        return object.IsCameraObject();
    };

    const auto gameplayParameterOnly = [](const engine::Object3d& object) {
        const std::string className = object.GetClassName();
        return object.param_.has_value() && className != "Player" && className != "Enemy";
    };

    // Cameraの有効性と役割は種類を限定し、誤った型・範囲のJSONを受け付けない。
    Register({
        "camera.enabled", "Camera有効", "Camera", EditorPropertyType::Bool, CommonFlags(),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().enabled); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_boolean()) return false;
            object.GetSceneCameraSettings().enabled = value.get<bool>();
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.role", "Camera役割", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().role)); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().role = static_cast<SceneCameraRole>(std::clamp(
                value.get<int>(), static_cast<int>(SceneCameraRole::kMain), static_cast<int>(SceneCameraRole::kCinematic)));
            return true;
        },
        cameraOnly,
        { "Main", "Cinematic" },
    });
    // 視野角とClip距離は、描画行列が破綻しない範囲へ制限する。
    Register({
        "camera.fov", "FOV", "Camera", EditorPropertyType::Number, CommonFlags(true),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().fovY); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().fovY = std::clamp(value.get<float>(), 0.05f, 3.0f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.nearClip", "Near Clip", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().nearClip); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            auto& settings = object.GetSceneCameraSettings();
            settings.nearClip = (std::max)(value.get<float>(), 0.001f);
            settings.farClip = (std::max)(settings.farClip, settings.nearClip + 0.01f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.farClip", "Far Clip", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().farClip); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            auto& settings = object.GetSceneCameraSettings();
            settings.farClip = (std::max)(value.get<float>(), settings.nearClip + 0.01f);
            return true;
        },
        cameraOnly,
    });
    // 視点の取得元、追従対象、オフセット、追従速度をひとまとまりで公開する。
    Register({
        "camera.eyeSource", "Eye取得元", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().eyeSource)); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().eyeSource = static_cast<SceneCameraEyeSource>(std::clamp(
                value.get<int>(), static_cast<int>(SceneCameraEyeSource::kCameraObject), static_cast<int>(SceneCameraEyeSource::kSceneObject)));
            return true;
        },
        cameraOnly,
        { "Camera Transform", "Scene Object" },
    });
    Register({
        "camera.eyeObject", "Eye追従Object", "Camera", EditorPropertyType::String, CommonFlags(),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().eyeObjectName); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.GetSceneCameraSettings().eyeObjectName = value.get<std::string>();
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.eyeOffset", "Eye Offset", "Camera", EditorPropertyType::Vector3, CommonFlags(true),
        [](const engine::Object3d& object) {
            const engine::Vector3& value = object.GetSceneCameraSettings().eyeOffset;
            return json::array({ value.x, value.y, value.z });
        },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            object.GetSceneCameraSettings().eyeOffset = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.eyeFollowMode", "Eye追従方式", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().eyeFollowMode)); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().eyeFollowMode =
                static_cast<SceneCameraFollowMode>(std::clamp(
                    value.get<int>(), static_cast<int>(SceneCameraFollowMode::kSnap), static_cast<int>(SceneCameraFollowMode::kSmooth)));
            return true;
        },
        cameraOnly,
        { "完全追従", "Smooth" },
    });
    Register({
        "camera.eyeFollowResponse", "Eye追従レスポンス", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().eyeFollowResponse); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().eyeFollowResponse = (std::max)(value.get<float>(), 0.01f);
            return true;
        },
        cameraOnly,
    });
    // 注視点は固定点、シーン内Object、カメラ前方から選び、追従方法を別に指定する。
    Register({
        "camera.targetMode", "注視方法", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().targetMode)); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().targetMode = static_cast<SceneCameraTargetMode>(std::clamp(
                value.get<int>(), static_cast<int>(SceneCameraTargetMode::kFixedPoint), static_cast<int>(SceneCameraTargetMode::kCameraForward)));
            return true;
        },
        cameraOnly,
        { "固定座標", "Scene Object", "Camera前方" },
    });
    Register({
        "camera.targetObject", "注視Object", "Camera", EditorPropertyType::String, CommonFlags(),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().targetObjectName); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_string()) return false;
            object.GetSceneCameraSettings().targetObjectName = value.get<std::string>();
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.targetOffset", "Target Offset", "Camera", EditorPropertyType::Vector3, CommonFlags(true),
        [](const engine::Object3d& object) {
            const engine::Vector3& value = object.GetSceneCameraSettings().targetOffset;
            return json::array({ value.x, value.y, value.z });
        },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            object.GetSceneCameraSettings().targetOffset = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.fixedTarget", "固定注視点", "Camera", EditorPropertyType::Vector3, CommonFlags(true),
        [](const engine::Object3d& object) {
            const engine::Vector3& value = object.GetSceneCameraSettings().fixedTarget;
            return json::array({ value.x, value.y, value.z });
        },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_array() || value.size() < 3) return false;
            object.GetSceneCameraSettings().fixedTarget = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.forwardDistance", "前方距離", "Camera", EditorPropertyType::Number, CommonFlags(true),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().forwardDistance); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().forwardDistance = (std::max)(value.get<float>(), 0.01f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.targetFollowMode", "Target追従方式", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().targetFollowMode)); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().targetFollowMode =
                static_cast<SceneCameraFollowMode>(std::clamp(
                    value.get<int>(), static_cast<int>(SceneCameraFollowMode::kSnap), static_cast<int>(SceneCameraFollowMode::kSmooth)));
            return true;
        },
        cameraOnly,
        { "完全追従", "Smooth" },
    });
    Register({
        "camera.targetFollowResponse", "Target追従レスポンス", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().targetFollowResponse); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().targetFollowResponse = (std::max)(value.get<float>(), 0.01f);
            return true;
        },
        cameraOnly,
    });
    // 演出への出入り時間とEasingは、視点と注視点の計算結果を合成する際に使う。
    Register({
        "camera.blendIn", "Blend In", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().blendInDuration); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().blendInDuration = (std::max)(value.get<float>(), 0.0f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.blendOut", "Blend Out", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3d& object) { return json(object.GetSceneCameraSettings().blendOutDuration); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number()) return false;
            object.GetSceneCameraSettings().blendOutDuration = (std::max)(value.get<float>(), 0.0f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.easing", "切替Easing", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3d& object) { return json(static_cast<int>(object.GetSceneCameraSettings().easing)); },
        [](engine::Object3d& object, const json& value) {
            if (!value.is_number_integer()) return false;
            object.GetSceneCameraSettings().easing = static_cast<SceneCameraEasing>(std::clamp(
                value.get<int>(), static_cast<int>(SceneCameraEasing::kLinear), static_cast<int>(SceneCameraEasing::kSmootherStep)));
            return true;
        },
        cameraOnly,
        { "Linear", "Ease In", "Ease Out", "Ease In Out", "Smoother Step" },
    });

    // EntityParameterの数値・整数・真偽値・文字列は同じ検証と読み書き手順で登録する。
    // 対象がPlayer/Enemyではないことは冒頭の述語で確認する。
    auto registerParamNumber = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        float engine::Object3d::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::Number, CommonFlags(true),
            [member](const engine::Object3d& object) { return json(object.param_.value().*member); },
            [member](engine::Object3d& object, const json& value) {
                if (!value.is_number() || !object.param_.has_value()) return false;
                object.param_.value().*member = value.get<float>();
                return true;
            },
            gameplayParameterOnly,
        });
    };
    auto registerParamInteger = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        int engine::Object3d::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::Integer, CommonFlags(),
            [member](const engine::Object3d& object) { return json(object.param_.value().*member); },
            [member](engine::Object3d& object, const json& value) {
                if (!value.is_number_integer() || !object.param_.has_value()) return false;
                object.param_.value().*member = value.get<int>();
                return true;
            },
            gameplayParameterOnly,
        });
    };
    auto registerParamBool = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        bool engine::Object3d::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::Bool, CommonFlags(),
            [member](const engine::Object3d& object) { return json(object.param_.value().*member); },
            [member](engine::Object3d& object, const json& value) {
                if (!value.is_boolean() || !object.param_.has_value()) return false;
                object.param_.value().*member = value.get<bool>();
                return true;
            },
            gameplayParameterOnly,
        });
    };
    auto registerParamString = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        std::string engine::Object3d::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::String, CommonFlags(),
            [member](const engine::Object3d& object) { return json(object.param_.value().*member); },
            [member](engine::Object3d& object, const json& value) {
                if (!value.is_string() || !object.param_.has_value()) return false;
                object.param_.value().*member = value.get<std::string>();
                return true;
            },
            gameplayParameterOnly,
        });
    };

    // パスはEditorから参照する安定ID。表示名を変えても保存項目との対応は維持する。
    registerParamNumber("gameplay.healAmount", "回復量", &engine::Object3d::EntityParameter::healAmount);
    registerParamNumber("gameplay.interval", "間隔", &engine::Object3d::EntityParameter::interval);
    registerParamInteger("gameplay.maxCount", "最大数", &engine::Object3d::EntityParameter::maxCount);
    registerParamNumber("gameplay.detectionRange", "感知範囲", &engine::Object3d::EntityParameter::detectionRange);
    registerParamNumber("gameplay.shakeDuration", "揺れ時間", &engine::Object3d::EntityParameter::shakeDuration);
    registerParamNumber("gameplay.fallDuration", "落下時間", &engine::Object3d::EntityParameter::fallDuration);
    registerParamInteger("gameplay.colorType", "色Type", &engine::Object3d::EntityParameter::colorType);
    registerParamInteger("gameplay.switchMode", "Switch Mode", &engine::Object3d::EntityParameter::switchMode);
    registerParamInteger("gameplay.actionMode", "Action Mode", &engine::Object3d::EntityParameter::actionMode);
    registerParamString("gameplay.targetScene", "移動先Scene", &engine::Object3d::EntityParameter::targetScene);
    registerParamNumber("gameplay.moveAmount", "移動量", &engine::Object3d::EntityParameter::moveAmount);
    registerParamNumber("gameplay.moveSpeed", "移動速度", &engine::Object3d::EntityParameter::moveSpeed);
    registerParamBool("gameplay.startActive", "開始時有効", &engine::Object3d::EntityParameter::startActive);
    registerParamBool("gameplay.returnOnOff", "OFFで戻る", &engine::Object3d::EntityParameter::returnOnOff);
}
