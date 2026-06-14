#define NOMINMAX
#include "TextSpriteGenerator.h"

#include "BaseScene.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "EditorManager.h"
#include "IconsFontAwesome5.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteDebugEditor.h"
#include "SRVManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "imgui.h"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <objbase.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {
constexpr const char* kOutputDirectory = "Resources/sprite/generated/text";
constexpr const char* kPreviewDirectory = "Resources/generated/editor/text_preview";
constexpr const char* kResourceDirectory = "Resources";

class ResourceFontFileEnumerator final : public IDWriteFontFileEnumerator {
public:
    ResourceFontFileEnumerator(IDWriteFactory* factory, const void* collectionKey, UINT32 collectionKeySize)
        : factory_(factory) {
        const wchar_t* cursor = static_cast<const wchar_t*>(collectionKey);
        const wchar_t* end = cursor + collectionKeySize / sizeof(wchar_t);
        while (cursor && cursor < end && *cursor != L'\0') {
            std::wstring path(cursor);
            fontPaths_.push_back(path);
            cursor += path.size() + 1;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontFileEnumerator)) {
            *object = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refCount_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = --refCount_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* hasCurrentFile) override {
        if (!hasCurrentFile) return E_POINTER;
        *hasCurrentFile = FALSE;
        currentFile_.Reset();

        while (nextIndex_ < fontPaths_.size()) {
            HRESULT hr = factory_->CreateFontFileReference(fontPaths_[nextIndex_].c_str(), nullptr, currentFile_.GetAddressOf());
            ++nextIndex_;
            if (SUCCEEDED(hr) && currentFile_) {
                *hasCurrentFile = TRUE;
                return S_OK;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** fontFile) override {
        if (!fontFile) return E_POINTER;
        if (!currentFile_) return E_FAIL;
        return currentFile_.CopyTo(fontFile);
    }

private:
    std::atomic<ULONG> refCount_{ 1 };
    Microsoft::WRL::ComPtr<IDWriteFactory> factory_;
    std::vector<std::wstring> fontPaths_;
    size_t nextIndex_ = 0;
    Microsoft::WRL::ComPtr<IDWriteFontFile> currentFile_;
};

class ResourceFontCollectionLoader final : public IDWriteFontCollectionLoader {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontCollectionLoader)) {
            *object = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refCount_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = --refCount_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(
        IDWriteFactory* factory,
        const void* collectionKey,
        UINT32 collectionKeySize,
        IDWriteFontFileEnumerator** fontFileEnumerator) override {
        if (!factory || !collectionKey || !fontFileEnumerator) return E_INVALIDARG;
        *fontFileEnumerator = new ResourceFontFileEnumerator(factory, collectionKey, collectionKeySize);
        return S_OK;
    }

private:
    std::atomic<ULONG> refCount_{ 1 };
};

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string SanitizeOutputName(const char* name) {
    std::string result = (name && name[0] != '\0') ? name : "generated_text.png";
    for (char& c : result) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }

    std::filesystem::path path(result);
    if (path.extension().empty()) {
        result += ".png";
    } else if (ToLowerAscii(path.extension().string()) != ".png") {
        path.replace_extension(".png");
        result = path.generic_string();
    }
    return result;
}

void CopyToBuffer(char* buffer, size_t bufferSize, const std::string& text) {
    if (!buffer || bufferSize == 0) return;
    std::snprintf(buffer, bufferSize, "%s", text.c_str());
}

uint32_t Fnv1aHash(const std::string& text) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

