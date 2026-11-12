#pragma once
#include "engine/utility/math/Math.h"
#include <string>

namespace engine { class Object3D; }

// 追従位置と目的地の座標だけを受け取り、ゲーム進行とは独立して案内モデルを動かす。
// 関連：TutorialDirectorなどが追従元・目的地を渡し、案内モデルの位置と到着判定を更新する。
// 主な入口：Initialize / Update / Hide / Reset
class DestinationGuide {
public:
    // 案内モデルの距離・高さ・動きの調整値。目的地やステップの進行状態は含めない。
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

    // 調整値を適用し、補間と到着判定を初期化する。表示Objectの生成・所有は行わない。
    void Initialize(const Settings& settings);
    // 案内の補間と到着判定を初期状態へ戻す。
    void Reset();
    // 一時非表示では到着済みを解除しない。次の案内を始める側がResetする。
    void Hide(engine::Object3D& arrow);
    void Update(engine::Object3D& arrow, const engine::Vector3& followerPosition,
                const engine::Vector3& targetPosition, float deltaTime);
    bool HasArrived() const { return arrived_; }
    const Settings& GetSettings() const { return settings_; }
    void SetSettings(const Settings& settings);
    // 案内の調整値をJSONから読む。目的地の切替や表示開始は行わない。
    bool LoadSettings(const std::string& path);
    // 現在の案内設定をJSONへ書き込み、成功可否を返す。
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