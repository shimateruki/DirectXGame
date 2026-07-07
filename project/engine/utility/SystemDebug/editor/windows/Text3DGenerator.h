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
struct ID3D12Resource;
class Object3d;
class SceneManager;

/// <summary>
/// 入力テキストから3Dモデル用OBJを生成し、シーンへ追加するEditorツール。
/// </summary>
/// 入力テキストから押し出しOBJモデルを生成し、3D文字としてプレビュー・配置するエディタツール。
class Text3DGenerator : public IEditable {
public:
    Text3DGenerator() = default;
    ~Text3DGenerator() override;

    /// モデル生成に必要なシーン参照とエディタ参照を受け取り、フォント情報を準備する。
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    /// 3Dプレビューの自動更新とカメラ前追従位置を毎フレーム更新する。
    void Update();
    /// 編集中だけ一時Objectを描画し、保存データへ混ぜずに見た目を確認する。
    void DrawPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    void DrawImGui() override;
    std::string GetName() override { return "Text 3D Generator"; }

private:
    /// <summary>
    /// DirectWriteで描いた文字のアルファマスク。
    /// </summary>
    /// DirectWriteで描いた文字をOBJ化するためのアルファマスク情報。
    struct TextMask {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> alpha;
    };

    /// <summary>
    /// 生成したモデルファイルと統計情報。
    /// </summary>
    /// 生成された3D文字モデルのパス、サイズ、頂点数などの結果情報。
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
    /// 現在の文字設定をビットマップへ描画し、メッシュ化の元になるマスクを作る。
    bool RenderTextMask(TextMask& outMask);
    bool BuildModelFile(GeneratedModelInfo& outInfo);
    /// アルファマスクを小さな押し出し面の集合へ変換し、OBJファイルとして保存する。
    bool WriteObjFromMask(const TextMask& mask, const std::filesystem::path& objPath, GeneratedModelInfo& outInfo);
    bool WriteReport(const GeneratedModelInfo& info);
    void AddGeneratedModelToScene(const GeneratedModelInfo& info);
    bool BuildPreviewModelFile(GeneratedModelInfo& outInfo);
    void UpdatePreviewModel(float deltaTime);
    void EnsurePreviewObject(const GeneratedModelInfo& info);
    void RemovePreviewObject();
    void MarkPreviewDirty();
    /// 固定位置またはカメラ前方距離から、現在のプレビュー配置座標を計算する。
    Vector3 ResolvePreviewPosition() const;
    void ApplyPreviewTransform();
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
    bool previewAutoUpdate_ = true;
    bool previewAttachToCamera_ = true;
    bool previewDirty_ = false;
    bool previewRequestPending_ = false;
    float previewDelayTimer_ = 0.0f;
    Vector3 previewPosition_ = { 0.0f, 2.0f, 0.0f };
    float previewCameraDistance_ = 5.0f;
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
