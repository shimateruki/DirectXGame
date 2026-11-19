#include "LoadingScene.h"
#include <numbers>

#include "TextureManager.h"
#include "WinApp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kGaugeWidth = 680.0f;
constexpr float kGaugeHeight = 26.0f;
constexpr float kSlimeFrameWidth = 543.0f;
constexpr float kSlimeFrameHeight = 724.0f;

const char* SelectLoadingSlimeTexturePath() {
    // 通常スライムを主役にしつつ、低確率で属性違いが登場するようにする。
    static constexpr std::array<const char*, 4> kTexturePaths = {
        "Resources/sprite/loading/slime_hop_sheet_right.png",
        "Resources/sprite/loading/slime_hop_sheet_right_thunder.png",
        "Resources/sprite/loading/slime_hop_sheet_right_fire.png",
        "Resources/sprite/loading/slime_hop_sheet_right_bomber.png",
    };

    static constexpr std::array<int, 4> kAppearanceWeights = { 70, 10, 10, 10 };
    static std::mt19937 randomEngine(std::random_device{}());

    std::discrete_distribution<int> distribution(kAppearanceWeights.begin(), kAppearanceWeights.end());
    return kTexturePaths[static_cast<std::size_t>(distribution(randomEngine))];
}

// ロード演出の補間値を0～1へ収めます。
float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

// ロード画面の設計解像度から幅と高さを取得します。
float ScreenW() {
    return static_cast<float>(WinApp::GetClientWidth());
}

// ロード画面の設計高さを返します。
float ScreenH() {
    return static_cast<float>(WinApp::GetClientHeight());
}

// 進捗ゲージの左端を画面中央基準で配置します。
float GaugeLeft() {
    return (ScreenW() - kGaugeWidth) * 0.5f;
}

// 進捗ゲージを画面下端から一定距離に配置します。
float GaugeY() {
    return ScreenH() - 72.0f;
}
}

