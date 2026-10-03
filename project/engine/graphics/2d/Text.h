#pragma once
#include "Sprite.h"
#include <string>
#include <vector>
#include <memory>

class SpriteCommon;

/// <summary>
/// フォント画像を文字ごとのSpriteへ分けて描画する。
/// </summary>
class Text {
public:
    /// <summary>フォント画像を読み込み、文字Spriteを生成できる状態にする。</summary>
    void Initialize(SpriteCommon* common, const std::string& fontTextureName);

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
    void SetPosition(const Vector2& position);

    /// <summary>
    /// 各文字Spriteに適用する表示倍率を設定する。
    /// </summary>
    void SetScale(const Vector2& scale);


private:
    SpriteCommon* common_ = nullptr;
    uint32_t fontTextureHandle_ = 0;

    // 1文字あたりのスプライトを保持する
    std::vector<std::unique_ptr<Sprite>> sprites_;

    std::string text_ = "";
    Vector2 position_ = { 0.0f, 0.0f };
    Vector2 scale_ = { 1.0f, 1.0f };

    // フォントテクスチャの1文字分のサイズ
    int fontWidth_ = 16;
    int fontHeight_ = 16;

    // フォントテクスチャの横に並んでいる文字数
    int fontCharsPerRow_ = 7;
};
