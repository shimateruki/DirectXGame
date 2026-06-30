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
/// 入力テキストから透過PNGを生成し、Spriteとしてシーンへ追加できるEditorツール。
/// </summary>
class TextSpriteGenerator : public IEditable {
public:
    TextSpriteGenerator() = default;
    ~TextSpriteGenerator() override;

    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void Update();
    void DrawPreview();
    void SetGameViewRegion(const Vector2& offset, const Vector2& size);
    void DrawImGui() override;
    std::string GetName() override { return "Text PNG Generator"; }

private:
    bool EnsureFactories();
    void RefreshFonts();
    bool RenderToFile(const std::string& fullPath, int& outWidth, int& outHeight);
    void UpdatePreviewTexture();
    bool ExportToFile(std::string* outFullPath = nullptr, std::string* outRelativePath = nullptr);
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
    bool previewAutoUpdate_ = false;
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
