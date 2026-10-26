#pragma once
#include "engine/utility/math/Math.h"
#include <string>

namespace engine { class Object3d; }

// 追従位置と目的地の座標だけを受け取り、ゲーム進行とは独立して案内モデルを動かす。
class DestinationGuide {
public:
    struct Settings {
        bool enabled = true;
        float distance = 3.0f;
        float height = 0.8f;
        float scale = 0.55f;
        float followResponse = 24.0f;
        float turnResponse = 12.0f;
        float bobAmplitude = 0.1f;
        float bobSpeed = 3.0f;
        float arrivalDistance = 4.0f;
        float arrivalHeight = 3.0f;
    };

    void Initialize(const Settings& settings);
    void Reset();
    // 一時非表示では到着済みを解除しない。次の案内を始める側がResetする。
    void Hide(engine::Object3d& arrow);
    void Update(engine::Object3d& arrow, const engine::Vector3& followerPosition,
                const engine::Vector3& targetPosition, float deltaTime);
    bool HasArrived() const { return arrived_; }
    const Settings& GetSettings() const { return settings_; }
    void SetSettings(const Settings& settings);
    bool LoadSettings(const std::string& path);
    bool SaveSettings(const std::string& path) const;
#ifdef USE_IMGUI
    void DrawImGui(const std::string& settingsPath);
#endif

private:
    Settings settings_;
    engine::Vector3 position_{};
    float yaw_ = 0.0f;
    float animationTime_ = 0.0f;
    bool poseInitialized_ = false;
    bool arrived_ = false;
#ifdef USE_IMGUI
    std::string settingsMessage_;
#endif
};