std::string MakeOutputNameFromText(const char* text) {
    std::string source = text ? text : "";
    std::string firstLine;
    for (char c : source) {
        if (c == '\r' || c == '\n') break;
        firstLine.push_back(c);
    }

    size_t begin = firstLine.find_first_not_of(" \t");
    size_t end = firstLine.find_last_not_of(" \t");
    firstLine = (begin == std::string::npos) ? "" : firstLine.substr(begin, end - begin + 1);

    std::string slug;
    bool previousUnderscore = false;
    for (unsigned char c : firstLine) {
        char out = '\0';
        if (std::isalnum(c)) {
            out = static_cast<char>(std::tolower(c));
        } else if (c == ' ' || c == '_' || c == '-') {
            out = '_';
        }

        if (out == '_') {
            if (previousUnderscore) continue;
            previousUnderscore = true;
        } else if (out != '\0') {
            previousUnderscore = false;
        }

        if (out != '\0') {
            slug.push_back(out);
            if (slug.size() >= 32) break;
        }
    }

    while (!slug.empty() && slug.back() == '_') {
        slug.pop_back();
    }
    if (slug.empty()) {
        slug = "text";
    }

    char hashText[16];
    std::snprintf(hashText, sizeof(hashText), "%08x", Fnv1aHash(source));
    return "text_" + slug + "_" + hashText + ".png";
}

D2D1_COLOR_F ToD2DColor(const float color[4]) {
    return D2D1::ColorF(color[0], color[1], color[2], color[3]);
}

ImU32 ToImGuiColor(const float color[4]) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], color[3]));
}

int ClampCanvasSize(float value) {
    return std::clamp(static_cast<int>(std::ceil(value)), 1, 4096);
}

bool ContainsFilter(const std::string& text, const std::string& filter) {
    if (filter.empty()) return true;
    return ToLowerAscii(text).find(ToLowerAscii(filter)) != std::string::npos;
}

bool IsFontFile(const std::filesystem::path& path) {
    std::string extension = ToLowerAscii(path.extension().generic_string());
    return extension == ".ttf" || extension == ".otf" || extension == ".ttc";
}

std::vector<std::wstring> CollectResourceFontPaths() {
    std::vector<std::wstring> paths;
    std::error_code ec;
    if (!std::filesystem::exists(kResourceDirectory, ec)) return paths;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(kResourceDirectory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (!IsFontFile(entry.path())) continue;
        paths.push_back(std::filesystem::absolute(entry.path(), ec).wstring());
    }

    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

std::wstring MakeFontCollectionKey(const std::vector<std::wstring>& paths) {
    std::wstring key;
    for (const std::wstring& path : paths) {
        key += path;
        key.push_back(L'\0');
    }
    key.push_back(L'\0');
    return key;
}

std::string MakeRelativeSpritePath(const std::string& fullPath) {
    std::filesystem::path base = "Resources/sprite";
    std::filesystem::path path = fullPath;
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(path, base, ec);
    if (ec) {
        return path.filename().generic_string();
    }
    return relative.generic_string();
}
}

TextSpriteGenerator::~TextSpriteGenerator() {
    if (dwriteFactory_ && resourceFontLoader_) {
        dwriteFactory_->UnregisterFontCollectionLoader(resourceFontLoader_.Get());
    }
    resourceFontCollection_.Reset();
    resourceFontLoader_.Reset();
    dwriteFactory_.Reset();
    d2dFactory_.Reset();
    wicFactory_.Reset();
    if (ownsCom_) {
        CoUninitialize();
        ownsCom_ = false;
    }
}

void TextSpriteGenerator::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
    std::filesystem::create_directories(kOutputDirectory);
    std::filesystem::create_directories(kPreviewDirectory);
    EnsureFactories();
    RefreshFonts();
    UpdateOutputNameFromText();
    previewDirty_ = false;
    previewRequestPending_ = false;
    previewDelayTimer_ = 0.0f;
}

void TextSpriteGenerator::Update() {
#ifdef USE_IMGUI
    if (exportNoticeTimer_ > 0.0f) {
        exportNoticeTimer_ = std::max(0.0f, exportNoticeTimer_ - (1.0f / 60.0f));
    }
#endif
}

