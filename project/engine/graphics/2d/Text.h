#pragma once
#include "Sprite.h"
#include <string>
#include <vector>
#include <memory>

class SpriteCommon;

/// <summary>
/// スプライトフォントを使用してテキストを描画するクラス
/// </summary>
// Textは、フォントテクスチャを複数Spriteへ分割して文字列として描画する簡易テキストクラスです。
class Text {
public:
        // フォント画像を読み込み、文字Spriteを生成できる状態にします。
void Initialize(SpriteCommon* common, const std::string& fontTextureName);
        // 文字列、座標、スケールの変更を各文字Spriteへ反映します。
void Update();
        // 生成済みの文字Spriteを順番に描画します。
void Draw();

    /// <summary>
    /// 表示する文字列を設定
    /// </summary>
        // 表示する文字列を変更し、必要な文字Sprite数へ調整します。
void SetString(const std::string& str);

    /// <summary>
    /// 座標を設定
    /// </summary>
        // テキスト全体の左上基準位置を設定します。
void SetPosition(const Vector2& position);

    /// <summary>
    /// 文字のサイズ（スケール）を設定
    /// </summary>
        // 各文字Spriteへ適用する表示倍率を設定します。
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