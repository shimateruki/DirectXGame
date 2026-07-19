#pragma once

#include "engine/utility/math/Math.h"

#include <memory>
#include <string>
#include <vector>
#include "json.hpp"

class Sprite;
class SpriteCommon;

class SaveIndicatorOverlay {
public:
    void Initialize(SpriteCommon* spriteCommon, const std::string& layoutPath = "Resources/json/sprite/saveIndicator.json");
    void Finalize();
    void Play(float holdDuration = 1.35f);
    bool IsActive() const { return isActive_ || debugHold_; }
    bool IsPlaying() const { return isActive_; }
    void Update(float deltaTime);
    void Draw();
    void CollectReplaySprites(std::vector<Sprite*>& sprites) const;
    void CaptureReplayState(nlohmann::json& state) const;
    void RestoreReplayState(const nlohmann::json& state);

#ifdef USE_IMGUI
    void DrawImGui();
#endif

private:
    struct SpriteState {
        Sprite* sprite = nullptr;
        Vector2 basePosition = { 0.0f, 0.0f };
        Vector2 baseSize = { 0.0f, 0.0f };
        Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        float baseRotation = 0.0f;
    };

    void LoadLayout(const std::string& layoutPath);
    void BindSprites();
    Sprite* FindSprite(const std::string& name) const;
    void SetAllVisible(bool visible);
    void CaptureBaseState(SpriteState& state);
    void ApplyVisibilityAnimation(float alpha);

    SpriteCommon* spriteCommon_ = nullptr;
    std::vector<std::unique_ptr<Sprite>> sprites_;

    SpriteState panel_;
    SpriteState icon_;
    SpriteState text_;

    bool isActive_ = false;
    bool debugHold_ = false;
    float timer_ = 0.0f;
    float holdDuration_ = 1.35f;
};
