#include "game/ui/PlayerHealthHud.h"
#include <numbers>
#include "Player.h"
#include "Sprite.h"
#include "TextureManager.h"
#include "game/ui/SpriteLayoutScaler.h"
#include <algorithm>
#include <cmath>

void PlayerHealthHud::Reset() { *this = PlayerHealthHud{}; }

bool PlayerHealthHud::Owns(const engine::Sprite* sprite) const {
    if (!sprite) return false;
    for (const auto* part : Parts()) if (part->sprite == sprite) return true;
    return false;
}

void PlayerHealthHud::Draw() const {
    // 不透明な枠を先に描き、残量と肖像が隠れないようにする。
    for (const auto* part : Parts()) if (part->sprite) part->sprite->Draw();
}

void PlayerHealthHud::Bind(engine::Sprite* icon, engine::Sprite* damage, engine::Sprite* fill,
          engine::Sprite* highlight, engine::Sprite* frame, const Player* player,
          Style style) {
    // 前のSprite参照と演出状態を破棄し、新しいレイアウトの姿勢を基準にします。
    Reset();
    style_ = style;
    icon_ = Capture(icon); damage_ = Capture(damage); fill_ = Capture(fill);
    highlight_ = Capture(highlight); frame_ = Capture(frame);
    // 肖像は短辺に合わせた正方形で表示し、液面とハイライトは異なる刻みで再生します。
    const float side = (std::min)(icon_.baseSize.x, icon_.baseSize.y);
    icon_.baseSize = {side, side};
    if (fill) { fill->SetAnimation(8, .08f, true); fill->Play(); }
    if (highlight) { highlight->SetAnimation(6, .10f, true); highlight->Play(); }
    // 初回表示を被弾と誤認しないよう、現在HPを起点としてから見た目を反映します。
    state_.Reset(player ? player->GetHp() : 0.0f, player ? player->GetMaxHp() : 1.0f);
    Update(player, 0.0f);
    // 簡素表示はシーン側の一括Updateで進める。初期化時だけGPUへ配置を反映する。
    if (style == Style::Simple)
        for (const auto* part : Parts()) if (part->sprite) part->sprite->Update();
}

void PlayerHealthHud::Update(const Player* player, float deltaTime) {
    // 最大HPは割合計算に使うため1以上にし、プレイヤーが未取得なら表示を閉じる。
    const float maxHp = player ? (std::max)(player->GetMaxHp(), 1.0f) : 1.0f;
    const float hp = player ? std::clamp(player->GetHp(), 0.0f, maxHp) : 0.0f;
    const bool visible = player != nullptr;
    state_.Update(hp, maxHp, deltaTime, visible, style_ == Style::Animated);
    // 同じHP割合と表示状態を基に、設定した見た目でスプライトを更新する。
    const float hpRate = engine::HealthBarState::Rate(hp, maxHp);
    if (style_ == Style::Animated) UpdateAnimated(player, hpRate, visible);
    else UpdateSimple(player, hpRate, visible);
}

PlayerHealthHud::Part PlayerHealthHud::Capture(engine::Sprite* sprite) {
    return sprite ? Part{sprite, sprite->GetPosition(), sprite->GetSize(), sprite->GetColor()} : Part{};
}

std::array<const PlayerHealthHud::Part*, 5> PlayerHealthHud::Parts() const { return {&frame_, &damage_, &fill_, &highlight_, &icon_}; }

void PlayerHealthHud::UpdatePortrait(const Player* player) {
    if (!icon_.sprite) return;
    const char* path = Portrait(player);
    // 同じ形態の間はテクスチャ検索とUVの再設定を繰り返さない。
    if (portrait_ == path) return;
    portrait_ = path;
    const auto handle = engine::Sprite::LoadTexture(path);
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
    icon_.sprite->SetTextureHandle(handle);
    icon_.sprite->SetTextureRect({0, 0}, {static_cast<float>(metadata.width), static_cast<float>(metadata.height)});
}