void TextSpriteGenerator::DrawPreview() {
#ifdef USE_IMGUI
    if (!previewEnabled_) return;
    if (EditorManager::GetInstance()->GetSelectedObject() != this) return;
    if (textBuffer_[0] == '\0') return;
    if (gameViewSize_.x <= 0.0f || gameViewSize_.y <= 0.0f) return;

    float scaleX = gameViewSize_.x / static_cast<float>(WinApp::kClientWidth);
    float scaleY = gameViewSize_.y / static_cast<float>(WinApp::kClientHeight);
    float viewScale = std::min(scaleX, scaleY);
    float sourceFontSize = std::max(4.0f, fontSize_);
    float drawScale = previewScale_ * viewScale;
    float previewFontSize = std::max(4.0f, sourceFontSize * drawScale);

    ImVec2 center = {
        gameViewOffset_.x + previewPosition_.x * scaleX,
        gameViewOffset_.y + previewPosition_.y * scaleY,
    };

    ImFont* font = ImGui::GetFont();
    ImVec2 sourceTextSize = font->CalcTextSizeA(sourceFontSize, FLT_MAX, 0.0f, textBuffer_);
    float outlinePad = outlineEnabled_ ? std::max(0.0f, outlineWidth_) : 0.0f;
    float shadowPadX = shadowEnabled_ ? std::abs(shadowOffset_[0]) : 0.0f;
    float shadowPadY = shadowEnabled_ ? std::abs(shadowOffset_[1]) : 0.0f;
    float extraPadX = padding_ * 2.0f + outlinePad * 2.0f + shadowPadX;
    float extraPadY = padding_ * 2.0f + outlinePad * 2.0f + shadowPadY;

    int sourceCanvasWidth = autoCanvas_ ? ClampCanvasSize(sourceTextSize.x + extraPadX) : std::clamp(canvasWidth_, 1, 4096);
    int sourceCanvasHeight = autoCanvas_ ? ClampCanvasSize(sourceTextSize.y + extraPadY) : std::clamp(canvasHeight_, 1, 4096);
    ImVec2 canvasSize = {
        static_cast<float>(sourceCanvasWidth) * drawScale,
        static_cast<float>(sourceCanvasHeight) * drawScale,
    };
    ImVec2 canvasMin = { center.x - canvasSize.x * 0.5f, center.y - canvasSize.y * 0.5f };
    ImVec2 canvasMax = { canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y };
    ImVec2 pos = {
        canvasMin.x + (padding_ + outlinePad + (shadowEnabled_ ? std::max(0.0f, -shadowOffset_[0]) : 0.0f)) * drawScale,
        canvasMin.y + (padding_ + outlinePad + (shadowEnabled_ ? std::max(0.0f, -shadowOffset_[1]) : 0.0f)) * drawScale,
    };
    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    if (previewBoundsEnabled_) {
        drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(32, 48, 64, 28));
        drawList->AddRect(canvasMin, canvasMax, IM_COL32(80, 210, 255, 180), 0.0f, 0, 1.5f);
    }

    drawList->PushClipRect(canvasMin, canvasMax, true);

    if (shadowEnabled_) {
        ImVec2 shadowPos = {
            pos.x + shadowOffset_[0] * drawScale,
            pos.y + shadowOffset_[1] * drawScale,
        };
        drawList->AddText(font, previewFontSize, shadowPos, ToImGuiColor(shadowColor_), textBuffer_);
    }

    if (outlineEnabled_ && outlineWidth_ > 0.0f) {
        int radius = std::clamp(static_cast<int>(std::ceil(outlineWidth_ * drawScale)), 1, 16);
        ImU32 outlineColor = ToImGuiColor(outlineColor_);
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x == 0 && y == 0) continue;
                if ((x * x + y * y) > radius * radius) continue;
                drawList->AddText(font, previewFontSize, { pos.x + static_cast<float>(x), pos.y + static_cast<float>(y) }, outlineColor, textBuffer_);
            }
        }
    }

    drawList->AddText(font, previewFontSize, pos, ToImGuiColor(textColor_), textBuffer_);
    drawList->PopClipRect();
