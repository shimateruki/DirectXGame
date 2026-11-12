#define NOMINMAX
#include "game/system/navigation/DestinationGuide.h"
#include "Object3D.h"
#include "engine/utility/math/AnimationInterpolation.h"
#include "json.hpp"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
float SafeRange(float value, float fallback, float lo, float hi) {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}
bool Finite(const engine::Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

void DestinationGuide::Initialize(const Settings& settings) {
    SetSettings(settings);
    Reset();
}

void DestinationGuide::SetSettings(const Settings& settings) {
    settings_ = settings;
    const Settings defaults;
    settings_.distance = SafeRange(settings.distance, defaults.distance, 0.0f, 15.0f);
    settings_.height = SafeRange(settings.height, defaults.height, 0.0f, 10.0f);
    settings_.scale = SafeRange(settings.scale, defaults.scale, 0.05f, 5.0f);
    settings_.followResponse = SafeRange(settings.followResponse, defaults.followResponse, 0.1f, 80.0f);
    settings_.turnResponse = SafeRange(settings.turnResponse, defaults.turnResponse, 0.1f, 80.0f);
    settings_.bobAmplitude = SafeRange(settings.bobAmplitude, defaults.bobAmplitude, 0.0f, 1.0f);
    settings_.bobSpeed = SafeRange(settings.bobSpeed, defaults.bobSpeed, 0.0f, 15.0f);
    settings_.arrivalDistance = SafeRange(settings.arrivalDistance, defaults.arrivalDistance, 0.1f, 20.0f);
    settings_.arrivalHeight = SafeRange(settings.arrivalHeight, defaults.arrivalHeight, 0.1f, 20.0f);
}

void DestinationGuide::Reset() {
    position_ = {};
    yaw_ = animationTime_ = 0.0f;
    poseInitialized_ = arrived_ = false;
}

void DestinationGuide::Hide(engine::Object3D& arrow) {
    arrow.SetIsVisible(false);
    poseInitialized_ = false;
}

void DestinationGuide::Update(engine::Object3D& arrow, const engine::Vector3& followerPosition,
                              const engine::Vector3& targetPosition, float deltaTime) {
    if (!settings_.enabled || arrived_ || !Finite(followerPosition) || !Finite(targetPosition)) {
        Hide(arrow);
        return;
    }
    // 停止中は表示状態も補間時計も進めない。
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f)
        return;
    engine::Vector3 direction = targetPosition - followerPosition;
    direction.y = 0.0f;
    const float distance = std::hypot(direction.x, direction.z);
    if (distance <= settings_.arrivalDistance &&
        std::abs(targetPosition.y - followerPosition.y) <= settings_.arrivalHeight) {
        arrived_ = true;
        Hide(arrow);
        return;
    }
    // 真下・真上では水平の方角を定義できないため、不安定な回転を表示しない。
    if (distance < 0.001f) {
        Hide(arrow);
        return;
    }
    direction = direction / distance;
    // 遠方に離す設定でも、目的地の向こう側へ矢印を置かない。
    auto desired = followerPosition + direction * (std::min)(settings_.distance, distance * 0.5f);
    desired.y = followerPosition.y + settings_.height;
    const auto delta = desired - position_;
    if (!poseInitialized_ || std::hypot(delta.x, delta.z) > 20.0f) {
        position_ = desired;
        yaw_ = std::atan2(direction.x, direction.z);
        poseInitialized_ = true;
    } else {
        position_ = AnimationInterpolation::Damp(position_, desired, settings_.followResponse, deltaTime);
    }
    const auto toTarget = targetPosition - position_;
    if (std::hypot(toTarget.x, toTarget.z) > 0.001f)
        yaw_ = AnimationInterpolation::DampAngle(yaw_, std::atan2(toTarget.x, toTarget.z), settings_.turnResponse, deltaTime);
    animationTime_ = std::fmod(animationTime_ + deltaTime, 3600.0f);
    auto displayPosition = position_;
    displayPosition.y += std::sin(animationTime_ * settings_.bobSpeed) * settings_.bobAmplitude;
    arrow.SetTranslate(displayPosition);
    arrow.SetRotation({0.0f, yaw_, 0.0f});
    arrow.SetScale({settings_.scale, settings_.scale, settings_.scale});
    arrow.SetIsVisible(true);
    // Sceneの物理更新後に呼ぶため、このモデルだけ描画定数まで確定する。
    arrow.UpdateWorldMatrix(true);
}