void PlayerHealthHud::UpdateSimple(const Player* player, float rate, bool visible) {
    // 形態の肖像を同期し、残量と遅れて減る色帯だけ幅をHPに連動させます。
    UpdatePortrait(player);
    for (auto* part : {&frame_, &icon_, &damage_, &fill_, &highlight_}) {
        if (!part->sprite) continue;
        const float amount = part == &damage_ ? state_.delayedRate : rate;
        const bool bar = part != &frame_ && part != &icon_;
        part->sprite->SetVisible(visible && (!bar || amount > .001f));
        part->sprite->SetSize({part->baseSize.x * (bar ? amount : 1.0f), part->baseSize.y});
    }
}

void PlayerHealthHud::UpdateAnimated(const Player* player, float hpRate, bool visible) {
    // 被弾中の反応と低HP時の脈動を別の時計から求め、枠・帯・肖像の演出へ使います。
    const float pulse = state_.damagePulseTimer > 0.0f ? std::sin(state_.damagePulseTimer * 70.0f) : 0.0f;
    // 0.42秒の残り時間を0〜1に直し、瞬間的な反動の山と後半の減衰に使う。
    const float damageRate = std::clamp(state_.damagePulseTimer / 0.42f, 0.0f, 1.0f);
    const float damageImpact = std::sin((1.0f - damageRate) * std::numbers::pi_v<float>) * damageRate;
    // HPが34%を下回るほど点滅と赤みを強める。満タン側と非表示時は低HPの演出を掛けない。
    const float lowHpRate = visible ? std::clamp((0.34f - hpRate) / 0.34f, 0.0f, 1.0f) : 0.0f;
    const float lowHpPulse = lowHpRate * (0.5f + 0.5f * std::sin(state_.animationTimer * (5.0f + lowHpRate * 3.0f)));
    const float liquidWave = std::sin(state_.animationTimer * 4.6f);
    const float idleIconPulse = 1.0f + std::sin(state_.animationTimer * 2.2f) * 0.012f + lowHpPulse * 0.035f;
    const float hpIconPulse = idleIconPulse + std::abs(pulse) * damageRate * 0.07f;
    // 枠と液体は同じ横揺れを使う。振れ幅はデザイン座標の3.2を画面用に換算する。
    const float barShake =
        damageRate > 0.0f ? std::sin(state_.damagePulseTimer * 92.0f) * SpriteLayoutScaler::ScaleDesignX(3.2f) * damageRate
                          : 0.0f;
    const engine::Vector2 liquidOffset = {barShake, 0.0f};
    const float liquidHeightScale = 1.0f + damageImpact * 0.07f;
    // 肖像だけは専用の残り時間でXYと回転を揺らし、枠の被弾演出とは別に終える。
    const bool damageReacting = state_.hurtIconTimer > 0.0f;
    const float hpIconShakeX =
        damageReacting ? std::sin(state_.hurtIconTimer * 85.0f) * SpriteLayoutScaler::ScaleDesignX(3.0f) : 0.0f;
    const float hpIconShakeY =
        damageReacting ? std::sin(state_.hurtIconTimer * 61.0f) * SpriteLayoutScaler::ScaleDesignY(1.2f) : 0.0f;
    const float hpIconRotation =
        damageReacting ? std::sin(state_.hurtIconTimer * 55.0f) * 0.055f : liquidWave * lowHpRate * 0.012f;

    // 肖像の画像と表示を先に更新し、残量の幅には連動させない。
    if (icon_.sprite) {
        UpdatePortrait(player);
        icon_.sprite->SetVisible(visible);
        // 枠と同じ揺れ・拡縮中心へ追従し、装飾を丸枠の内側に保つ。
        icon_.sprite->SetPosition(
            {icon_.basePosition.x + barShake + (icon_.basePosition.x - frame_.basePosition.x) * damageImpact * 0.012f + hpIconShakeX * 0.25f,
             icon_.basePosition.y + hpIconShakeY * 0.25f});
        icon_.sprite->SetSize({icon_.baseSize.x * hpIconPulse, icon_.baseSize.y * hpIconPulse});
        icon_.sprite->SetRotation(hpIconRotation);
        icon_.sprite->SetColor({icon_.baseColor.x, icon_.baseColor.y, icon_.baseColor.z,
                                     visible ? icon_.baseColor.w : 0.0f});
        icon_.sprite->Update();
    }

    // 遅れて減る色帯は過去のHP割合から描き、現在の減少幅が短時間だけ残るようにします。
    if (damage_.sprite) {
        const float rate = std::clamp(state_.delayedRate, 0.0f, 1.0f);
        damage_.sprite->SetVisible(visible && rate > 0.001f);
        damage_.sprite->SetPosition(
            {damage_.basePosition.x + liquidOffset.x, damage_.basePosition.y + liquidOffset.y});
        damage_.sprite->SetSize(
            {damage_.baseSize.x * rate, damage_.baseSize.y * liquidHeightScale});
        damage_.sprite->SetColor(
            {damage_.baseColor.x, damage_.baseColor.y, damage_.baseColor.z,
             visible ? damage_.baseColor.w * (0.88f + damageRate * 0.12f) : 0.0f});
        damage_.sprite->Update();
    }

    // 手前の液体は現在HPと同じ幅にし、低HPの間は赤みに寄せます。
    if (fill_.sprite) {
        fill_.sprite->SetVisible(visible && hpRate > 0.001f);
        fill_.sprite->SetPosition(
            {fill_.basePosition.x + liquidOffset.x, fill_.basePosition.y + liquidOffset.y});
        fill_.sprite->SetSize({fill_.baseSize.x * hpRate, fill_.baseSize.y * liquidHeightScale});
        fill_.sprite->SetColor(
            {1.0f, 1.0f - lowHpPulse * 0.08f, 1.0f - lowHpPulse * 0.14f, visible ? fill_.baseColor.w : 0.0f});
        fill_.sprite->Update();
    }

    // 光沢は実ゲージの残量に合わせて切り詰め、空の部分へはみ出さないようにします。
    if (highlight_.sprite) {
        const float shimmer = 0.78f + hpRate * 0.10f + damageImpact * 0.12f;
        const float alpha = visible && hpRate > 0.001f ? highlight_.baseColor.w * shimmer : 0.0f;
        highlight_.sprite->SetVisible(visible && hpRate > 0.001f);
        highlight_.sprite->SetPosition({highlight_.basePosition.x + liquidOffset.x,
                                             highlight_.basePosition.y - SpriteLayoutScaler::ScaleDesignY(0.35f)});
        highlight_.sprite->SetSize(
            {highlight_.baseSize.x * hpRate, highlight_.baseSize.y * liquidHeightScale});
        highlight_.sprite->SetColor(
            {highlight_.baseColor.x, highlight_.baseColor.y, highlight_.baseColor.z, alpha});
        highlight_.sprite->Update();
    }

    // 枠はHPで切り詰めず、被弾による揺れと伸縮だけを反映します。
    if (frame_.sprite) {
        frame_.sprite->SetVisible(visible);
        frame_.sprite->SetPosition({frame_.basePosition.x + barShake, frame_.basePosition.y});
        frame_.sprite->SetSize({frame_.baseSize.x * (1.0f + damageImpact * 0.012f),
                                     frame_.baseSize.y * (1.0f + damageImpact * 0.055f)});
        frame_.sprite->SetColor(
            {1.0f, 1.0f - lowHpPulse * 0.10f, 1.0f - lowHpPulse * 0.16f, visible ? frame_.baseColor.w : 0.0f});
        frame_.sprite->Update();
    }

}

const char* PlayerHealthHud::Portrait(const Player* player) {
    // 現在の本体と同じ装飾付き肖像を表示し、被弾時も能力の目印を維持する。
    if (player && player->IsEnemyMorphed()) {
        switch (player->GetEnemyMorphType()) {
        case Player::EnemyMorphType::Slime: return "ui/hud/copy_portraits/pink.png";
        case Player::EnemyMorphType::FireSlime: return "ui/hud/copy_portraits/fire.png";
        case Player::EnemyMorphType::ThunderSlime: return "ui/hud/copy_portraits/thunder.png";
        case Player::EnemyMorphType::WindSlime: return "ui/hud/copy_portraits/wind.png";
        case Player::EnemyMorphType::Bomber: return "ui/hud/copy_portraits/bomb.png";
        case Player::EnemyMorphType::Crown: return "ui/hud/copy_portraits/crown.png";
        default: break;
        }
    }
    return "ui/hud/copy_portraits/normal.png";
}
