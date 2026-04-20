#define NOMINMAX
#include "CinematicFade.h"
#include "WinApp.h"
#include "easing.h" // ※隊長の環境のイージング関数パス
#include <algorithm>

CinematicFade* CinematicFade::GetInstance() {
    static CinematicFade instance;
    return &instance;
}

void CinematicFade::Initialize(SpriteCommon* spriteCommon) {
    screenWidth_ = static_cast<float>(WinApp::kClientWidth);
    screenHeight_ = static_cast<float>(WinApp::kClientHeight);

    
    topSprite_ = std::make_unique<Sprite>();
    topSprite_->Initialize(spriteCommon, "Resources/sprite/white.png");
    topSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f }); // 白い画像を真っ黒に染める
    topSprite_->SetSize({ screenWidth_, screenHeight_ / 2.0f });
    topSprite_->SetAnchorPoint({ 0.0f, 0.0f }); // ★ 計算しやすいように左上基準にする！

    bottomSprite_ = std::make_unique<Sprite>();
    bottomSprite_->Initialize(spriteCommon, "Resources/sprite/white.png");
    bottomSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
    bottomSprite_->SetSize({ screenWidth_, screenHeight_ / 2.0f });
    bottomSprite_->SetAnchorPoint({ 0.0f, 0.0f });

    // 初期位置（画面外）
    topSprite_->SetPosition({ 0.0f, -(screenHeight_ / 2.0f) });
    bottomSprite_->SetPosition({ 0.0f, screenHeight_ });

    state_ = State::kIdle;
}

void CinematicFade::StartClose(float duration) {
    if (state_ == State::kIdle || state_ == State::kOpening) {
        state_ = State::kClosing;
        timer_ = 0.0f;
        duration_ = duration;
    }
}

void CinematicFade::StartOpen(float duration) {
    if (state_ == State::kClosed || state_ == State::kClosing) {
        state_ = State::kOpening;
        timer_ = 0.0f;
        duration_ = duration;
    }
}

void CinematicFade::Update(float deltaTime) {
    // 待機中、または完全に閉じた「直後」も更新自体は止めない（行列更新のため）
    if (state_ == State::kIdle) return;

    if (state_ == State::kClosing || state_ == State::kOpening) {
        timer_ += deltaTime;
        float t = std::min(timer_ / duration_, 1.0f);

        // Easing処理（ここは以前と同じ）
        float easeT = (state_ == State::kClosing) ? Easing::OutExpo(t) : Easing::InOutSine(t);

        float topY = (state_ == State::kClosing)
            ? Math::Lerp(-(screenHeight_ / 2.0f), 0.0f, easeT)
            : Math::Lerp(0.0f, -(screenHeight_ / 2.0f), easeT);

        float bottomY = (state_ == State::kClosing)
            ? Math::Lerp(screenHeight_, screenHeight_ / 2.0f, easeT)
            : Math::Lerp(screenHeight_ / 2.0f, screenHeight_, easeT);

        topSprite_->SetPosition({ 0.0f, topY });
        bottomSprite_->SetPosition({ 0.0f, bottomY });

        if (t >= 1.0f) {
            state_ = (state_ == State::kClosing) ? State::kClosed : State::kIdle;
        }
    }

    // 常にUpdateを呼んで行列を最新にする
    topSprite_->Update();
    bottomSprite_->Update();
}

void CinematicFade::Draw() {
    if (state_ == State::kIdle) return;

    // 描画
    topSprite_->Draw();
    bottomSprite_->Draw();
}
void CinematicFade::SetSpriteCommon(SpriteCommon* common) {
    if (topSprite_) topSprite_->SetCommon(common);
    if (bottomSprite_) bottomSprite_->SetCommon(common);
}