#endif
}

void TextSpriteGenerator::SetGameViewRegion(const Vector2& offset, const Vector2& size) {
    gameViewOffset_ = offset;
    gameViewSize_ = size;
}

void TextSpriteGenerator::DrawImGui() {
#ifdef USE_IMGUI
    bool textureChanged = false;

    ImGui::Text(ICON_FA_FONT " テキストPNG生成");
    ImGui::TextDisabled("透明PNGとして出力し、必要ならSpriteとしてシーンに追加できます。");
    ImGui::Separator();

    ImGui::Checkbox("Game Viewにプレビュー", &previewEnabled_);
    ImGui::SameLine();
    ImGui::TextDisabled("入力中に即時反映");
    ImGui::Checkbox("透明PNG範囲を表示", &previewBoundsEnabled_);
    ImGui::DragFloat2("プレビュー位置", &previewPosition_.x, 1.0f, -4096.0f, 4096.0f);
    ImGui::DragFloat("プレビュー倍率", &previewScale_, 0.01f, 0.05f, 8.0f, "%.2f");
    ImGui::Separator();

    if (ImGui::InputTextMultiline("文字", textBuffer_, sizeof(textBuffer_), ImVec2(-1.0f, 96.0f))) {
        textureChanged = true;
        if (autoOutputName_) {
            UpdateOutputNameFromText();
        }
    }

    if (ImGui::Button(ICON_FA_SYNC_ALT " フォント再取得")) {
        RefreshFonts();
        textureChanged = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("フォント検索", fontFilterBuffer_, sizeof(fontFilterBuffer_));

    const char* selectedFont = fontNamesUtf8_.empty() ? "Meiryo" : fontNamesUtf8_[selectedFontIndex_].c_str();
    if (ImGui::BeginCombo("フォント", selectedFont)) {
        std::string filter = fontFilterBuffer_;
        for (int i = 0; i < static_cast<int>(fontNamesUtf8_.size()); ++i) {
            if (!ContainsFilter(fontNamesUtf8_[i], filter)) continue;
            bool selected = (i == selectedFontIndex_);
            if (ImGui::Selectable(fontNamesUtf8_[i].c_str(), selected)) {
                selectedFontIndex_ = i;
                textureChanged = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    textureChanged |= ImGui::DragFloat("フォントサイズ", &fontSize_, 1.0f, 4.0f, 320.0f, "%.0f");
    textureChanged |= ImGui::DragFloat("余白", &padding_, 1.0f, 0.0f, 256.0f, "%.0f");
    textureChanged |= ImGui::ColorEdit4("文字色", textColor_);
    textureChanged |= ImGui::Checkbox("自動キャンバス", &autoCanvas_);
    if (!autoCanvas_) {
        textureChanged |= ImGui::DragInt("幅", &canvasWidth_, 1.0f, 1, 4096);
        textureChanged |= ImGui::DragInt("高さ", &canvasHeight_, 1.0f, 1, 4096);
    }

    ImGui::Separator();
    textureChanged |= ImGui::Checkbox("アウトライン", &outlineEnabled_);
    if (outlineEnabled_) {
        textureChanged |= ImGui::DragFloat("アウトライン幅", &outlineWidth_, 0.25f, 0.0f, 32.0f, "%.2f");
        textureChanged |= ImGui::ColorEdit4("アウトライン色", outlineColor_);
    }

    textureChanged |= ImGui::Checkbox("影", &shadowEnabled_);
    if (shadowEnabled_) {
        textureChanged |= ImGui::DragFloat2("影オフセット", shadowOffset_, 0.5f, -128.0f, 128.0f);
        textureChanged |= ImGui::ColorEdit4("影色", shadowColor_);
    }

    ImGui::Separator();
    if (ImGui::Checkbox("出力名を文字から自動生成", &autoOutputName_)) {
        if (autoOutputName_) {
            UpdateOutputNameFromText();
        }
    }
    if (ImGui::InputText("出力名", outputNameBuffer_, sizeof(outputNameBuffer_))) {
        autoOutputName_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("文字から更新")) {
        autoOutputName_ = true;
        UpdateOutputNameFromText();
    }

    if (ImGui::Button(ICON_FA_FILE_EXPORT " PNG出力")) {
        ExportToFile();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS " Spriteとして追加")) {
        std::string fullPath;
        std::string relativePath;
        if (ExportToFile(&fullPath, &relativePath)) {
            pendingSpriteFullPath_ = fullPath;
            pendingSpriteRelativePath_ = relativePath;
            AddPendingSpriteToScene();
        }
    }

    if (exportNoticeTimer_ > 0.0f && !exportNoticeMessage_.empty()) {
        const ImVec4 noticeColor = exportNoticeSuccess_
            ? ImVec4(0.36f, 1.0f, 0.58f, 1.0f)
            : ImVec4(1.0f, 0.36f, 0.28f, 1.0f);
        ImGui::TextColored(noticeColor, "%s", exportNoticeMessage_.c_str());
    }

    ImGui::TextDisabled("生成先: %s", kOutputDirectory);
    ImGui::TextDisabled("Game ViewプレビューはPNG保存せずに軽量描画します。");
    ImGui::TextDisabled("フォント参照元: Resources内の .ttf / .otf / .ttc");

    if (textureChanged) {
        MarkPreviewDirty();
    }
#endif
}

bool TextSpriteGenerator::EnsureFactories() {
    if (!wicFactory_) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == S_OK || hr == S_FALSE) {
            ownsCom_ = true;
        } else if (hr != RPC_E_CHANGED_MODE) {
            DebugConsole::GetInstance()->AddLog("Text PNG: COM initialize failed.");
            return false;
        }

        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            DebugConsole::GetInstance()->AddLog("Text PNG: WIC factory failed.");
            return false;
        }
    }

    if (!d2dFactory_) {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
        if (FAILED(hr)) {
            DebugConsole::GetInstance()->AddLog("Text PNG: D2D factory failed.");
            return false;
        }
    }

    if (!dwriteFactory_) {
        HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            DebugConsole::GetInstance()->AddLog("Text PNG: DirectWrite factory failed.");
            return false;
        }
    }

    return true;
}

