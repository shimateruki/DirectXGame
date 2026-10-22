#pragma once
#include "Player.h"
#include "Sprite.h"
#include "TextureManager.h"
#include <algorithm>
#include <array>

// シーンが所有するHP用Spriteを更新する。位置と寸法はEditorの配置を基準にする。
class PlayerHealthHud {
public:
    bool Owns(const engine::Sprite* sprite) const {
        return std::find(sprites_.begin(),sprites_.end(),sprite)!=sprites_.end();
    }
    void Draw() const {
        // 枠には不透明な下地があるため、残量と肖像は必ずその上に描画する。
        for(size_t i:{4u,1u,2u,3u,0u}) if(sprites_[i]) sprites_[i]->Draw();
    }
    void Bind(engine::Sprite* icon, engine::Sprite* damage, engine::Sprite* fill,
              engine::Sprite* highlight, engine::Sprite* frame, const Player* player) {
        sprites_ = {icon, damage, fill, highlight, frame};
        for (size_t i=0; i<sprites_.size(); ++i)
            if (sprites_[i]) sizes_[i] = sprites_[i]->GetSize();
        if (icon) {
            const float side=(std::min)(sizes_[0].x,sizes_[0].y);
            sizes_[0]={side,side};icon->SetSize(sizes_[0]);
        }
        if (fill) { fill->SetAnimation(8,.08f,true);fill->Play(); }
        if (highlight) { highlight->SetAnimation(6,.10f,true);highlight->Play(); }
        delayedRate_=previousRate_=Rate(player);hold_=0;
        Update(player,0);
        for (auto* sprite:sprites_) if(sprite) sprite->Update();
    }

    void Update(const Player* player,float deltaTime) {
        const float dt=(std::max)(0.f,deltaTime),rate=Rate(player);
        if (rate<previousRate_-.001f) hold_=.18f;
        previousRate_=rate;
        if(rate>=delayedRate_) { delayedRate_=rate;hold_=0; }
        else if(hold_>0) hold_=(std::max)(0.f,hold_-dt);
        else delayedRate_=(std::max)(rate,delayedRate_-dt*.62f);

        for(auto* sprite:sprites_) if(sprite) sprite->SetVisible(player!=nullptr);
        // 遅れて減るバーと現在HPを分け、回復時は同時に満たす。
        for(size_t i=1;i<=3;++i) if(auto* sprite=sprites_[i]) {
            const float amount=i==1?delayedRate_:rate;
            sprite->SetSize({sizes_[i].x*amount,sizes_[i].y});
            sprite->SetVisible(player&&amount>.001f);
        }
        if(auto* icon=sprites_[0]) {
            const auto handle=engine::Sprite::LoadTexture(Portrait(player));
            const auto& meta=TextureManager::GetInstance()->GetMetadata(handle);
            icon->SetTextureHandle(handle);
            icon->SetTextureRect({0,0},{static_cast<float>(meta.width),static_cast<float>(meta.height)});
            icon->SetSize(sizes_[0]);
        }
    }
private:
    static float Rate(const Player* player) {
        return player?std::clamp(player->GetHp()/(std::max)(1.f,player->GetMaxHp()),0.f,1.f):0.f;
    }
    static const char* Portrait(const Player* player) {
        if(player&&player->IsEnemyMorphed()) switch(player->GetEnemyMorphType()) {
        case Player::EnemyMorphType::Slime:return "ui/hud/copy_portraits/pink.png";
        case Player::EnemyMorphType::FireSlime:return "ui/hud/copy_portraits/fire.png";
        case Player::EnemyMorphType::ThunderSlime:return "ui/hud/copy_portraits/thunder.png";
        case Player::EnemyMorphType::WindSlime:return "ui/hud/copy_portraits/wind.png";
        case Player::EnemyMorphType::Bomber:return "ui/hud/copy_portraits/bomb.png";
        case Player::EnemyMorphType::Crown:return "ui/hud/copy_portraits/crown.png";
        default:break;
        }
        return "ui/hud/copy_portraits/normal.png";
    }
    std::array<engine::Sprite*,5> sprites_{};
    std::array<engine::Vector2,5> sizes_{};
    float delayedRate_=1,previousRate_=1,hold_=0;
};