void LoadingScene::Initialize() {
    // ロード中だけ使うSpriteを作り、進捗表示とキャラクター演出を同じ座標系へ揃えます。

    const uint32_t whiteHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/white.png");
    const uint32_t glowHandle = TextureManager::GetInstance()->Load("Resources/sprite/common/circle2.png");
    const uint32_t fadeFinalHandle = TextureManager::GetInstance()->Load("Resources/sprite/fade/crown_iris/frame_47.png");
    const uint32_t slimeHandle = TextureManager::GetInstance()->Load(SelectLoadingSlimeTexturePath());
    const uint32_t loadingTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/generated/text/text_text_load.png");
    const uint32_t hintTextHandle = TextureManager::GetInstance()->Load("Resources/sprite/generated/text/text_loading_hint.png");
    const uint32_t dotHandle = TextureManager::GetInstance()->Load("Resources/sprite/number/dot.png");

    // 後から所有リスト順に描くため、背景を先に、文字や動く装飾を後に作る。
    background_ = CreateSprite(
        fadeFinalHandle,
        { ScreenW() * 0.5f, ScreenH() * 0.5f },
        { ScreenW(), ScreenH() },
        { 1.0f, 1.0f, 1.0f, 1.0f });

    hintText_ = CreateSprite(
        hintTextHandle,
        { ScreenW() * 0.5f, ScreenH() * 0.22f },
        { 180.0f, 72.0f },
        { 1.0f, 1.0f, 1.0f, 0.95f });

    loadingText_ = CreateSprite(
        loadingTextHandle,
        { ScreenW() * 0.5f - 18.0f, ScreenH() - 132.0f },
        { 154.0f, 56.0f },
        { 0.88f, 0.98f, 1.0f, 1.0f });

    dots_.clear();

    // 進行中の点は同じ素材を共有し、時間差のアニメーションだけ個別に与える。
    for (int i = 0; i < 3; ++i) {
        dots_.push_back(CreateSprite(
            dotHandle,
            { ScreenW() * 0.5f + 62.0f + static_cast<float>(i) * 18.0f, ScreenH() - 119.0f },
            { 10.0f, 10.0f },
            { 0.88f, 0.98f, 1.0f, 1.0f }));
    }

    const float gaugeCenterX = ScreenW() * 0.5f;

    // 影、外枠、地の順に置き、進捗の塗りと光はその上へ重ねる。
    gaugeShadow_ = CreateSprite(
        whiteHandle,
        { gaugeCenterX + 3.0f, GaugeY() + 4.0f },
        { kGaugeWidth + 10.0f, 36.0f },
        { 0.0f, 0.08f, 0.12f, 0.30f });

    gaugeFrame_ = CreateSprite(
        whiteHandle,
        { gaugeCenterX, GaugeY() },
        { kGaugeWidth + 6.0f, 32.0f },
        { 0.015f, 0.12f, 0.20f, 0.98f });

    gaugeTrack_ = CreateSprite(
        whiteHandle,
        { gaugeCenterX, GaugeY() },
        { kGaugeWidth, kGaugeHeight },
        { 0.02f, 0.25f, 0.32f, 0.96f });

    gaugeFill_ = CreateSprite(
        whiteHandle,
        { GaugeLeft(), GaugeY() },
        { 0.0f, 18.0f },
        { 0.20f, 0.92f, 1.0f, 1.0f });
    gaugeFill_->SetAnchorPoint({ 0.0f, 0.5f });

    // ハイライトもFillと同じ左端基準にし、幅を変えても先端位置を揃える。
    gaugeHighlight_ = CreateSprite(
        whiteHandle,
        { GaugeLeft(), GaugeY() - 5.0f },
        { 0.0f, 5.0f },
        { 0.78f, 1.0f, 1.0f, 0.80f });
    gaugeHighlight_->SetAnchorPoint({ 0.0f, 0.5f });

    gaugeGlow_ = CreateSprite(
        glowHandle,
        { GaugeLeft(), GaugeY() },
        { 44.0f, 44.0f },
        { 0.40f, 0.96f, 1.0f, 0.0f });

    gaugeBubbles_.clear();

    for (int i = 0; i < 12; ++i) {
        gaugeBubbles_.push_back(CreateSprite(
            glowHandle,
            { GaugeLeft(), GaugeY() },
            { 18.0f, 18.0f },
            { 0.88f, 1.0f, 1.0f, 0.0f }));
    }

    // 影を先に描くためスライムより先に登録し、移動時は同じXに保つ。
    shadow_ = CreateSprite(
        glowHandle,
        { GaugeLeft(), ScreenH() - 171.0f },
        { 96.0f, 30.0f },
        { 0.0f, 0.10f, 0.15f, 0.30f });

    slime_ = CreateSprite(
        slimeHandle,
        { GaugeLeft(), ScreenH() - 203.0f },
        { 116.0f, 130.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f });
    slime_->SetTextureRect({ 0.0f, 0.0f }, { kSlimeFrameWidth, kSlimeFrameHeight });

    timer_ = 0.0f;
    targetProgress_ = 0.0f;
    displayedProgress_ = 0.0f;
    slimeFrame_ = -1;
    UpdateProgressGauge();
}

void LoadingScene::Update(float deltaTime) {
    // 実進捗へ表示値を滑らかに追従させ、更新頻度の揺れを見た目に出しません。
    const float effectiveDeltaTime = deltaTime > 0.0f ? deltaTime : (1.0f / 60.0f);
    timer_ += effectiveDeltaTime;

    // 経過秒に応じて追従割合を決める。0.001は1秒後に残す差分の比率。
    const float progressFollow = 1.0f - std::pow(0.001f, effectiveDeltaTime);
    displayedProgress_ += (targetProgress_ - displayedProgress_) * progressFollow;

    // 完了通知を受けたらすぐ99.5%以上に寄せ、遷移前にゲージがほぼ端まで届くようにする。
    if (targetProgress_ >= 0.999f) {
        displayedProgress_ = (std::max)(displayedProgress_, 0.995f);
    }

    UpdateProgressGauge();
    UpdateSlimeRunAnimation();
    // ここまで部品設定を更新し、続く全SpriteのUpdateで描画用の値へ反映する。
    UpdateDotAnimation();

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Update();
        }
    }
}

void LoadingScene::Draw() {
    // 3D描画は行わず、表示物はDrawUIでまとめて描きます。
}