void TextSpriteGenerator::RefreshFonts() {
    if (!EnsureFactories()) return;

    std::string previousFont = fontNamesUtf8_.empty()
        ? std::string()
        : fontNamesUtf8_[std::clamp(selectedFontIndex_, 0, static_cast<int>(fontNamesUtf8_.size()) - 1)];

    fontNamesWide_.clear();
    fontNamesUtf8_.clear();
    resourceFontCollection_.Reset();

    std::vector<std::wstring> resourceFontPaths = CollectResourceFontPaths();
    if (resourceFontPaths.empty()) {
        DebugConsole::GetInstance()->AddLog("Text PNG: no font files found in Resources.");
        fontNamesWide_.push_back(L"Meiryo");
        fontNamesUtf8_.push_back("Meiryo");
        selectedFontIndex_ = 0;
        return;
    }

    if (!resourceFontLoader_) {
        resourceFontLoader_.Attach(new ResourceFontCollectionLoader());
        HRESULT registerHr = dwriteFactory_->RegisterFontCollectionLoader(resourceFontLoader_.Get());
        if (FAILED(registerHr)) {
            DebugConsole::GetInstance()->AddLog("Text PNG: resource font loader registration failed.");
            resourceFontLoader_.Reset();
            return;
        }
    }

    resourceFontCollectionKey_ = MakeFontCollectionKey(resourceFontPaths);
    HRESULT hr = dwriteFactory_->CreateCustomFontCollection(
        resourceFontLoader_.Get(),
        resourceFontCollectionKey_.data(),
        static_cast<UINT32>(resourceFontCollectionKey_.size() * sizeof(wchar_t)),
        resourceFontCollection_.GetAddressOf());
    if (FAILED(hr) || !resourceFontCollection_) {
        DebugConsole::GetInstance()->AddLog("Text PNG: resource font collection failed.");
        fontNamesWide_.push_back(L"Meiryo");
        fontNamesUtf8_.push_back("Meiryo");
        selectedFontIndex_ = 0;
        return;
    }

    IDWriteFontCollection* collection = resourceFontCollection_.Get();
    UINT32 count = collection->GetFontFamilyCount();
    fontNamesWide_.reserve(count);
    fontNamesUtf8_.reserve(count);

    for (UINT32 i = 0; i < count; ++i) {
        Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        if (FAILED(collection->GetFontFamily(i, family.GetAddressOf()))) continue;

        Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(family->GetFamilyNames(names.GetAddressOf()))) continue;

        UINT32 index = 0;
        BOOL exists = FALSE;
        names->FindLocaleName(L"ja-jp", &index, &exists);
        if (!exists) names->FindLocaleName(L"en-us", &index, &exists);
        if (!exists) index = 0;

        UINT32 length = 0;
        if (FAILED(names->GetStringLength(index, &length))) continue;
        std::wstring name(length + 1, L'\0');
        if (FAILED(names->GetString(index, name.data(), length + 1))) continue;
        name.resize(length);

        fontNamesWide_.push_back(name);
        fontNamesUtf8_.push_back(WideToUtf8(name));
    }

    if (fontNamesWide_.empty()) {
        fontNamesWide_.push_back(L"Meiryo");
        fontNamesUtf8_.push_back("Meiryo");
    }

    selectedFontIndex_ = 0;
    if (!previousFont.empty()) {
        for (int i = 0; i < static_cast<int>(fontNamesUtf8_.size()); ++i) {
            if (fontNamesUtf8_[i] == previousFont) {
                selectedFontIndex_ = i;
                MarkPreviewDirty();
                return;
            }
        }
    }

    for (int i = 0; i < static_cast<int>(fontNamesUtf8_.size()); ++i) {
        std::string lower = ToLowerAscii(fontNamesUtf8_[i]);
        if (lower.find("meiryo") != std::string::npos) {
            selectedFontIndex_ = i;
            break;
        }
    }
    MarkPreviewDirty();
}

