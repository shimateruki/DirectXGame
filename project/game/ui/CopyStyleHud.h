#pragma once

#include "Player.h"
#include "Easing.h"
#include "Sprite.h"
#include "TextureManager.h"

#include <algorithm>
#include <array>
#include <cmath>

// 現在のコピー能力名を、チュートリアル・本編・プレビューで同じ条件で表示する。
// 位置と大きさは各シーンが所有する Sprite の配置設定から受け取る。
class CopyStyleHud {
  public:
    // コピー能力ごとの表示画像。事前ロードは操作ガイドのUIデータから収集する。
    static constexpr std::array<const char*, 6> kTexturePaths = {
        "ui/hud/copy_name/pink.png", "ui/hud/copy_name/fire.png", "ui/hud/copy_name/thunder.png",
        "ui/hud/copy_name/wind.png", "ui/hud/copy_name/bomb.png", "ui/hud/copy_name/crown.png"};

    void Bind(engine::Sprite* sprite) {
        // シーン所有のSpriteから基準座標と寸法だけ保持し、能力表示は一度隠す。
        sprite_ = sprite;
        previousType_ = Player::EnemyMorphType::None;
        revealTime_ = 0.0f;
        if (!sprite_)
            return;
        basePosition_ = sprite_->GetPosition();
        baseSize_ = sprite_->GetSize();
        // 画面の縦横比が変わっても文字を潰さず、Editorで指定した範囲へ収める。
        const auto& metadata = TextureManager::GetInstance()->GetMetadata(sprite_->GetTextureHandle());
        if (metadata.width > 0 && metadata.height > 0) {
            const float width = static_cast<float>(metadata.width);
            const float height = static_cast<float>(metadata.height);
            const float fit = (std::min)(baseSize_.x / width, baseSize_.y / height);
            baseSize_ = {width * fit, height * fit};
        }

        sprite_->SetVisible(false);
    }

    void Update(const Player* player, float deltaTime) {
        // 能力の種類が変わったフレームだけ画像を差し替え、表示アニメーションを始め直す。
        if (!sprite_)
            return;

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

        // 能力名はHPバーの上に固定し、切替時の短いフェードだけを行う。
        revealTime_ += (std::max)(deltaTime, 0.0f);
        const float t = std::clamp(revealTime_ / 0.16f, 0.0f, 1.0f);
        const float smooth = Easing::SmoothStep(t);
        sprite_->SetVisible(true);
        sprite_->SetPosition(basePosition_);
        sprite_->SetSize(baseSize_);
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
    engine::Vector2 basePosition_ = engine::Vector2::ZERO;
    engine::Vector2 baseSize_ = engine::Vector2::ZERO;
    Player::EnemyMorphType previousType_ = Player::EnemyMorphType::None;
    float revealTime_ = 0.0f;
};
