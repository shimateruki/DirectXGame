#include "EditorPropertyRegistry.h"
#include "EditorPropertyRegistry.BuiltIn.h"
#include "Object3D.h"

#include <algorithm>

using nlohmann::json;
using EditorPropertyBuiltIn::CommonFlags;

void EditorPropertyRegistry::RegisterBuiltInCameraAndGameplayProperties() {
    // Cameraオブジェクトと、汎用EntityParameterを持つゲーム用Objectに編集項目を登録する。
    const auto cameraOnly = [](const engine::Object3D& object) {
        return object.IsCameraObject();
    };

    const auto gameplayParameterOnly = [](const engine::Object3D& object) {
        const std::string className = object.GetClassName();
        return object.GetParameters().has_value() && className != "Player" && className != "Enemy";
    };

    // Cameraの有効性と役割は種類を限定し、誤った型・範囲のJSONを受け付けない。
    Register({
        "camera.enabled", "Camera有効", "Camera", EditorPropertyType::Bool, CommonFlags(),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().enabled); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_boolean())
                return false;
            object.GetSceneCameraSettings().enabled = value.get<bool>();
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.role", "Camera役割", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3D& object) { return json(static_cast<int>(object.GetSceneCameraSettings().role)); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number_integer())
                return false;
            object.GetSceneCameraSettings().role = static_cast<SceneCameraRole>(std::clamp(
                value.get<int>(), static_cast<int>(SceneCameraRole::Main), static_cast<int>(SceneCameraRole::Cinematic)));
            return true;
        },
        cameraOnly,
        { "Main", "Cinematic" },
    });

    // 共通の保存読込で検証し、専用InspectorとPrefab編集で範囲の制限を揃える。
    auto registerRegion = [&](const std::string& key, const std::string& label, EditorPropertyType type) {
        Register({"camera.region." + key, label, "Camera", type, CommonFlags(),
            [key](const engine::Object3D& object) { return SerializeSceneCameraSettings(object.GetSceneCameraSettings())["region"][key]; },
            [key, type](engine::Object3D& object, const json& value) {
                const int dimensions = type == EditorPropertyType::Vector3 ? 3 : type == EditorPropertyType::Vector4 ? 4 : 0;
                if (dimensions) {
                    if (!value.is_array() || value.size() != dimensions) return false;
                    for (const auto& component : value) if (!component.is_number()) return false;
                } else if (type == EditorPropertyType::Bool ? !value.is_boolean() :
                    type == EditorPropertyType::Integer ? !value.is_number_integer() : !value.is_number()) return false;
                json camera = SerializeSceneCameraSettings(object.GetSceneCameraSettings());
                camera["region"][key] = value;
                DeserializeSceneCameraSettings(camera, object.GetSceneCameraSettings());
                return true;
            }, cameraOnly});
    };
    registerRegion("enabled", "範囲内で自動適用", EditorPropertyType::Bool);
    registerRegion("center", "範囲の中心", EditorPropertyType::Vector3);
    registerRegion("size", "範囲の大きさ", EditorPropertyType::Vector3);
    registerRegion("color", "範囲の色", EditorPropertyType::Vector4);
    registerRegion("priority", "範囲の優先度", EditorPropertyType::Integer);
    registerRegion("activationDelay", "範囲の切替待ち時間", EditorPropertyType::Number);
    registerRegion("exitMargin", "範囲の退出余白", EditorPropertyType::Number);
    registerRegion("clearForegroundBarriers", "手前バリアの上部を透過", EditorPropertyType::Bool);
    registerRegion("barrierFloorHeight", "バリアの床高さ", EditorPropertyType::Number);
    registerRegion("barrierEdgeHeight", "バリアの足元に残す高さ", EditorPropertyType::Number);
    registerRegion("barrierOpacity", "手前バリアの不透明度", EditorPropertyType::Number);

    // 視野角とClip距離は、描画行列が破綻しない範囲へ制限する。
    Register({
        "camera.fov", "FOV", "Camera", EditorPropertyType::Number, CommonFlags(true),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().fovY); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number())
                return false;
            object.GetSceneCameraSettings().fovY = std::clamp(value.get<float>(), 0.05f, 3.0f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.nearClip", "Near Clip", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().nearClip); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number())
                return false;
            auto& settings = object.GetSceneCameraSettings();
            settings.nearClip = (std::max)(value.get<float>(), 0.001f);
            settings.farClip = (std::max)(settings.farClip, settings.nearClip + 0.01f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.farClip", "Far Clip", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().farClip); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number())
                return false;
            auto& settings = object.GetSceneCameraSettings();
            settings.farClip = (std::max)(value.get<float>(), settings.nearClip + 0.01f);
            return true;
        },
        cameraOnly,
    });

    // 視点の取得元、追従対象、オフセット、追従速度をひとまとまりで公開する。
    Register({
        "camera.eyeSource", "Eye取得元", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3D& object) { return json(static_cast<int>(object.GetSceneCameraSettings().eyeSource)); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number_integer())
                return false;
            object.GetSceneCameraSettings().eyeSource = static_cast<SceneCameraEyeSource>(std::clamp(
                value.get<int>(), static_cast<int>(SceneCameraEyeSource::CameraObject), static_cast<int>(SceneCameraEyeSource::SceneObjectRelativeToReference)));
            return true;
        },
        cameraOnly,
        { "Camera Transform", "Scene Object", "参照Object方向" },
    });
    Register({
        "camera.eyeObject", "Eye追従Object", "Camera", EditorPropertyType::String, CommonFlags(),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().eyeObjectName); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_string())
                return false;
            object.GetSceneCameraSettings().eyeObjectName = value.get<std::string>();
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.eyeOffset", "Eye Offset", "Camera", EditorPropertyType::Vector3, CommonFlags(true),
        [](const engine::Object3D& object) {
            const engine::Vector3& value = object.GetSceneCameraSettings().eyeOffset;
            return json::array({ value.x, value.y, value.z });
        },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_array() || value.size() < 3)
                return false;
            object.GetSceneCameraSettings().eyeOffset = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.eyeFollowMode", "Eye追従方式", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3D& object) { return json(static_cast<int>(object.GetSceneCameraSettings().eyeFollowMode)); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number_integer())
                return false;
            object.GetSceneCameraSettings().eyeFollowMode =
                static_cast<SceneCameraFollowMode>(std::clamp(
                    value.get<int>(), static_cast<int>(SceneCameraFollowMode::Snap), static_cast<int>(SceneCameraFollowMode::Smooth)));
            return true;
        },
        cameraOnly,
        { "完全追従", "Smooth" },
    });
    Register({
        "camera.eyeFollowResponse", "Eye追従レスポンス", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().eyeFollowResponse); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number())
                return false;
            object.GetSceneCameraSettings().eyeFollowResponse = (std::max)(value.get<float>(), 0.01f);
            return true;
        },
        cameraOnly,
    });

    // 注視点は固定点、シーン内Object、カメラ前方から選び、追従方法を別に指定する。
    Register({
        "camera.targetMode", "注視方法", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3D& object) { return json(static_cast<int>(object.GetSceneCameraSettings().targetMode)); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number_integer())
                return false;
            object.GetSceneCameraSettings().targetMode = static_cast<SceneCameraTargetMode>(std::clamp(
                value.get<int>(), static_cast<int>(SceneCameraTargetMode::FixedPoint), static_cast<int>(SceneCameraTargetMode::CameraForward)));
            return true;
        },
        cameraOnly,
        { "固定座標", "Scene Object", "Camera前方" },
    });
    Register({
        "camera.targetObject", "注視Object", "Camera", EditorPropertyType::String, CommonFlags(),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().targetObjectName); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_string())
                return false;
            object.GetSceneCameraSettings().targetObjectName = value.get<std::string>();
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.targetOffset", "Target Offset", "Camera", EditorPropertyType::Vector3, CommonFlags(true),
        [](const engine::Object3D& object) {
            const engine::Vector3& value = object.GetSceneCameraSettings().targetOffset;
            return json::array({ value.x, value.y, value.z });
        },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_array() || value.size() < 3)
                return false;
            object.GetSceneCameraSettings().targetOffset = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.fixedTarget", "固定注視点", "Camera", EditorPropertyType::Vector3, CommonFlags(true),
        [](const engine::Object3D& object) {
            const engine::Vector3& value = object.GetSceneCameraSettings().fixedTarget;
            return json::array({ value.x, value.y, value.z });
        },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_array() || value.size() < 3)
                return false;
            object.GetSceneCameraSettings().fixedTarget = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.forwardDistance", "前方距離", "Camera", EditorPropertyType::Number, CommonFlags(true),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().forwardDistance); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number())
                return false;
            object.GetSceneCameraSettings().forwardDistance = (std::max)(value.get<float>(), 0.01f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.targetFollowMode", "Target追従方式", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3D& object) { return json(static_cast<int>(object.GetSceneCameraSettings().targetFollowMode)); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number_integer())
                return false;
            object.GetSceneCameraSettings().targetFollowMode =
                static_cast<SceneCameraFollowMode>(std::clamp(
                    value.get<int>(), static_cast<int>(SceneCameraFollowMode::Snap), static_cast<int>(SceneCameraFollowMode::Smooth)));
            return true;
        },
        cameraOnly,
        { "完全追従", "Smooth" },
    });
    Register({
        "camera.targetFollowResponse", "Target追従レスポンス", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().targetFollowResponse); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number())
                return false;
            object.GetSceneCameraSettings().targetFollowResponse = (std::max)(value.get<float>(), 0.01f);
            return true;
        },
        cameraOnly,
    });

    // 演出への出入り時間とEasingは、視点と注視点の計算結果を合成する際に使う。
    Register({
        "camera.blendIn", "Blend In", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().blendInDuration); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number())
                return false;
            object.GetSceneCameraSettings().blendInDuration = (std::max)(value.get<float>(), 0.0f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.blendOut", "Blend Out", "Camera", EditorPropertyType::Number, CommonFlags(),
        [](const engine::Object3D& object) { return json(object.GetSceneCameraSettings().blendOutDuration); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number())
                return false;
            object.GetSceneCameraSettings().blendOutDuration = (std::max)(value.get<float>(), 0.0f);
            return true;
        },
        cameraOnly,
    });
    Register({
        "camera.easing", "切替Easing", "Camera", EditorPropertyType::Integer, CommonFlags(),
        [](const engine::Object3D& object) { return json(static_cast<int>(object.GetSceneCameraSettings().easing)); },
        [](engine::Object3D& object, const json& value) {
            if (!value.is_number_integer())
                return false;
            object.GetSceneCameraSettings().easing = static_cast<SceneCameraEasing>(std::clamp(
                value.get<int>(), static_cast<int>(SceneCameraEasing::Linear), static_cast<int>(SceneCameraEasing::SmootherStep)));
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
        float engine::Object3D::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::Number, CommonFlags(true),
            [member](const engine::Object3D& object) { return json(object.GetParameters().value().*member); },
            [member](engine::Object3D& object, const json& value) {
                if (!value.is_number() || !object.GetParameters().has_value())
                    return false;
                object.SetParameter(member, value.get<float>());
                return true;
            },
            gameplayParameterOnly,
        });
    };

    auto registerParamInteger = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        int engine::Object3D::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::Integer, CommonFlags(),
            [member](const engine::Object3D& object) { return json(object.GetParameters().value().*member); },
            [member](engine::Object3D& object, const json& value) {
                if (!value.is_number_integer() || !object.GetParameters().has_value())
                    return false;
                object.SetParameter(member, value.get<int>());
                return true;
            },
            gameplayParameterOnly,
        });
    };

    auto registerParamBool = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        bool engine::Object3D::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::Bool, CommonFlags(),
            [member](const engine::Object3D& object) { return json(object.GetParameters().value().*member); },
            [member](engine::Object3D& object, const json& value) {
                if (!value.is_boolean() || !object.GetParameters().has_value())
                    return false;
                object.SetParameter(member, value.get<bool>());
                return true;
            },
            gameplayParameterOnly,
        });
    };

    auto registerParamString = [this, gameplayParameterOnly](
        const char* path,
        const char* displayName,
        std::string engine::Object3D::EntityParameter::* member) {
        Register({
            path, displayName, "Gameplay", EditorPropertyType::String, CommonFlags(),
            [member](const engine::Object3D& object) { return json(object.GetParameters().value().*member); },
            [member](engine::Object3D& object, const json& value) {
                if (!value.is_string() || !object.GetParameters().has_value())
                    return false;
                object.SetParameter(member, value.get<std::string>());
                return true;
            },
            gameplayParameterOnly,
        });
    };

    // パスはEditorから参照する安定ID。表示名を変えても保存項目との対応は維持する。
    registerParamNumber("gameplay.healAmount", "回復量", &engine::EntityParameter::healAmount);
    registerParamNumber("gameplay.interval", "間隔", &engine::EntityParameter::interval);
    registerParamInteger("gameplay.maxCount", "最大数", &engine::EntityParameter::maxCount);
    registerParamNumber("gameplay.detectionRange", "感知範囲", &engine::EntityParameter::detectionRange);
    registerParamNumber("gameplay.shakeDuration", "揺れ時間", &engine::EntityParameter::shakeDuration);
    registerParamNumber("gameplay.fallDuration", "落下時間", &engine::EntityParameter::fallDuration);
    registerParamInteger("gameplay.colorType", "色Type", &engine::EntityParameter::colorType);
    registerParamInteger("gameplay.switchMode", "Switch Mode", &engine::EntityParameter::switchMode);
    registerParamInteger("gameplay.actionMode", "Action Mode", &engine::EntityParameter::actionMode);
    registerParamString("gameplay.targetScene", "移動先Scene", &engine::EntityParameter::targetScene);
    registerParamNumber("gameplay.moveAmount", "移動量", &engine::EntityParameter::moveAmount);
    registerParamNumber("gameplay.moveSpeed", "移動速度", &engine::EntityParameter::moveSpeed);
    registerParamNumber("gameplay.ringWaveSpeed", "リング拡大速度", &engine::EntityParameter::ringWaveSpeed);
    registerParamBool("gameplay.startActive", "開始時有効", &engine::EntityParameter::startActive);
    registerParamBool("gameplay.returnOnOff", "OFFで戻る", &engine::EntityParameter::returnOnOff);
}
