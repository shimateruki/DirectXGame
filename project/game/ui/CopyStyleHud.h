#pragma once

#include "Player.h"
#include "Sprite.h"
#include "TextureManager.h"

#include <algorithm>
#include <array>
#include <cmath>

// コピー能力の短い案内を、チュートリアル・本編・プレビューで同じ条件で表示する。
// 位置と大きさは各シーンが所有する Sprite の配置設定から受け取る。
class CopyStyleHud {
  public:
    // コピー能力ごとの表示画像。事前ロードは操作ガイドのUIデータから収集する。
    static constexpr std::array<const char*, 6> kTexturePaths = {
        "ui/hud/copy_style/pink.png", "ui/hud/copy_style/fire.png", "ui/hud/copy_style/thunder.png",
        "ui/hud/copy_style/wind.png", "ui/hud/copy_style/bomb.png", "ui/hud/copy_style/crown.png"};

    void Bind(engine::Sprite* sprite) {
        // シーン所有のSpriteから基準座標と寸法だけ保持し、能力表示は一度隠す。
        sprite_ = sprite;
        previousType_ = Player::EnemyMorphType::None;
        revealTime_ = 0.0f;
        if (!sprite_) return;
        basePosition_ = sprite_->GetPosition();
        baseSize_ = sprite_->GetSize();
        sprite_->SetVisible(false);
    }

    void Update(const Player* player, float deltaTime) {
        // 能力の種類が変わったフレームだけ画像を差し替え、表示アニメーションを始め直す。
        if (!sprite_) return;

        const Player::EnemyMorphType type = player && player->IsEnemyMorphed()
                                                ? player->GetEnemyMorphType()
                                                : Player::EnemyMorphType::None;
        const char* texturePath = ResolveTexture(type);
        if (!texturePath) {
            previousType_ = Player::EnemyMorphType::None;
            revealTime_ = 0.0f;
            sprite_->SetVisible(false);
            return;
        }

        if (type != previousType_) {
            previousType_ = type;
            revealTime_ = 0.0f;
            const uint32_t handle = engine::Sprite::LoadTexture(texturePath);
            sprite_->SetTextureHandle(handle);
            const auto& metadata = TextureManager::GetInstance()->GetMetadata(handle);
            sprite_->SetTextureRect({0.0f, 0.0f},
                                    {static_cast<float>(metadata.width), static_cast<float>(metadata.height)});
        }

        // 出現時だけ小さく跳ねさせ、透明度と位置を同じ進行率で揃える。
        revealTime_ += (std::max)(deltaTime, 0.0f);
        const float t = std::clamp(revealTime_ / 0.32f, 0.0f, 1.0f);
        const float smooth = t * t * (3.0f - 2.0f * t);
        const float bounce = std::sin(std::clamp(revealTime_ / 0.48f, 0.0f, 1.0f) * 3.14159265f);
        const float scale = 1.0f + bounce * 0.08f;
        sprite_->SetVisible(true);
        sprite_->SetPosition({basePosition_.x, basePosition_.y + (1.0f - smooth) * 14.0f - bounce * 3.0f});
        sprite_->SetSize({baseSize_.x * scale, baseSize_.y * scale});
        sprite_->SetColor({1.0f, 1.0f, 1.0f, smooth});
        sprite_->Update();
    }

  private:
    static const char* ResolveTexture(Player::EnemyMorphType type) {
        // コピー能力なし、または未対応の能力では案内画像を表示しない。
        switch (type) {
        case Player::EnemyMorphType::Slime: return kTexturePaths[0];
        case Player::EnemyMorphType::FireSlime: return kTexturePaths[1];
        case Player::EnemyMorphType::ThunderSlime: return kTexturePaths[2];
        case Player::EnemyMorphType::WindSlime: return kTexturePaths[3];
        case Player::EnemyMorphType::Bomber: return kTexturePaths[4];
        case Player::EnemyMorphType::Crown: return kTexturePaths[5];
        default: return nullptr;
        }
    }

    engine::Sprite* sprite_ = nullptr; // 所有権は各シーンが持つ。
    engine::Vector2 basePosition_ = {0.0f, 0.0f};
    engine::Vector2 baseSize_ = {0.0f, 0.0f};
    Player::EnemyMorphType previousType_ = Player::EnemyMorphType::None;
    float revealTime_ = 0.0f;
};
