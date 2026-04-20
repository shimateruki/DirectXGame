#pragma once
#include "Sprite.h"
#include <string>
#include <vector>
#include <memory>

class SpriteCommon;

/// <summary>
/// スプライトフォントを使用してテキストを描画するクラス
/// </summary>
class Text {
public:
    void Initialize(SpriteCommon* common, const std::string& fontTextureName);
    void Update();
    void Draw();

    /// <summary>
    /// 表示する文字列を設定
    /// </summary>
    void SetString(const std::string& str);

    /// <summary>
    /// 座標を設定
    /// </summary>
    void SetPosition(const Vector2& position);

    /// <summary>
    /// 文字のサイズ（スケール）を設定
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