bool TextSpriteGenerator::RenderToFile(const std::string& fullPath, int& outWidth, int& outHeight) {
    if (!EnsureFactories()) return false;

    std::filesystem::create_directories(std::filesystem::path(fullPath).parent_path());

    std::wstring text = Utf8ToWide(textBuffer_);
    if (text.empty()) text = L" ";

    std::wstring fontName = fontNamesWide_.empty()
        ? std::wstring(L"Meiryo")
        : fontNamesWide_[std::clamp(selectedFontIndex_, 0, static_cast<int>(fontNamesWide_.size()) - 1)];

    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
    HRESULT hr = dwriteFactory_->CreateTextFormat(
        fontName.c_str(),
        resourceFontCollection_.Get(),
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        std::max(4.0f, fontSize_),
        L"ja-jp",
        textFormat.GetAddressOf());
    if (FAILED(hr)) return false;

    textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    float outlinePad = outlineEnabled_ ? std::max(0.0f, outlineWidth_) : 0.0f;
    float shadowPadX = shadowEnabled_ ? std::abs(shadowOffset_[0]) : 0.0f;
    float shadowPadY = shadowEnabled_ ? std::abs(shadowOffset_[1]) : 0.0f;
    float extraPadX = padding_ * 2.0f + outlinePad * 2.0f + shadowPadX;
    float extraPadY = padding_ * 2.0f + outlinePad * 2.0f + shadowPadY;

    float layoutWidth = autoCanvas_ ? 4096.0f : std::max(1.0f, static_cast<float>(canvasWidth_) - extraPadX);
    float layoutHeight = autoCanvas_ ? 2048.0f : std::max(1.0f, static_cast<float>(canvasHeight_) - extraPadY);

    Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
    hr = dwriteFactory_->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        textFormat.Get(),
        layoutWidth,
        layoutHeight,
        textLayout.GetAddressOf());
    if (FAILED(hr)) return false;

    DWRITE_TEXT_METRICS metrics{};
    textLayout->GetMetrics(&metrics);

    int width = autoCanvas_ ? ClampCanvasSize(metrics.left + metrics.widthIncludingTrailingWhitespace + extraPadX) : std::clamp(canvasWidth_, 1, 4096);
    int height = autoCanvas_ ? ClampCanvasSize(metrics.top + metrics.height + extraPadY) : std::clamp(canvasHeight_, 1, 4096);
    outWidth = width;
    outHeight = height;

    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    hr = wicFactory_->CreateBitmap(
        static_cast<UINT>(width),
        static_cast<UINT>(height),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnLoad,
        bitmap.GetAddressOf());
    if (FAILED(hr)) return false;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);

    Microsoft::WRL::ComPtr<ID2D1RenderTarget> renderTarget;
    hr = d2dFactory_->CreateWicBitmapRenderTarget(bitmap.Get(), props, renderTarget.GetAddressOf());
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> outlineBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush;
    renderTarget->CreateSolidColorBrush(ToD2DColor(textColor_), textBrush.GetAddressOf());
    renderTarget->CreateSolidColorBrush(ToD2DColor(outlineColor_), outlineBrush.GetAddressOf());
    renderTarget->CreateSolidColorBrush(ToD2DColor(shadowColor_), shadowBrush.GetAddressOf());

    float originX = padding_ + outlinePad - metrics.left + (shadowEnabled_ ? std::max(0.0f, -shadowOffset_[0]) : 0.0f);
    float originY = padding_ + outlinePad - metrics.top + (shadowEnabled_ ? std::max(0.0f, -shadowOffset_[1]) : 0.0f);

    renderTarget->BeginDraw();
    renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    if (shadowEnabled_) {
        D2D1_POINT_2F shadowOrigin = D2D1::Point2F(originX + shadowOffset_[0], originY + shadowOffset_[1]);
        renderTarget->DrawTextLayout(shadowOrigin, textLayout.Get(), shadowBrush.Get());
    }

    if (outlineEnabled_ && outlineWidth_ > 0.0f) {
        int radius = std::clamp(static_cast<int>(std::ceil(outlineWidth_)), 1, 64);
        float radiusSq = outlineWidth_ * outlineWidth_;
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x == 0 && y == 0) continue;
                float distSq = static_cast<float>(x * x + y * y);
                if (distSq > radiusSq + 0.5f) continue;
                D2D1_POINT_2F point = D2D1::Point2F(originX + static_cast<float>(x), originY + static_cast<float>(y));
                renderTarget->DrawTextLayout(point, textLayout.Get(), outlineBrush.Get());
            }
        }
    }

    renderTarget->DrawTextLayout(D2D1::Point2F(originX, originY), textLayout.Get(), textBrush.Get());
    hr = renderTarget->EndDraw();
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IWICStream> stream;
    hr = wicFactory_->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) return false;

    std::wstring widePath = Utf8ToWide(std::filesystem::path(fullPath).generic_string());
    hr = stream->InitializeFromFilename(widePath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    hr = wicFactory_->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
    if (FAILED(hr)) return false;

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) return false;

    hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
    if (FAILED(hr)) return false;

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) return false;

    hr = frame->WriteSource(bitmap.Get(), nullptr);
    if (FAILED(hr)) return false;

    hr = frame->Commit();
    if (FAILED(hr)) return false;

    hr = encoder->Commit();
    return SUCCEEDED(hr);
}

