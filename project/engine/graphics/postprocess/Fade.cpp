#include "Fade.h"

#include "DirectXCommon.h"
#include "PostEffect.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "WinApp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {
// フェード演出に使う画面の設計幅と高さを返します。
float ScreenW() {
    return static_cast<float>(WinApp::kClientWidth);
}

// フェード演出に使う画面の設計高さを返します。
float ScreenH() {
    return static_cast<float>(WinApp::kClientHeight);
}

// 補間値を0～1へ制限します。
float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

// フェードの始点と終点を滑らかにつなぐ補間率を返します。
float EaseInOut(float t) {
    t = Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

// フェード用連番画像の番号を2桁に揃えてファイルパスを作ります。
std::string MakeFramePath(const std::string& directory, int index) {
    char buffer[256] = {};
    std::snprintf(buffer, sizeof(buffer), "%s/frame_%02d.png", directory.c_str(), index);
    return buffer;
}
}

Fade* Fade::GetInstance() {
    static Fade instance;
    return &instance;
}

// ノイズ素材とSpriteを準備し、旧PostEffectのフェード値を解除します。
void Fade::Initialize() {
    ResolveNoiseTexture();
    ResetPostEffectFade();
    InitializeSprites();
}

// 描画基盤と黒画面用Spriteを一度だけ作り、画像列が欠けても暗転できるようにします。
void Fade::InitializeSprites() {
    if (fallbackBlack_) {
        return;
    }

    if (!spriteCommon_) {
        spriteCommon_ = std::make_unique<engine::SpriteCommon>();
        spriteCommon_->Initialize(DirectXCommon::GetInstance());
    }

    // 連番画像を読み込めない場合も画面を覆えるよう、白一枚を黒く着色するSpriteを持つ。
    const uint32_t whiteHandle = TextureManager::GetInstance()->GetSrvHandle("Resources/sprite/common/white.png");
    if (whiteHandle == 0) {
        return;
    }
    fallbackBlack_ = std::make_unique<engine::Sprite>();
    fallbackBlack_->Initialize(spriteCommon_.get(), whiteHandle);
    fallbackBlack_->SetAnchorPoint({ 0.5f, 0.5f });
}

void Fade::LoadSequence(FrameSequence& sequence, const std::string& directory, int frameCount) {
    if (!sequence.textureHandles.empty()) {
        return;
    }

    // 1枚でも欠けていたら系列を確定せず、描画側の黒フェードに任せる。
    std::vector<uint32_t> resolvedHandles;
    resolvedHandles.reserve(frameCount);
    for (int i = 0; i < frameCount; ++i) {
        const uint32_t handle = TextureManager::GetInstance()->GetSrvHandle(MakeFramePath(directory, i));
        if (handle == 0) {
            return;
        }
        resolvedHandles.push_back(handle);
    }

    // すべて揃ってから確定し、途中までのフレームを誤って再生しない。
    sequence.textureHandles = std::move(resolvedHandles);

    if (!sequence.textureHandles.empty()) {
        sequence.sprite = std::make_unique<engine::Sprite>();
        sequence.sprite->Initialize(spriteCommon_.get(), sequence.textureHandles.front());
        sequence.sprite->SetAnchorPoint({ 0.5f, 0.5f });
    }
}

void Fade::ResolveNoiseTexture() {
    // ノイズ素材が使える場合だけSRVを解決し、フェード描画に渡す。
    if (noiseTextureResolved_) {
        return;
    }
    const uint32_t noiseHandle = TextureManager::GetInstance()->GetSrvHandle(
        "Resources/sprite/effect/noise0.png");
    if (noiseHandle == 0) {
        return;
    }
    PostEffect::GetInstance()->SetNoiseTexture(noiseHandle);
    noiseTextureResolved_ = true;
}

// 開閉方向から被覆率を計算し、演出時間を使い切ったらFinishedへ進めます。
void Fade::Update(float deltaTime) {
    ResolveNoiseTexture();
    if (status_ == Status::None || status_ == Status::Finished) {
        return;
    }

    counter_ += deltaTime;
    const float t = GetNormalizedTime();
    // 開閉を同じ被覆率で表し、連番画像のフレーム選択に使う。
    coverage_ = IsClosing() ? EaseInOut(t) : 1.0f - EaseInOut(t);

    // このフェードはSpriteで描画し、旧PostEffect側のフェード値は毎回解除する。
    ResetPostEffectFade();

    if (t >= 1.0f) {
        coverage_ = IsClosing() ? 1.0f : 0.0f;
        status_ = Status::Finished;
    }
}

