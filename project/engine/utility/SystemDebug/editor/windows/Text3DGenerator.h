#pragma once

#include "IEditable.h"
#include "engine/utility/math/Math.h"

#include <filesystem>
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
class Object3d;
class SceneManager;

/// <summary>
/// 入力テキストから3Dモデル用OBJを生成し、シーンへ追加するEditorツール。
/// </summary>
class Text3DGenerator : public IEditable {
public:
    Text3DGenerator() = default;
    ~Text3DGenerator() override;

    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void Update();
    void DrawImGui() override;
    std::string GetName() override { return "Text 3D Generator"; }

private:
    /// <summary>
    /// DirectWriteで描いた文字のアルファマスク。
    /// </summary>
    struct TextMask {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> alpha;
    };

    /// <summary>
    /// 生成したモデルファイルと統計情報。
    /// </summary>
    struct GeneratedModelInfo {
        std::string modelName;
        std::string objPath;
        std::string reportPath;
        int width = 0;
        int height = 0;
        int filledCells = 0;
        int vertexCount = 0;
        int faceCount = 0;
    };

    bool EnsureFactories();
    void RefreshFonts();
    bool RenderTextMask(TextMask& outMask);
    bool BuildModelFile(GeneratedModelInfo& outInfo);
    bool WriteObjFromMask(const TextMask& mask, const std::filesystem::path& objPath, GeneratedModelInfo& outInfo);
    bool WriteReport(const GeneratedModelInfo& info);
    void AddGeneratedModelToScene(const GeneratedModelInfo& info);
    bool BuildPreviewModelFile(GeneratedModelInfo& outInfo);
    void UpdatePreviewModel(float deltaTime);
    void EnsurePreviewObject(const GeneratedModelInfo& info);
    void RemovePreviewObject();
    void MarkPreviewDirty();
    Object3d* FindPreviewObject() const;
    void UpdateOutputNameFromText();
    void SetNotice(const std::string& message, bool success);

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
    int selectedFontIndex_ = 0;

    // UI入力と生成パラメータ。
    char textBuffer_[1024] = "Text";
    char outputNameBuffer_[128] = "text3d";
    char fontFilterBuffer_[128] = "";
    bool autoOutputName_ = true;

    float fontSize_ = 128.0f;
    float padding_ = 18.0f;
    int sampleStep_ = 4;
    int alphaThreshold_ = 48;
    float modelHeight_ = 2.2f;
    float thickness_ = 0.22f;
    bool bold_ = true;
    bool centerOrigin_ = true;
    float modelColor_[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool previewEnabled_ = true;
    bool previewDirty_ = true;
    bool previewRequestPending_ = false;
    float previewDelayTimer_ = 0.0f;
    Vector3 previewPosition_ = { 0.0f, 2.0f, 0.0f };
    float previewScale_ = 1.0f;
    Object3d* previewObject_ = nullptr;
    GeneratedModelInfo lastPreview_;
    bool hasPreviewModel_ = false;

    GeneratedModelInfo lastGenerated_;
    bool hasGeneratedModel_ = false;

    std::string noticeMessage_;
    float noticeTimer_ = 0.0f;
    bool noticeSuccess_ = false;
};