void TextSpriteGenerator::UpdatePreviewTexture() {
    if (!previewDirty_) return;
    if (textBuffer_[0] == '\0') {
        previewTextureHandle_ = 0;
        previewWidth_ = 1;
        previewHeight_ = 1;
        previewDirty_ = false;
        return;
    }

    char fileName[64];
    std::snprintf(fileName, sizeof(fileName), "_preview_%04d.png", previewSerial_++ % 4);
    std::filesystem::path path = std::filesystem::path(kPreviewDirectory) / fileName;

    int width = 1;
    int height = 1;
    if (!RenderToFile(path.generic_string(), width, height)) {
        DebugConsole::GetInstance()->AddLog("Text PNG: preview render failed.");
        return;
    }

    uint32_t handle = TextureManager::GetInstance()->Load(path.generic_string(), false, false, true);
    if (handle == 0) return;

    previewWidth_ = width;
    previewHeight_ = height;
    previewTextureHandle_ = handle;
    previewDirty_ = false;
}

bool TextSpriteGenerator::ExportToFile(std::string* outFullPath, std::string* outRelativePath) {
    std::string fileName = SanitizeOutputName(outputNameBuffer_);
    std::filesystem::path fullPath = std::filesystem::path(kOutputDirectory) / fileName;

    int width = 1;
    int height = 1;
    if (!RenderToFile(fullPath.generic_string(), width, height)) {
        DebugConsole::GetInstance()->AddLog("Text PNG: export failed.");
        exportNoticeSuccess_ = false;
        exportNoticeTimer_ = 2.5f;
        exportNoticeMessage_ = "PNG出力に失敗しました";
        return false;
    }

    pendingSpriteWidth_ = width;
    pendingSpriteHeight_ = height;

    if (outFullPath) *outFullPath = fullPath.generic_string();
    if (outRelativePath) *outRelativePath = MakeRelativeSpritePath(fullPath.generic_string());

    DebugConsole::GetInstance()->AddLog("Text PNG exported: " + fullPath.generic_string());
    exportNoticeSuccess_ = true;
    exportNoticeTimer_ = 2.5f;
    exportNoticeMessage_ = "PNG出力完了: " + fileName;
    return true;
}