// スタイルに応じた連番画像を描き、画像が使えない場合は黒画面で被覆します。
void Fade::Draw() {
    if (coverage_ <= 0.001f) {
        return;
    }

    InitializeSprites();

    // 演出の種類に応じた画像列を選び、使えないときは描画側で黒板へ戻す。
    if (style_ == VisualStyle::CrownIris) {
        LoadSequence(crownIris_, "Resources/sprite/fade/crown_iris", 48);
        DrawFrameSequence(crownIris_, coverage_);
    } else {
        LoadSequence(slimeWipe_, "Resources/sprite/fade/slime_wipe", 48);
        DrawFrameSequence(slimeWipe_, coverage_);
    }
}

// 王冠アイリスを閉状態から開くフェードを開始します。
void Fade::StartFadeIn(float duration) {
    Begin(Status::FadeIn, VisualStyle::CrownIris, duration, { 0.5f, 0.5f }, 1.0f);
}

// 王冠アイリスを開状態から閉じるフェードを開始します。
void Fade::StartFadeOut(float duration) {
    Begin(Status::FadeOut, VisualStyle::CrownIris, duration, { 0.5f, 0.5f }, 0.0f);
}

// 指定中心から画面を閉じるアイリス演出を開始します。
void Fade::StartIrisOut(float duration, const engine::Vector2& center) {
    Begin(Status::IrisOut, VisualStyle::CrownIris, duration, center, 0.0f);
}

// 指定中心へ向けて画面を開くアイリス演出を開始します。
void Fade::StartIrisIn(float duration, const engine::Vector2& center) {
    Begin(Status::IrisIn, VisualStyle::CrownIris, duration, center, 1.0f);
}

// 進行中の演出と被覆率を消し、旧PostEffectのフェード値も解除します。
void Fade::Stop() {
    status_ = Status::None;
    counter_ = 0.0f;
    coverage_ = 0.0f;
    ResetPostEffectFade();
}

void Fade::Begin(Status status, VisualStyle style, float duration, const engine::Vector2& center, float initialCoverage) {
    // 再開時にも前回のポストエフェクト量が残らないよう、開始値を確定してからリセットする。
    InitializeSprites();
    status_ = status;
    style_ = style;
    duration_ = (std::max)(duration, 0.001f);
    counter_ = 0.0f;
    center_ = { Clamp01(center.x), Clamp01(center.y) };
    coverage_ = Clamp01(initialCoverage);
    ResetPostEffectFade();
}

// 被覆率から対応フレームを選び、画像列が未準備なら黒画面へ切り替えます。
void Fade::DrawFrameSequence(FrameSequence& sequence, float coverage) {
    if (!sequence.sprite || sequence.textureHandles.empty()) {
        DrawFallbackBlack(coverage);
        return;
    }

    // 画面の被覆率を連番画像の添字に対応させ、開閉に同じ画像列を使う。
    const float framePosition = Clamp01(coverage) * static_cast<float>(sequence.textureHandles.size() - 1);
    const size_t frameIndex = static_cast<size_t>(std::round(framePosition));
    const uint32_t textureHandle = sequence.textureHandles[(std::min)(frameIndex, sequence.textureHandles.size() - 1)];

    sequence.sprite->SetTextureHandle(textureHandle);
    sequence.sprite->SetPosition({ ScreenW() * 0.5f, ScreenH() * 0.5f });
    sequence.sprite->SetSize({ ScreenW(), ScreenH() });
    sequence.sprite->SetRotation(0.0f);
    sequence.sprite->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    sequence.sprite->Update();
    sequence.sprite->Draw();
}

// 画面端に隙間を作らない大きさで黒Spriteを描きます。
void Fade::DrawFallbackBlack(float alpha) {
    if (!fallbackBlack_) {
        return;
    }

    fallbackBlack_->SetPosition({ ScreenW() * 0.5f, ScreenH() * 0.5f });
    // 画面端に隙間が出ないよう、フォールバックの黒板だけ少し大きく描く。
    fallbackBlack_->SetSize({ ScreenW() + 8.0f, ScreenH() + 8.0f });
    fallbackBlack_->SetRotation(0.0f);
    fallbackBlack_->SetColor({ 0.0f, 0.0f, 0.0f, Clamp01(alpha) });
    fallbackBlack_->Update();
    fallbackBlack_->Draw();
}

// 0秒指定でも除算が破綻しないよう、経過率を0～1へ収めます。
float Fade::GetNormalizedTime() const {
    return Clamp01(counter_ / (std::max)(duration_, 0.001f));
}

// 現在の状態が被覆率を増やす方向か判定します。
bool Fade::IsClosing() const {
    return status_ == Status::FadeOut || status_ == Status::IrisOut;
}

// Sprite描画へ移したフェードと旧PostEffectの値が重複しないようにします。
void Fade::ResetPostEffectFade() {
    if (PostEffect::Params* params = PostEffect::GetInstance()->GetParams()) {
        params->slimeFadeIntensity = 0.0f;
        params->irisFadeIntensity = 0.0f;
    }
}