void LoadingScene::DrawUi() {
    // SpriteCommonのパイプラインを設定してから、保持した表示順で描画します。
    if (!GetSpriteCommon()) {
        return;
    }

    BeginSpriteDrawing();

    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Draw();
        }
    }
}

void LoadingScene::Finalize() {
    // Sprite所有権を解放する前に、参照だけ保持していた各UIポインタを無効化します。
    dots_.clear();
    gaugeBubbles_.clear();
    background_ = nullptr;
    loadingText_ = nullptr;
    slime_ = nullptr;
    shadow_ = nullptr;
    hintText_ = nullptr;
    gaugeShadow_ = nullptr;
    gaugeFrame_ = nullptr;
    gaugeTrack_ = nullptr;
    gaugeFill_ = nullptr;
    gaugeHighlight_ = nullptr;
    gaugeGlow_ = nullptr;
    sprites_.clear();
}

void LoadingScene::SetProgress(float progress) {
    // 表示の補間に使う目標進捗を0～1へ制限します。
    targetProgress_ = Clamp01(progress);
}

engine::Sprite* LoadingScene::CreateSprite(uint32_t textureHandle, const engine::Vector2& position, const engine::Vector2& size, const engine::Vector4& color) {
    // Spriteの所有権はSceneへ残し、部品更新用に生ポインタだけ返します。
    auto sprite = std::make_unique<engine::Sprite>();
    sprite->Initialize(GetSpriteCommon(), textureHandle);
    sprite->SetAnchorPoint({ 0.5f, 0.5f });
    sprite->SetPosition(position);
    sprite->SetSize(size);
    sprite->SetColor(color);
    sprite->Update();

    engine::Sprite* raw = sprite.get();
    sprites_.push_back(std::move(sprite));
    return raw;
}

void LoadingScene::UpdateSlimeRunAnimation() {
    // ゲージの進捗を走行位置へ写し、時間周期で跳ねと影の動きを重ねます。
    if (!slime_ || !shadow_) {
        return;
    }

    const float progress = Clamp01(displayedProgress_);
    const float x = GaugeLeft() + kGaugeWidth * progress;
    // 1秒に1.45周進め、余りを一回の跳ねの中の位置として四つの画像を使い分ける。
    const float hopT = std::fmod(timer_ * 1.45f, 1.0f);
    int frame = 0;
    float lift = 0.0f;
    engine::Vector2 size = { 116.0f, 130.0f };
    float rotation = 0.0f;

    // 踏み切り前は横に広がった姿勢で、次の跳ね上がりへの溜めを作る。
    if (hopT < 0.22f) {
        frame = 0;
        size = { 120.0f, 122.0f };
        lift = -2.0f;
    // 踏み切りで縦に伸ばし、表示位置も持ち上げる。
    } else if (hopT < 0.42f) {
        frame = 1;
        size = { 112.0f, 138.0f };
        lift = 17.0f;
        rotation = 0.025f;
    // 空中の区間を0～1へ戻し、サイン半周期を使って途中で最も高くする。
    } else if (hopT < 0.76f) {
        frame = 2;
        size = { 116.0f, 130.0f };
        const float airT = (hopT - 0.42f) / 0.34f;
        lift = 22.0f + std::sin(airT * kPi) * 9.0f;
        rotation = -0.018f;
    } else {
        frame = 3;
        size = { 130.0f, 112.0f };
        // 着地時は横に潰し、区間中央で小さく戻して次の周へつなぐ。
        const float landT = (hopT - 0.76f) / 0.24f;
        lift = -4.0f + std::sin(landT * kPi) * 4.0f;
    }

    // 画像のコマが変わったときだけ、横に並ぶシートの切り出し位置を移す。
    if (frame != slimeFrame_) {
        slimeFrame_ = frame;
        slime_->SetTextureRect(
            { kSlimeFrameWidth * static_cast<float>(frame), 0.0f },
            { kSlimeFrameWidth, kSlimeFrameHeight });
    }

    slime_->SetPosition({ x, ScreenH() - 203.0f - lift });
    slime_->SetSize(size);
    slime_->SetRotation(rotation);

    // 跳ね上がった高さが大きいほど、床に残る影を小さく薄くして距離を見せる。
    const float shadowScale = 1.0f - Clamp01(lift / 34.0f) * 0.34f;
    shadow_->SetPosition({ x, ScreenH() - 164.0f });
    shadow_->SetSize({ 96.0f * shadowScale, 30.0f * shadowScale });
    shadow_->SetColor({ 0.0f, 0.08f, 0.14f, 0.12f + shadowScale * 0.16f });
}