void TextSpriteGenerator::AddPendingSpriteToScene() {
    pendingAddSprite_ = false;

    if (!sceneManager_) return;
    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene || !scene->GetSpriteCommon()) return;

    uint32_t handle = TextureManager::GetInstance()->Load(pendingSpriteFullPath_, false, false, true);
    if (handle == 0) return;

    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(scene->GetSpriteCommon(), handle);
    sprite->SetTextureName(pendingSpriteRelativePath_);
    sprite->SetName(std::filesystem::path(pendingSpriteRelativePath_).stem().generic_string());
    sprite->SetAnchorPoint({ 0.5f, 0.5f });
    sprite->SetPosition(previewPosition_);
    sprite->SetSize({ pendingSpriteWidth_ * previewScale_, pendingSpriteHeight_ * previewScale_ });
    sprite->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    sprite->Update();

    Sprite* addedSprite = sprite.get();
    scene->GetSprites().push_back(std::move(sprite));

    if (editor_ && editor_->GetSpriteDebugEditor()) {
        editor_->GetSpriteDebugEditor()->SetSelectedSprite(addedSprite);
        EditorManager::GetInstance()->SetSelectedObject(editor_->GetSpriteDebugEditor());
    }

    DebugConsole::GetInstance()->AddLog("Text PNG sprite added: " + pendingSpriteRelativePath_);
}

void TextSpriteGenerator::MarkPreviewDirty() {
    previewDirty_ = true;
}

void TextSpriteGenerator::UpdateOutputNameFromText() {
    CopyToBuffer(outputNameBuffer_, sizeof(outputNameBuffer_), MakeOutputNameFromText(textBuffer_));
}
