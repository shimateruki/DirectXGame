#pragma once

#include "IEditable.h"
#include "engine/utility/math/Math.h"

#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

class DebugEditor;
class SceneManager;
class Sprite;
class SpriteCommon;

/// <summary>
/// 入力テキストから透明PNGを生成し、必要に応じてSpriteとしてシーンへ追加するエディタツール。
/// </summary>
/// 入力テキストを透明PNGとして生成し、Spriteとしてプレビュー・配置できるエディタツール。
class TextSpriteGenerator : public IEditable {
public:
    TextSpriteGenerator() = default;
    ~TextSpriteGenerator() override;

    /// シーンとエディタ参照を保持し、フォント一覧や生成状態を初期化する。
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    /// リアルタイムプレビューの遅延更新や通知タイマーを毎フレーム進める。
    void Update();
    /// Game View上に生成予定のテキストSpriteをプレビュー表示する。
    void DrawPreview();
    void SetGameViewRegion(const Vector2& offset, const Vector2& size);
    /// 文字列、フォント、縁取り、影、出力先などの編集UIを描画する。
    void DrawImGui() override;
    std::string GetName() override { return "Text PNG Generator"; }

private:
    bool EnsureFactories();
    void RefreshFonts();
    /// 現在の設定を外部PNG生成ツールへ渡し、実際の画像ファイルを書き出す。
    bool RenderToFile(const std::string& fullPath, int& outWidth, int& outHeight);
    /// 入力変更後すぐに重い生成を走らせず、短い待ち時間を挟んでプレビュー更新する。
    void TickPreviewAutoUpdate();
    void UpdatePreviewTexture();
    bool ExportToFile(std::string* outFullPath = nullptr, std::string* outRelativePath = nullptr);
    /// 書き出し済みPNGをSpriteとして現在シーンへ追加する。
    void AddPendingSpriteToScene();
    void MarkPreviewDirty();
    void UpdateOutputNameFromText();

private:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;

    // Direct2D/DirectWrite/WICの生成用リソース。
    Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteFontCollectionLoader> resourceFontLoader_;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> resourceFontCollection_;
    std::wstring resourceFontCollectionKey_;
    bool ownsCom_ = false;

    std::vector<std::wstring> fontNamesWide_;
    std::vector<std::string> fontNamesUtf8_;
    std::vector<std::string> fontPaths_;
    int selectedFontIndex_ = 0;

    // UI入力とPNG生成パラメータ。
    char textBuffer_[1024] = "Text";
    char outputNameBuffer_[128] = "text.png";
    char fontFilterBuffer_[128] = "";
    bool autoOutputName_ = true;

    float fontSize_ = 72.0f;
    bool bold_ = true;
    float padding_ = 20.0f;
    bool autoCanvas_ = true;
    int canvasWidth_ = 512;
    int canvasHeight_ = 160;
    float textColor_[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    bool outlineEnabled_ = true;
    float outlineWidth_ = 5.0f;
    float outlineColor_[4] = { 0.05f, 0.06f, 0.08f, 1.0f };

    bool shadowEnabled_ = false;
    float shadowOffset_[2] = { 4.0f, 4.0f };
    float shadowColor_[4] = { 0.0f, 0.0f, 0.0f, 0.45f };

    // GameView上のプレビュー状態。
    bool previewEnabled_ = true;
    bool previewBoundsEnabled_ = true;
    bool previewAutoUpdate_ = true;
    Vector2 previewPosition_ = { 640.0f, 360.0f };
    float previewScale_ = 1.0f;
    bool previewDirty_ = false;
    bool previewRequestPending_ = false;
    float previewDelayTimer_ = 0.0f;
    int previewSerial_ = 0;
    int previewWidth_ = 1;
    int previewHeight_ = 1;
    uint32_t previewTextureHandle_ = 0;
    Vector2 gameViewOffset_ = { 0.0f, 0.0f };
    Vector2 gameViewSize_ = { 0.0f, 0.0f };

    // 書き出し後にシーンへ追加するSprite情報。
    bool pendingAddSprite_ = false;
    std::string pendingSpriteFullPath_;
    std::string pendingSpriteRelativePath_;
    int pendingSpriteWidth_ = 1;
    int pendingSpriteHeight_ = 1;

    std::string exportNoticeMessage_;
    float exportNoticeTimer_ = 0.0f;
    bool exportNoticeSuccess_ = false;
};
