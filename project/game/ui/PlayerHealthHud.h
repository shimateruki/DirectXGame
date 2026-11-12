#pragma once

#include "engine/utility/math/Math.h"
#include "engine/graphics/2d/HealthBarState.h"
#include <array>

class Player;
namespace engine { class Sprite; }

// 配置はシーン所有のSpriteを借用し、描画と演出は専用実装へまとめる。
// 関連：Playerとシーン所有のSpriteを借用し、HP表示・被ダメージ時の補間を更新する。
// 主な入口：Bind / Update / Draw / Reset
class PlayerHealthHud {
public:
    enum class Style { Simple, Animated };

    // Spriteは破棄せず参照と演出状態を解除する。シーン側のSpriteを片付ける前に呼ぶ。
    void Reset();
    // ここでのOwnsは描画対象かの判定。Spriteの所有権を持つという意味ではない。
    bool Owns(const engine::Sprite* sprite) const;
    // 保持した5パーツを枠から順に描画する。シーン側で同じSpriteを二重描画しない。
    void Draw() const;
    // Spriteの配置・サイズ・色を基準値として取り込む。各ポインタは借用で、未使用パーツはnullptr可。
    void Bind(engine::Sprite* icon, engine::Sprite* damage, engine::Sprite* fill,
              engine::Sprite* highlight, engine::Sprite* frame, const Player* player,
              Style style = Style::Simple);
    // 経過秒数でHPと形態アイコンを更新する。Playerがnullptrなら全パーツを非表示にする。
    void Update(const Player* player, float deltaTime);
    // 演出値への直接参照。Reset/Bindで内容が初期化されるため、その後の値を持ち越さない。
    engine::HealthBarState& State() { return state_; }
    const engine::HealthBarState& State() const { return state_; }

private:
    // Sprite本体はシーン所有。基準値はBind時のスナップショットとして演出から参照する。
    struct Part {
        engine::Sprite* sprite = nullptr;
        engine::Vector2 basePosition{}, baseSize{};
        engine::Vector4 baseColor{1, 1, 1, 1};
    };
    static Part Capture(engine::Sprite* sprite);
    std::array<const Part*, 5> Parts() const;
    void UpdatePortrait(const Player* player);
    void UpdateSimple(const Player* player, float rate, bool visible);
    void UpdateAnimated(const Player* player, float hpRate, bool visible);
    static const char* Portrait(const Player* player);

    Style style_ = Style::Simple;
    Part icon_, damage_, fill_, highlight_, frame_;
    engine::HealthBarState state_;
    const char* portrait_ = nullptr;
};
