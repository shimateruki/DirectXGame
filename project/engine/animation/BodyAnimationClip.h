#pragma once

#include "engine/utility/math/Math.h"

#include <string>
#include <vector>

// Animation Workbenchで作成した単体Object用の見た目Animationを保持します。
// PositionはVisual Offsetとして保存し、物理座標へ適用するかは利用側が決定します。
class BodyAnimationClip {
public:
    static constexpr int kCurrentVersion = 2;

    struct Key {
        float time = 0.0f;
        Vector3 translate = { 0.0f, 0.0f, 0.0f };
        Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
        int easingToNext = 4;
    };

    struct Sample {
        Vector3 translate = { 0.0f, 0.0f, 0.0f };
        Vector3 rotate = { 0.0f, 0.0f, 0.0f };
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
    };

    void Clear();
    bool Load(const std::string& filePath);
    bool Evaluate(float timeSeconds, bool loop, Sample& sampleOut) const;

    bool IsValid() const { return !keys_.empty() && duration_ > 0.0f; }
    float GetDuration() const { return duration_; }
    bool IsLoopDefault() const { return loopDefault_; }
    const std::string& GetName() const { return name_; }
    const std::string& GetModelName() const { return modelName_; }
    const std::vector<Key>& GetKeys() const { return keys_; }

    // Asset名だけを受け取った場合は共通Directoryへ解決します。
    static std::string ResolveAssetPath(const std::string& assetNameOrPath);

private:
    std::string name_;
    std::string modelName_;
    float duration_ = 0.0f;
    bool loopDefault_ = true;
    std::vector<Key> keys_;
};