bool DestinationGuide::LoadSettings(const std::string& path) {
    try {
        std::ifstream stream{std::filesystem::path(std::u8string(path.begin(), path.end()))};
        if (!stream)
            return false;
        const auto data = nlohmann::json::parse(stream);
        if (!data.is_object())
            return false;
        // 読み取りを全て終えてから反映し、壊れたJSONでは編集中の値を維持する。
        Settings loaded = settings_;
        loaded.enabled = data.value("enabled", loaded.enabled);
        loaded.distance = data.value("distance", loaded.distance);
        loaded.height = data.value("height", loaded.height);
        loaded.scale = data.value("scale", loaded.scale);
        loaded.followResponse = data.value("followResponse", loaded.followResponse);
        loaded.turnResponse = data.value("turnResponse", loaded.turnResponse);
        loaded.bobAmplitude = data.value("bobAmplitude", loaded.bobAmplitude);
        loaded.bobSpeed = data.value("bobSpeed", loaded.bobSpeed);
        loaded.arrivalDistance = data.value("arrivalDistance", loaded.arrivalDistance);
        loaded.arrivalHeight = data.value("arrivalHeight", loaded.arrivalHeight);
        SetSettings(loaded);
        return true;
    } catch (...) {
        return false;
    }
}

bool DestinationGuide::SaveSettings(const std::string& path) const {
    try {
        const auto destination = std::filesystem::path(std::u8string(path.begin(), path.end()));
        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path());
        auto temporary = destination;
        temporary += L".tmp";
        nlohmann::json data = {{"schemaVersion", 1}, {"enabled", settings_.enabled}};
        data["distance"] = settings_.distance;
        data["height"] = settings_.height;
        data["scale"] = settings_.scale;
        data["followResponse"] = settings_.followResponse;
        data["turnResponse"] = settings_.turnResponse;
        data["bobAmplitude"] = settings_.bobAmplitude;
        data["bobSpeed"] = settings_.bobSpeed;
        data["arrivalDistance"] = settings_.arrivalDistance;
        data["arrivalHeight"] = settings_.arrivalHeight;
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream << data.dump(2) << '\n';
            stream.flush();
            if (!stream)
                return false;
        }
        // 保存途中のファイルを設定として読ませない。
        return MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    } catch (...) {
        return false;
    }
}

#ifdef USE_IMGUI
void DestinationGuide::DrawImGui(const std::string& settingsPath) {
    Settings edited = settings_;
    bool changed = ImGui::Checkbox("矢印を有効にする", &edited.enabled);
    changed |= ImGui::DragFloat("追従距離", &edited.distance, 0.05f, 0.0f, 15.0f);
    changed |= ImGui::DragFloat("足元からの高さ", &edited.height, 0.05f, 0.0f, 10.0f);
    changed |= ImGui::DragFloat("矢印の大きさ", &edited.scale, 0.01f, 0.05f, 5.0f);
    changed |= ImGui::DragFloat("位置の追従速度", &edited.followResponse, 0.2f, 0.1f, 80.0f);
    changed |= ImGui::DragFloat("旋回速度", &edited.turnResponse, 0.2f, 0.1f, 80.0f);
    changed |= ImGui::DragFloat("浮遊の振幅", &edited.bobAmplitude, 0.01f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("浮遊の速さ", &edited.bobSpeed, 0.05f, 0.0f, 15.0f);
    changed |= ImGui::DragFloat("到着とみなす距離", &edited.arrivalDistance, 0.05f, 0.1f, 20.0f);
    changed |= ImGui::DragFloat("到着時の許容高低差", &edited.arrivalHeight, 0.05f, 0.1f, 20.0f);
    if (changed)
        SetSettings(edited);
    ImGui::Text("到着状態: %s", arrived_ ? "到着済み・非表示" : "案内待機／案内中");
    if (ImGui::Button("矢印設定を保存"))
        settingsMessage_ = SaveSettings(settingsPath) ? "矢印設定を保存しました。" : "保存に失敗しました。現在値は保持します。";
    ImGui::SameLine();
    if (ImGui::Button("矢印設定を再読込"))
        settingsMessage_ = LoadSettings(settingsPath) ? "矢印設定を読み込みました。" : "読込に失敗しました。現在値は保持します。";
    if (ImGui::Button("到着状態をリセット"))
        Reset();
    ImGui::TextWrapped("%s", settingsMessage_.c_str());
    ImGui::TextDisabled("保存先: %s", settingsPath.c_str());
}
#endif