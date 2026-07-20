#define NOMINMAX
#include "BodyAnimationClip.h"

#include "engine/utility/math/AnimationInterpolation.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace {
constexpr const char* kBodyAnimationDirectory = "Resources/json/animation_clip/";
constexpr const char* kLegacyAnimationDirectory = "Resources/json/enemy_animation/";
constexpr float kTimeEpsilon = 0.0001f;

Vector3 ReadVector3(const json& object, const char* key, const Vector3& fallback) {
    if (!object.contains(key) || !object[key].is_array() || object[key].size() < 3) {
        return fallback;
    }
    const auto& values = object[key];
    return {
        values[0].get<float>(),
        values[1].get<float>(),
        values[2].get<float>()
    };
}

AnimationInterpolation::EasingType ToEasing(int value) {
    return static_cast<AnimationInterpolation::EasingType>(std::clamp(value, 0, 4));
}
}

void BodyAnimationClip::Clear() {
    name_.clear();
    modelName_.clear();
    duration_ = 0.0f;
    loopDefault_ = true;
    keys_.clear();
}

bool BodyAnimationClip::Load(const std::string& filePath) {
    Clear();

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    try {
        json root;
        file >> root;

        name_ = root.value("name", std::filesystem::path(filePath).stem().string());
        modelName_ = root.value("model", "");
        duration_ = std::max(0.01f, root.value("duration", 1.0f));
        loopDefault_ = root.value("loop", true);

        const json bodyKeys = root.value("bodyKeys", json::array());
        for (const auto& item : bodyKeys) {
            Key key;
            key.time = std::max(0.0f, item.value("time", 0.0f));
            key.translate = ReadVector3(item, "translate", {});
            key.rotate = ReadVector3(item, "rotate", {});
            key.scale = ReadVector3(item, "scale", { 1.0f, 1.0f, 1.0f });
            key.easingToNext = std::clamp(item.value("easingToNext", 4), 0, 4);
            keys_.push_back(key);
        }

        std::sort(keys_.begin(), keys_.end(), [](const Key& lhs, const Key& rhs) {
            return lhs.time < rhs.time;
        });
        if (!keys_.empty()) {
            duration_ = std::max(duration_, keys_.back().time);
        }
    } catch (...) {
        Clear();
        return false;
    }

    return IsValid();
}

bool BodyAnimationClip::Evaluate(float timeSeconds, bool loop, Sample& sampleOut) const {
    sampleOut = {};
    if (!IsValid()) {
        return false;
    }

    float sampleTime = std::max(0.0f, timeSeconds);
    if (loop && duration_ > kTimeEpsilon) {
        sampleTime = std::fmod(sampleTime, duration_);
        if (sampleTime < 0.0f) {
            sampleTime += duration_;
        }
    } else {
        sampleTime = std::min(sampleTime, duration_);
    }

    const auto CopyKeyToSample = [&sampleOut](const Key& key) {
        sampleOut.translate = key.translate;
        sampleOut.rotate = key.rotate;
        sampleOut.scale = key.scale;
    };

    if (keys_.size() == 1 || sampleTime <= keys_.front().time) {
        CopyKeyToSample(keys_.front());
        return true;
    }
    if (sampleTime >= keys_.back().time) {
        CopyKeyToSample(keys_.back());
        return true;
    }

    for (size_t index = 0; index + 1 < keys_.size(); ++index) {
        const Key& from = keys_[index];
        const Key& to = keys_[index + 1];
        if (sampleTime < from.time || sampleTime > to.time) {
            continue;
        }

        const float segmentDuration = std::max(kTimeEpsilon, to.time - from.time);
        const float rawRate = (sampleTime - from.time) / segmentDuration;
        const float rate = AnimationInterpolation::ApplyEasing(rawRate, ToEasing(from.easingToNext));
        sampleOut.translate = AnimationInterpolation::Lerp(from.translate, to.translate, rate);
        sampleOut.rotate = AnimationInterpolation::SlerpEuler(from.rotate, to.rotate, rate);
        sampleOut.scale = AnimationInterpolation::Lerp(from.scale, to.scale, rate);
        return true;
    }

    CopyKeyToSample(keys_.back());
    return true;
}

std::string BodyAnimationClip::ResolveAssetPath(const std::string& assetNameOrPath) {
    if (assetNameOrPath.empty()) {
        return {};
    }

    std::filesystem::path path(assetNameOrPath);
    if (path.has_parent_path()) {
        if (path.extension().empty()) {
            path += ".json";
        }
        return path.generic_string();
    }

    std::string fileName = path.string();
    if (path.extension().empty()) {
        fileName += ".json";
    }

    const std::filesystem::path primary = std::filesystem::path(kBodyAnimationDirectory) / fileName;
    if (std::filesystem::exists(primary)) {
        return primary.generic_string();
    }

    const std::filesystem::path legacy = std::filesystem::path(kLegacyAnimationDirectory) / fileName;
    if (std::filesystem::exists(legacy)) {
        return legacy.generic_string();
    }
    return primary.generic_string();
}
