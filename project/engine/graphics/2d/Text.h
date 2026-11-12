#pragma once
#include "Sprite.h"
#include <string>
#include <vector>
#include <memory>

namespace engine { class SpriteCommon; }

// 担当：フォント画像を文字ごとのSpriteとして並べ、文字列を描画する。
// 関連：Sprite（文字の描画）、SpriteCommon（共有描画基盤）
// 主な入口：Initialize / SetString / Update / Draw
class Text {
public:
    /// <summary>フォント画像を読み込み、文字Spriteを生成できる状態にする。</summary>
    void Initialize(engine::SpriteCommon* common, const std::string& fontTextureName);

    /// <summary>文字列、座標、スケールの変更を各文字Spriteへ反映する。</summary>
    void Update();

    /// <summary>生成済みの文字Spriteを順に描画する。</summary>
    void Draw();

    /// <summary>
    /// 表示する文字列と必要な文字Sprite数を設定する。
    /// </summary>
    void SetString(const std::string& str);

    /// <summary>
    /// テキスト全体の左上基準位置を設定する。
    /// </summary>
    void SetPosition(const engine::Vector2& position);

    /// <summary>
    /// 各文字Spriteに適用する表示倍率を設定する。
    /// </summary>
    void SetScale(const engine::Vector2& scale);


private:
    engine::SpriteCommon* common_ = nullptr; // 文字Spriteが使う共有描画基盤。
    uint32_t fontTextureHandle_ = 0; // 文字を切り出すフォント画像のハンドル。

    // 1文字あたりのスプライトを保持する
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;

    std::string text_ = "";
    engine::Vector2 position_ = engine::Vector2::ZERO;
    engine::Vector2 scale_ = { 1.0f, 1.0f };

    // フォントテクスチャの1文字分のサイズ
    int fontWidth_ = 16;
    int fontHeight_ = 16; // 一文字の元画像の高さ（ピクセル）。

    // フォントテクスチャの横に並んでいる文字数
    int fontCharsPerRow_ = 7;
};
