#include "Text.h"
#include "SpriteCommon.h"
#include "TextureManager.h" // GetMetadata用

void Text::Initialize(SpriteCommon* common, const std::string& fontTextureName) {
    common_ = common;
    fontTextureHandle_ = Sprite::LoadTexture(fontTextureName);

}

void Text::SetString(const std::string& str) {
    if (text_ == str) return; // 文字列が変わってなければ何もしない
    text_ = str;

    // 必要なスプライト数（文字数）に合わせて sprites_ を調整
    sprites_.resize(text_.length());

    for (size_t i = 0; i < text_.length(); ++i) {
        if (!sprites_[i]) {
            sprites_[i] = std::make_unique<Sprite>();
            sprites_[i]->Initialize(common_, fontTextureHandle_);
        }

        // ASCIIコード（文字）から、フォントテクスチャのUV座標を計算
        unsigned char charCode = static_cast<unsigned char>(text_[i]);

        // (例：フォントが横16文字で並んでいる場合)
        int tx = (charCode % fontCharsPerRow_) * fontWidth_;
        int ty = (charCode / fontCharsPerRow_) * fontHeight_;

        // SpriteのUV切り出しを設定 (SetTextureRect)
        sprites_[i]->SetTextureRect(
            { (float)tx, (float)ty },             // 左上のUV座標
            { (float)fontWidth_, (float)fontHeight_ } // 1文字分のサイズ
        );

        // 1文字分のスプライトサイズも設定
        sprites_[i]->SetSize({ (float)fontWidth_, (float)fontHeight_ });
    }

    // 座標の再計算のために Update() を呼ぶ
    Update();
}

void Text::SetPosition(const Vector2& position) {
    position_ = position;
    Update();
}

void Text::SetScale(const Vector2& scale) {
    scale_ = scale;
    Update();
}

void Text::Update() {
    // 各文字（スプライト）の位置とスケールを設定し直す
    for (size_t i = 0; i < sprites_.size(); ++i) {
        if (sprites_[i]) {
            // (文字の横幅 * スケール * i文字目) でX座標をずらす
            float posX = position_.x + (i * fontWidth_ * scale_.x);
            sprites_[i]->SetPosition({ posX, position_.y });
            sprites_[i]->SetSize({ fontWidth_ * scale_.x, fontHeight_ * scale_.y });

            sprites_[i]->Update(); // スプライトの内部行列を更新
        }
    }
}

void Text::Draw() {
    // 全てのスプライト（文字）を描画
    for (auto& sprite : sprites_) {
        if (sprite) {
            sprite->Draw();
        }
    }
}