void LoadingScene::UpdateDotAnimation() {
    // 点ごとに位相をずらし、進捗待ちでも静止して見えないようにします。
    for (size_t i = 0; i < dots_.size(); ++i) {
        engine::Sprite* dot = dots_[i];

        if (!dot) {
            continue;
        }

        const float phase = timer_ * 4.0f - static_cast<float>(i) * 0.58f;
        const float pulse = 0.5f + 0.5f * std::sin(phase);
        const float alpha = 0.26f + pulse * 0.74f;
        const float scale = 0.72f + pulse * 0.35f;
        dot->SetSize({ 10.0f * scale, 10.0f * scale });
        dot->SetColor({ 0.88f, 0.98f, 1.0f, alpha });
    }
}

void LoadingScene::UpdateProgressGauge() {
    // 表示進捗を幅へ変換し、発光と波形を時間で軽く変動させます。
    if (!gaugeFill_ || !gaugeHighlight_ || !gaugeGlow_) {
        return;
    }

    const float progress = Clamp01(displayedProgress_);
    const float fillWidth = kGaugeWidth * progress;
    const float wave = std::sin(timer_ * 5.4f) * 1.2f;
    const float pulse = 0.5f + 0.5f * std::sin(timer_ * 4.2f);

    // 塗りとハイライトは同じ幅を使い、上下の揺れと明るさだけを変える。
    gaugeFill_->SetPosition({ GaugeLeft(), GaugeY() + wave * 0.20f });
    gaugeFill_->SetSize({ fillWidth, 18.0f + wave });
    gaugeFill_->SetColor({ 0.10f + pulse * 0.06f, 0.78f + pulse * 0.12f, 1.0f, 1.0f });

    gaugeHighlight_->SetPosition({ GaugeLeft(), GaugeY() - 5.0f + wave * 0.45f });
    gaugeHighlight_->SetSize({ fillWidth, 3.0f + pulse * 1.5f });
    gaugeHighlight_->SetColor({ 0.84f, 1.0f, 1.0f, 0.44f + pulse * 0.30f });

    // 先端の光は進捗ゼロ付近では隠し、塗った幅の右端を追わせる。
    gaugeGlow_->SetVisible(progress > 0.002f);
    gaugeGlow_->SetPosition({ GaugeLeft() + fillWidth, GaugeY() });
    gaugeGlow_->SetSize({ 36.0f + pulse * 8.0f, 36.0f + pulse * 8.0f });
    gaugeGlow_->SetColor({ 0.42f, 0.98f, 1.0f, 0.18f + pulse * 0.18f });

    for (size_t i = 0; i < gaugeBubbles_.size(); ++i) {
        engine::Sprite* bubble = gaugeBubbles_[i];

        if (!bubble) {
            continue;
        }

        // 泡は幅方向に等間隔で配置し、ゲージの伸びがその位置を6画素越えたら表示する。
        const float normalizedX = (static_cast<float>(i) + 0.5f) / static_cast<float>(gaugeBubbles_.size());
        const float bubbleX = GaugeLeft() + normalizedX * kGaugeWidth;
        const bool visible = fillWidth > normalizedX * kGaugeWidth + 6.0f;
        bubble->SetVisible(visible);

        if (!visible) {
            continue;
        }

        const float phase = timer_ * 3.0f - static_cast<float>(i) * 0.52f;
        const float rise = std::sin(phase);
        const float size = 12.0f + (0.5f + 0.5f * std::sin(phase * 0.73f)) * 7.0f;
        bubble->SetPosition({ bubbleX, GaugeY() + rise * 3.0f });
        bubble->SetSize({ size, size });
        bubble->SetColor({ 0.74f, 1.0f, 1.0f, 0.16f + pulse * 0.20f });
    }
}
