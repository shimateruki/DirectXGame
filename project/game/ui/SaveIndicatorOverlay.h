#pragma once

#include "engine/utility/math/Math.h"

#include <memory>
#include <string>
#include <vector>
#include "json.hpp"

namespace engine { class Sprite; }
namespace engine { class SpriteCommon; }

// セーブ発生時に一時表示する HUD。表示継続時間は Play で指定します。
class SaveIndicatorOverlay {
  public:
    // Sprite配置JSONを読み込み、セーブ表示を生成する。spriteCommon は借用する。
    void Initialize(engine::SpriteCommon* spriteCommon,
                    const std::string& layoutPath = "Resources/json/sprite/saveIndicator.json");
    // 表示用Spriteと借用中の参照を片付ける。
    void Finalize();
    /// <summary>フェードを先頭から再生します。</summary>
    /// <param name="holdDuration">不透明状態を維持する秒数</param>
    void Play(float holdDuration = 1.35f);
    bool IsActive() const { return isActive_ || debugHold_; }
    bool IsPlaying() const { return isActive_; }
    // フェードと指定した保持時間を進める。
    void Update(float deltaTime);
    // 現在の表示状態に応じてSpriteを描画する。
    void Draw();
    // Replay が表示状態を復元するため、所有するSpriteを列挙する。
    void CollectReplaySprites(std::vector<engine::Sprite*>& sprites) const;
    // フェード進行とデバッグ表示状態をReplay JSONへ保存／復元する。
    void CaptureReplayState(nlohmann::json& state) const;
    void RestoreReplayState(const nlohmann::json& state);

#ifdef USE_IMGUI
    // 表示時間と保持状態の開発用操作を描画する。
    void DrawImGui();
#endif

  private:
    // JSON で指定した初期配置を控え、フェード時はアルファだけを変更します。
    struct SpriteState {
        engine::Sprite* sprite = nullptr;
        engine::Vector2 basePosition = {0.0f, 0.0f};
        engine::Vector2 baseSize = {0.0f, 0.0f};
        engine::Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
        float baseRotation = 0.0f;
    };

    // レイアウト読み込み後、元の Sprite 設定を保持します。
    void LoadLayout(const std::string& layoutPath);
    void BindSprites();
    engine::Sprite* FindSprite(const std::string& name) const;
    // レイアウト内のSpriteをまとめて表示／非表示にする。
    void SetAllVisible(bool visible);
    // 読み込み時の姿勢・色を控え、指定アルファでフェードを反映する。
    void CaptureBaseState(SpriteState& state);
    void ApplyVisibilityAnimation(float alpha);

    engine::SpriteCommon* spriteCommon_ = nullptr;
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;

    SpriteState panel_;
    SpriteState icon_;
    SpriteState text_;

    bool isActive_ = false;
    bool debugHold_ = false;
    float timer_ = 0.0f;
    float holdDuration_ = 1.35f;
};
