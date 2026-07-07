#define NOMINMAX
#include "Text3DGenerator.h"

#include "BaseScene.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "EditorManager.h"
#include "IconsFontAwesome5.h"
#include "CameraManager.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"
#include "json.hpp"

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <objbase.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {
namespace fs = std::filesystem;

constexpr const char* kOutputRoot = "Resources/3DModel/GeneratedText";
constexpr const char* kResourceRoot = "Resources";
constexpr const char* kPreviewStem = "_preview_text3d";
constexpr const char* kPreviewModelName = "GeneratedText/_preview_text3d";
constexpr const char* kPreviewObjectName = "__Editor_Text3DPreview";
constexpr float kPreviewRebuildDelay = 0.22f;
constexpr int kPreviewMaxVertices = 240000;
constexpr int kGeneratedMaxVertices = 480000;

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
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) return "";
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
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

uint32_t Fnv1aHash(const std::string& text) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

void CopyToBuffer(char* buffer, size_t bufferSize, const std::string& text) {
    if (!buffer || bufferSize == 0) return;
    std::snprintf(buffer, bufferSize, "%s", text.c_str());
}

bool ContainsFilter(const std::string& text, const std::string& filter) {
    if (filter.empty()) return true;
    return ToLowerAscii(text).find(ToLowerAscii(filter)) != std::string::npos;
}

bool IsFontFile(const fs::path& path) {
    std::string extension = ToLowerAscii(path.extension().generic_string());
    return extension == ".ttf" || extension == ".otf" || extension == ".ttc";
}

std::vector<std::wstring> CollectResourceFontPaths() {
    std::vector<std::wstring> paths;
    std::error_code ec;
    if (!fs::exists(kResourceRoot, ec)) return paths;

    for (const auto& entry : fs::recursive_directory_iterator(kResourceRoot, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (!IsFontFile(entry.path())) continue;
        paths.push_back(fs::absolute(entry.path(), ec).wstring());
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

std::string MakeSafeStemFromText(const char* text) {
    const std::string source = text ? text : "";
    std::string firstLine;
    for (char c : source) {
        if (c == '\r' || c == '\n') break;
        firstLine.push_back(c);
    }

    std::string stem;
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
            stem.push_back(out);
            if (stem.size() >= 32) break;
        }
    }

    while (!stem.empty() && stem.back() == '_') {
        stem.pop_back();
    }
    if (stem.empty()) {
        stem = "text3d";
    }

    char hashText[16]{};
    std::snprintf(hashText, sizeof(hashText), "%08x", Fnv1aHash(source));
    return stem + "_" + hashText;
}

std::string SanitizeModelStem(const char* name, const char* fallbackText) {
    std::string stem = (name && name[0] != '\0') ? name : MakeSafeStemFromText(fallbackText);
    stem = fs::path(stem).stem().generic_string();
    for (char& c : stem) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            c = '_';
        }
    }
    if (stem.empty()) {
        stem = MakeSafeStemFromText(fallbackText);
    }
    return stem;
}

int ClampCanvasSize(float value) {
    return std::clamp(static_cast<int>(std::ceil(value)), 1, 4096);
}

std::string MakeUniqueObjectName(BaseScene* scene, const std::string& baseName) {
    if (!scene) return baseName;

    auto exists = [&](const std::string& name) {
        for (const auto& object : scene->GetObjects()) {
            if (object && object->GetName() == name) {
                return true;
            }
        }
        return false;
    };

    if (!exists(baseName)) {
        return baseName;
    }

    for (int index = 1; index < 10000; ++index) {
        std::string candidate = baseName + "_" + std::to_string(index);
        if (!exists(candidate)) {
            return candidate;
        }
    }
    return baseName + "_copy";
}

struct ObjVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ObjUv {
    float u = 0.0f;
    float v = 0.0f;
};

struct ObjFace {
    std::array<int, 4> v{};
    std::array<int, 4> vt{};
    int vn = 0;
};

void AddQuad(
    const std::array<ObjVertex, 4>& positions,
    const std::array<ObjUv, 4>& uvs,
    const ObjVertex& normal,
    std::vector<ObjVertex>& outVertices,
    std::vector<ObjUv>& outUvs,
    std::vector<ObjVertex>& outNormals,
    std::vector<ObjFace>& outFaces) {
    ObjFace face;
    for (int i = 0; i < 4; ++i) {
        outVertices.push_back(positions[i]);
        outUvs.push_back(uvs[i]);
        face.v[i] = static_cast<int>(outVertices.size());
        face.vt[i] = static_cast<int>(outUvs.size());
    }
    outNormals.push_back(normal);
    face.vn = static_cast<int>(outNormals.size());
    outFaces.push_back(face);
}
}

Text3DGenerator::~Text3DGenerator() {
    RemovePreviewObject();
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

void Text3DGenerator::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
    fs::create_directories(kOutputRoot);
    EnsureFactories();
    RefreshFonts();
    UpdateOutputNameFromText();
    previewDirty_ = true;
    previewRequestPending_ = true;
    previewDelayTimer_ = 0.0f;
}

void Text3DGenerator::Update() {
#ifdef USE_IMGUI
    if (noticeTimer_ > 0.0f) {
        noticeTimer_ = std::max(0.0f, noticeTimer_ - (1.0f / 60.0f));
    }
    UpdatePreviewModel(1.0f / 60.0f);

    // カメラ追従プレビューは、カメラ操作に合わせて毎フレーム位置だけ更新します。
    if (previewAttachToCamera_ && previewObject_ &&
        EditorManager::GetInstance()->GetSelectedObject() == this) {
        ApplyPreviewTransform();
    }
#endif
}

void Text3DGenerator::DrawPreview(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
#ifdef USE_IMGUI
    if (!previewEnabled_) return;
    if (EditorManager::GetInstance()->GetSelectedObject() != this) return;
    if (!previewObject_ || !previewObject_->GetIsVisible()) return;

    // シーン側の描画対象に入らない場合でも、エディタプレビューとして確実に描画する。
    const std::string originalClassName = previewObject_->GetClassName();
    previewObject_->SetClassName("Model");
    previewObject_->Draw(pointLightResource, spotLightResource);
    previewObject_->SetClassName(originalClassName);
#else
    (void)pointLightResource;
    (void)spotLightResource;
#endif
}

void Text3DGenerator::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_CUBE " 3Dテキスト生成");
    ImGui::TextDisabled("入力した文字を厚み付きのOBJモデルとして生成し、通常のObject3Dとして配置します。");
    ImGui::Separator();

    bool previewModelChanged = false;
    bool previewTransformChanged = false;

    bool autoNameDirty = false;
    if (ImGui::InputTextMultiline("文字", textBuffer_, sizeof(textBuffer_), ImVec2(-1.0f, 96.0f))) {
        autoNameDirty = true;
        previewModelChanged = true;
    }

    if (ImGui::Button(ICON_FA_SYNC_ALT " フォント再読込")) {
        RefreshFonts();
        previewModelChanged = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("フォント検索", fontFilterBuffer_, sizeof(fontFilterBuffer_));

    const int selectedFontIndex = fontNamesUtf8_.empty()
        ? 0
        : std::clamp(selectedFontIndex_, 0, static_cast<int>(fontNamesUtf8_.size()) - 1);
    const char* selectedFont = fontNamesUtf8_.empty() ? "Meiryo" : fontNamesUtf8_[selectedFontIndex].c_str();
    if (ImGui::BeginCombo("フォント", selectedFont)) {
        std::string filter = fontFilterBuffer_;
        for (int i = 0; i < static_cast<int>(fontNamesUtf8_.size()); ++i) {
            if (!ContainsFilter(fontNamesUtf8_[i], filter)) continue;
            const bool selected = (i == selectedFontIndex_);
            if (ImGui::Selectable(fontNamesUtf8_[i].c_str(), selected)) {
                selectedFontIndex_ = i;
                previewModelChanged = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    previewModelChanged |= ImGui::DragFloat("フォントサイズ", &fontSize_, 1.0f, 12.0f, 512.0f, "%.0f");
    previewModelChanged |= ImGui::DragFloat("余白", &padding_, 1.0f, 0.0f, 256.0f, "%.0f");
    previewModelChanged |= ImGui::Checkbox("太字", &bold_);

    ImGui::Separator();
    ImGui::Text("モデル化設定");
    previewModelChanged |= ImGui::DragInt("サンプル幅", &sampleStep_, 1.0f, 1, 16);
    previewModelChanged |= ImGui::DragInt("透明しきい値", &alphaThreshold_, 1.0f, 1, 255);
    previewModelChanged |= ImGui::DragFloat("モデル高さ", &modelHeight_, 0.05f, 0.1f, 20.0f, "%.2f");
    previewModelChanged |= ImGui::DragFloat("厚み", &thickness_, 0.01f, 0.01f, 5.0f, "%.2f");
    previewModelChanged |= ImGui::Checkbox("原点を中央へ", &centerOrigin_);
    const bool colorChanged = ImGui::ColorEdit4("配置時の色", modelColor_);

    ImGui::Separator();
    const bool previewToggleChanged = ImGui::Checkbox("Game Viewに3Dプレビュー", &previewEnabled_);
    ImGui::SameLine();
    if (ImGui::Checkbox("リアルタイム更新", &previewAutoUpdate_) && previewAutoUpdate_) {
        MarkPreviewDirty();
    }
    ImGui::TextDisabled("編集中だけ表示し、保存データには含めません。");
    ImGui::SameLine();
    if (ImGui::Button("3Dプレビュー更新")) {
        previewDirty_ = true;
        previewRequestPending_ = true;
        previewDelayTimer_ = 0.0f;
    }
    previewTransformChanged |= ImGui::Checkbox("カメラの前に貼り付け", &previewAttachToCamera_);
    if (previewAttachToCamera_) {
        previewTransformChanged |= ImGui::DragFloat("カメラからの距離", &previewCameraDistance_, 0.1f, 1.0f, 50.0f, "%.1f");
    } else {
        previewTransformChanged |= ImGui::DragFloat3("プレビュー位置", &previewPosition_.x, 0.1f, -1000.0f, 1000.0f);
    }    previewTransformChanged |= ImGui::DragFloat("プレビュー倍率", &previewScale_, 0.01f, 0.05f, 20.0f, "%.2f");
    if (previewToggleChanged) {
        if (previewEnabled_) {
            MarkPreviewDirty();
        } else {
            RemovePreviewObject();
        }
    }
    if ((previewTransformChanged || colorChanged) && previewObject_) {
        ApplyPreviewTransform();
    }
    if (previewEnabled_) {
        if (previewRequestPending_) {
            ImGui::TextDisabled("3Dプレビュー: 更新待ち");
        } else if (previewDirty_) {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.28f, 1.0f), "3Dプレビュー: 設定変更あり");
        } else if (hasPreviewModel_) {
            ImGui::TextDisabled("3Dプレビュー: 頂点 %d / 面 %d", lastPreview_.vertexCount, lastPreview_.faceCount);
        } else {
            ImGui::TextDisabled("3Dプレビュー: 未生成");
        }
    }

    ImGui::Separator();
    if (ImGui::Checkbox("出力名を文字から自動生成", &autoOutputName_)) {
        if (autoOutputName_) {
            UpdateOutputNameFromText();
        }
    }
    if (autoOutputName_ && autoNameDirty) {
        UpdateOutputNameFromText();
    }
    if (ImGui::InputText("出力名", outputNameBuffer_, sizeof(outputNameBuffer_))) {
        autoOutputName_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("文字から更新")) {
        autoOutputName_ = true;
        UpdateOutputNameFromText();
    }

    if (ImGui::Button(ICON_FA_FILE_EXPORT " OBJ生成")) {
        GeneratedModelInfo info;
        if (BuildModelFile(info)) {
            lastGenerated_ = info;
            hasGeneratedModel_ = true;
            SetNotice("3D文字モデルを生成しました: " + info.modelName, true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS " 生成して配置")) {
        GeneratedModelInfo info;
        if (BuildModelFile(info)) {
            lastGenerated_ = info;
            hasGeneratedModel_ = true;
            AddGeneratedModelToScene(info);
        }
    }

    if (hasGeneratedModel_) {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CUBE " 最後の生成物を配置")) {
            AddGeneratedModelToScene(lastGenerated_);
        }
        ImGui::TextDisabled("Model: %s", lastGenerated_.modelName.c_str());
        ImGui::TextDisabled("OBJ: %s", lastGenerated_.objPath.c_str());
        ImGui::TextDisabled("Cells: %d / Vertices: %d / Faces: %d",
            lastGenerated_.filledCells,
            lastGenerated_.vertexCount,
            lastGenerated_.faceCount);
    }

    if (noticeTimer_ > 0.0f && !noticeMessage_.empty()) {
        ImGui::TextColored(
            noticeSuccess_ ? ImVec4(0.36f, 1.0f, 0.58f, 1.0f) : ImVec4(1.0f, 0.36f, 0.28f, 1.0f),
            "%s",
            noticeMessage_.c_str());
    }

    ImGui::Spacing();
    ImGui::TextDisabled("生成先: %s", kOutputRoot);
    ImGui::TextDisabled("サンプル幅を小さくすると綺麗になりますが、頂点数は増えます。");
    ImGui::TextDisabled("フォント参照先: Resources 内の .ttf / .otf / .ttc");

    if (previewModelChanged) {
        MarkPreviewDirty();
    }
#endif
}

bool Text3DGenerator::EnsureFactories() {
    if (!wicFactory_) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == S_OK || hr == S_FALSE) {
            ownsCom_ = true;
        } else if (hr != RPC_E_CHANGED_MODE) {
            DebugConsole::GetInstance()->AddLog("Text 3D: COM initialize failed.");
            return false;
        }

        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            DebugConsole::GetInstance()->AddLog("Text 3D: WIC factory failed.");
            return false;
        }
    }

    if (!d2dFactory_) {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
        if (FAILED(hr)) {
            DebugConsole::GetInstance()->AddLog("Text 3D: D2D factory failed.");
            return false;
        }
    }

    if (!dwriteFactory_) {
        HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            DebugConsole::GetInstance()->AddLog("Text 3D: DirectWrite factory failed.");
            return false;
        }
    }

    return true;
}

void Text3DGenerator::RefreshFonts() {
    if (!EnsureFactories()) return;

    std::string previousFont = fontNamesUtf8_.empty()
        ? std::string()
        : fontNamesUtf8_[std::clamp(selectedFontIndex_, 0, static_cast<int>(fontNamesUtf8_.size()) - 1)];

    fontNamesWide_.clear();
    fontNamesUtf8_.clear();
    resourceFontCollection_.Reset();

    std::vector<std::wstring> resourceFontPaths = CollectResourceFontPaths();
    if (!resourceFontPaths.empty()) {
        if (!resourceFontLoader_) {
            resourceFontLoader_.Attach(new ResourceFontCollectionLoader());
            HRESULT registerHr = dwriteFactory_->RegisterFontCollectionLoader(resourceFontLoader_.Get());
            if (FAILED(registerHr)) {
                DebugConsole::GetInstance()->AddLog("Text 3D: resource font loader registration failed.");
                resourceFontLoader_.Reset();
            }
        }

        if (resourceFontLoader_) {
            resourceFontCollectionKey_ = MakeFontCollectionKey(resourceFontPaths);
            HRESULT hr = dwriteFactory_->CreateCustomFontCollection(
                resourceFontLoader_.Get(),
                resourceFontCollectionKey_.data(),
                static_cast<UINT32>(resourceFontCollectionKey_.size() * sizeof(wchar_t)),
                resourceFontCollection_.GetAddressOf());
            if (FAILED(hr)) {
                resourceFontCollection_.Reset();
            }
        }
    }

    IDWriteFontCollection* collection = resourceFontCollection_.Get();
    if (collection) {
        UINT32 count = collection->GetFontFamilyCount();
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
                return;
            }
        }
    }

    for (int i = 0; i < static_cast<int>(fontNamesUtf8_.size()); ++i) {
        std::string lower = ToLowerAscii(fontNamesUtf8_[i]);
        if (lower.find("mplus") != std::string::npos || lower.find("meiryo") != std::string::npos) {
            selectedFontIndex_ = i;
            break;
        }
    }
}

bool Text3DGenerator::RenderTextMask(TextMask& outMask) {
    outMask = {};
    if (!EnsureFactories()) return false;

    std::wstring text = Utf8ToWide(textBuffer_);
    if (text.empty()) {
        SetNotice("文字が空です。", false);
        return false;
    }

    std::wstring fontName = fontNamesWide_.empty()
        ? std::wstring(L"Meiryo")
        : fontNamesWide_[std::clamp(selectedFontIndex_, 0, static_cast<int>(fontNamesWide_.size()) - 1)];

    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
    HRESULT hr = dwriteFactory_->CreateTextFormat(
        fontName.c_str(),
        resourceFontCollection_.Get(),
        bold_ ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        std::max(4.0f, fontSize_),
        L"ja-jp",
        textFormat.GetAddressOf());
    if (FAILED(hr)) return false;

    textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    const float layoutWidth = 4096.0f;
    const float layoutHeight = 2048.0f;

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

    const float pad = std::max(0.0f, padding_);
    const int width = ClampCanvasSize(metrics.left + metrics.widthIncludingTrailingWhitespace + pad * 2.0f);
    const int height = ClampCanvasSize(metrics.top + metrics.height + pad * 2.0f);

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
    renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), textBrush.GetAddressOf());

    const float originX = pad - metrics.left;
    const float originY = pad - metrics.top;

    renderTarget->BeginDraw();
    renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    renderTarget->DrawTextLayout(D2D1::Point2F(originX, originY), textLayout.Get(), textBrush.Get());
    hr = renderTarget->EndDraw();
    if (FAILED(hr)) return false;

    WICRect rect{ 0, 0, width, height };
    Microsoft::WRL::ComPtr<IWICBitmapLock> lock;
    hr = bitmap->Lock(&rect, WICBitmapLockRead, lock.GetAddressOf());
    if (FAILED(hr)) return false;

    UINT stride = 0;
    hr = lock->GetStride(&stride);
    if (FAILED(hr)) return false;

    UINT bufferSize = 0;
    BYTE* data = nullptr;
    hr = lock->GetDataPointer(&bufferSize, &data);
    if (FAILED(hr) || !data) return false;

    outMask.width = width;
    outMask.height = height;
    outMask.alpha.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    for (int y = 0; y < height; ++y) {
        const BYTE* row = data + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            outMask.alpha[static_cast<size_t>(y) * width + x] = row[static_cast<size_t>(x) * 4 + 3];
        }
    }

    return true;
}

bool Text3DGenerator::BuildModelFile(GeneratedModelInfo& outInfo) {
    TextMask mask;
    if (!RenderTextMask(mask)) {
        SetNotice("文字マスクの生成に失敗しました。", false);
        return false;
    }

    const std::string stem = SanitizeModelStem(outputNameBuffer_, textBuffer_);
    const fs::path outputDir = fs::path(kOutputRoot) / stem;
    const fs::path objPath = outputDir / (stem + ".obj");
    std::error_code ec;
    fs::create_directories(outputDir, ec);
    if (ec) {
        SetNotice("出力フォルダを作れませんでした。", false);
        return false;
    }

    GeneratedModelInfo info;
    info.modelName = "GeneratedText/" + stem;
    info.objPath = objPath.generic_string();
    info.reportPath = (outputDir / (stem + "_text3d_report.json")).generic_string();
    info.width = mask.width;
    info.height = mask.height;

    if (!WriteObjFromMask(mask, objPath, info)) {
        SetNotice("OBJ生成に失敗しました。", false);
        return false;
    }

    if (info.vertexCount > kGeneratedMaxVertices) {
        SetNotice("3Dテキストモデルが大きすぎるため生成を中止しました。サンプル幅を大きくしてください。", false);
        DebugConsole::GetInstance()->AddLog(
            "Text 3D generation skipped: too many vertices (" + std::to_string(info.vertexCount) + ").");
        return false;
    }

    if (!WriteReport(info)) {
        DebugConsole::GetInstance()->AddLog("Text 3D: report write failed.");
    }

    outInfo = info;
    DebugConsole::GetInstance()->AddLog("Text 3D generated: " + info.modelName);
    return true;
}

bool Text3DGenerator::WriteObjFromMask(const TextMask& mask, const fs::path& objPath, GeneratedModelInfo& outInfo) {
    if (mask.width <= 0 || mask.height <= 0 || mask.alpha.empty()) return false;

    const int step = std::clamp(sampleStep_, 1, 16);
    const int threshold = std::clamp(alphaThreshold_, 1, 255);
    const int cols = (mask.width + step - 1) / step;
    const int rows = (mask.height + step - 1) / step;
    std::vector<unsigned char> filled(static_cast<size_t>(cols) * rows, 0);

    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = -1;
    int maxY = -1;

    for (int gy = 0; gy < rows; ++gy) {
        for (int gx = 0; gx < cols; ++gx) {
            unsigned char maxAlpha = 0;
            const int beginX = gx * step;
            const int beginY = gy * step;
            const int endX = std::min(beginX + step, mask.width);
            const int endY = std::min(beginY + step, mask.height);
            for (int y = beginY; y < endY; ++y) {
                for (int x = beginX; x < endX; ++x) {
                    maxAlpha = std::max(maxAlpha, mask.alpha[static_cast<size_t>(y) * mask.width + x]);
                }
            }

            if (maxAlpha >= threshold) {
                filled[static_cast<size_t>(gy) * cols + gx] = 1;
                minX = std::min(minX, gx);
                minY = std::min(minY, gy);
                maxX = std::max(maxX, gx);
                maxY = std::max(maxY, gy);
                ++outInfo.filledCells;
            }
        }
    }

    if (outInfo.filledCells == 0) {
        SetNotice("モデル化できる文字ピクセルがありません。", false);
        return false;
    }

    const int usedCols = maxX - minX + 1;
    const int usedRows = maxY - minY + 1;
    const float cell = std::max(0.001f, modelHeight_) / static_cast<float>(std::max(1, usedRows));
    const float depth = std::max(0.001f, thickness_);
    const float halfDepth = depth * 0.5f;
    const float totalWidth = static_cast<float>(usedCols) * cell;
    const float totalHeight = static_cast<float>(usedRows) * cell;
    const float xOrigin = centerOrigin_ ? -totalWidth * 0.5f : 0.0f;
    const float yOrigin = centerOrigin_ ? totalHeight * 0.5f : totalHeight;

    auto isFilled = [&](int gx, int gy) {
        if (gx < minX || gx > maxX || gy < minY || gy > maxY) return false;
        return filled[static_cast<size_t>(gy) * cols + gx] != 0;
    };

    std::vector<ObjVertex> vertices;
    std::vector<ObjUv> uvs;
    std::vector<ObjVertex> normals;
    std::vector<ObjFace> faces;

    const auto makeUv = [&](float x, float y) {
        return ObjUv{
            totalWidth > 0.0f ? (x - xOrigin) / totalWidth : 0.0f,
            totalHeight > 0.0f ? (yOrigin - y) / totalHeight : 0.0f
        };
    };

    for (int gy = minY; gy <= maxY; ++gy) {
        for (int gx = minX; gx <= maxX; ++gx) {
            if (!isFilled(gx, gy)) continue;

            const float localX0 = static_cast<float>(gx - minX) * cell;
            const float localX1 = localX0 + cell;
            const float localYTop = yOrigin - static_cast<float>(gy - minY) * cell;
            const float localYBottom = localYTop - cell;
            const float x0 = xOrigin + localX0;
            const float x1 = xOrigin + localX1;
            const float y0 = localYBottom;
            const float y1 = localYTop;

            const ObjUv uv00 = makeUv(x0, y0);
            const ObjUv uv10 = makeUv(x1, y0);
            const ObjUv uv11 = makeUv(x1, y1);
            const ObjUv uv01 = makeUv(x0, y1);

            AddQuad(
                { ObjVertex{x0, y0, halfDepth}, ObjVertex{x1, y0, halfDepth}, ObjVertex{x1, y1, halfDepth}, ObjVertex{x0, y1, halfDepth} },
                { uv00, uv10, uv11, uv01 },
                ObjVertex{ 0.0f, 0.0f, 1.0f },
                vertices, uvs, normals, faces);

            AddQuad(
                { ObjVertex{x1, y0, -halfDepth}, ObjVertex{x0, y0, -halfDepth}, ObjVertex{x0, y1, -halfDepth}, ObjVertex{x1, y1, -halfDepth} },
                { uv10, uv00, uv01, uv11 },
                ObjVertex{ 0.0f, 0.0f, -1.0f },
                vertices, uvs, normals, faces);

            if (!isFilled(gx - 1, gy)) {
                AddQuad(
                    { ObjVertex{x0, y0, -halfDepth}, ObjVertex{x0, y0, halfDepth}, ObjVertex{x0, y1, halfDepth}, ObjVertex{x0, y1, -halfDepth} },
                    { uv00, uv10, uv11, uv01 },
                    ObjVertex{ -1.0f, 0.0f, 0.0f },
                    vertices, uvs, normals, faces);
            }
            if (!isFilled(gx + 1, gy)) {
                AddQuad(
                    { ObjVertex{x1, y0, halfDepth}, ObjVertex{x1, y0, -halfDepth}, ObjVertex{x1, y1, -halfDepth}, ObjVertex{x1, y1, halfDepth} },
                    { uv00, uv10, uv11, uv01 },
                    ObjVertex{ 1.0f, 0.0f, 0.0f },
                    vertices, uvs, normals, faces);
            }
            if (!isFilled(gx, gy + 1)) {
                AddQuad(
                    { ObjVertex{x0, y0, halfDepth}, ObjVertex{x1, y0, halfDepth}, ObjVertex{x1, y0, -halfDepth}, ObjVertex{x0, y0, -halfDepth} },
                    { uv00, uv10, uv11, uv01 },
                    ObjVertex{ 0.0f, -1.0f, 0.0f },
                    vertices, uvs, normals, faces);
            }
            if (!isFilled(gx, gy - 1)) {
                AddQuad(
                    { ObjVertex{x0, y1, -halfDepth}, ObjVertex{x1, y1, -halfDepth}, ObjVertex{x1, y1, halfDepth}, ObjVertex{x0, y1, halfDepth} },
                    { uv00, uv10, uv11, uv01 },
                    ObjVertex{ 0.0f, 1.0f, 0.0f },
                    vertices, uvs, normals, faces);
            }
        }
    }

    std::ofstream obj(objPath);
    if (!obj) return false;

    obj << "# Generated by GE3 Text3DGenerator\n";
    obj << "o Text3D\n";
    obj << std::fixed << std::setprecision(6);
    for (const ObjVertex& v : vertices) {
        obj << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
    }
    for (const ObjUv& uv : uvs) {
        obj << "vt " << uv.u << ' ' << uv.v << '\n';
    }
    for (const ObjVertex& n : normals) {
        obj << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
    }
    obj << "usemtl Text3DMaterial\n";
    for (const ObjFace& face : faces) {
        obj << "f";
        for (int i = 0; i < 4; ++i) {
            obj << ' ' << face.v[i] << '/' << face.vt[i] << '/' << face.vn;
        }
        obj << '\n';
    }

    outInfo.vertexCount = static_cast<int>(vertices.size());
    outInfo.faceCount = static_cast<int>(faces.size());
    return true;
}

bool Text3DGenerator::WriteReport(const GeneratedModelInfo& info) {
    nlohmann::json report;
    report["tool"] = "Text3DGenerator";
    report["text"] = textBuffer_;
    report["modelName"] = info.modelName;
    report["objPath"] = info.objPath;
    report["font"] = fontNamesUtf8_.empty() ? "Meiryo" : fontNamesUtf8_[std::clamp(selectedFontIndex_, 0, static_cast<int>(fontNamesUtf8_.size()) - 1)];
    report["fontSize"] = fontSize_;
    report["padding"] = padding_;
    report["sampleStep"] = sampleStep_;
    report["alphaThreshold"] = alphaThreshold_;
    report["modelHeight"] = modelHeight_;
    report["thickness"] = thickness_;
    report["maskWidth"] = info.width;
    report["maskHeight"] = info.height;
    report["filledCells"] = info.filledCells;
    report["vertexCount"] = info.vertexCount;
    report["faceCount"] = info.faceCount;

    std::ofstream file(info.reportPath);
    if (!file) return false;
    file << report.dump(2);
    return true;
}

void Text3DGenerator::AddGeneratedModelToScene(const GeneratedModelInfo& info) {
    if (!editor_ || !sceneManager_ || !sceneManager_->GetCurrentScene()) {
        SetNotice("配置できるシーンがありません。", false);
        return;
    }

    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene->GetObject3dCommon()) {
        SetNotice("Object3dCommonが見つかりません。", false);
        return;
    }

    ModelManager::GetInstance()->LoadModel(info.modelName);

    auto object = std::make_unique<Object3d>();
    object->Initialize(scene->GetObject3dCommon());
    object->SetModel(info.modelName);
    object->SetClassName("Model");
    object->SetSaveCategory("Object");
    object->SetName(MakeUniqueObjectName(scene, "Text3D_" + fs::path(info.modelName).filename().generic_string()));
    object->SetColor({ modelColor_[0], modelColor_[1], modelColor_[2], modelColor_[3] });
    object->SetCollisionAttribute(0);
    object->SetCollisionMask(0);

    Vector3 position = { 0.0f, 2.0f, 0.0f };
    if (Object3d* selected = editor_->GetSelectedObject3D()) {
        position = selected->GetTranslate();
        position.x += 2.0f;
        position.y += 1.5f;
    }
    object->SetTranslate(position);
    object->UpdateLocalMatrix();
    object->UpdateWorldMatrix();

    editor_->AddEditorObject(std::move(object), "Create 3D Text");
    SetNotice("3D文字モデルを配置しました: " + info.modelName, true);
}

bool Text3DGenerator::BuildPreviewModelFile(GeneratedModelInfo& outInfo) {
    TextMask mask;
    if (!RenderTextMask(mask)) {
        return false;
    }

    const fs::path outputDir = fs::path(kOutputRoot) / kPreviewStem;
    const fs::path objPath = outputDir / (std::string(kPreviewStem) + ".obj");
    std::error_code ec;
    fs::create_directories(outputDir, ec);
    if (ec) {
        return false;
    }

    GeneratedModelInfo info;
    info.modelName = kPreviewModelName;
    info.objPath = objPath.generic_string();
    info.width = mask.width;
    info.height = mask.height;

    if (!WriteObjFromMask(mask, objPath, info)) {
        return false;
    }
    if (info.vertexCount > kPreviewMaxVertices) {
        SetNotice("3Dテキストプレビューが重すぎるため更新を止めました。サンプル幅を大きくしてください。", false);
        DebugConsole::GetInstance()->AddLog(
            "Text 3D preview skipped: too many vertices (" + std::to_string(info.vertexCount) + ").");
        return false;
    }

    outInfo = info;
    return true;
}

void Text3DGenerator::UpdatePreviewModel(float deltaTime) {
#ifdef USE_IMGUI
    if (!previewEnabled_) {
        RemovePreviewObject();
        return;
    }

    if (EditorManager::GetInstance()->GetSelectedObject() != this) {
        RemovePreviewObject();
        return;
    }

    const bool needsRebuild = previewDirty_ || !previewObject_;
    if (!needsRebuild) {
        return;
    }

    // リアルタイム更新OFF時は、手動更新ボタンで予約された時だけ再生成する。
    if (!previewAutoUpdate_ && !previewRequestPending_) {
        return;
    }

    if (previewRequestPending_) {
        previewDelayTimer_ -= std::max(0.0f, deltaTime);
        if (previewDelayTimer_ > 0.0f) {
            return;
        }
    }

    GeneratedModelInfo info;
    if (!BuildPreviewModelFile(info)) {
        previewDirty_ = false;
        previewRequestPending_ = false;
        return;
    }

    if (!ModelManager::GetInstance()->ReloadModel(info.modelName)) {
        SetNotice("3Dテキストプレビューの読み込みに失敗しました。", false);
        previewDirty_ = false;
        previewRequestPending_ = false;
        return;
    }
    EnsurePreviewObject(info);
    lastPreview_ = info;
    hasPreviewModel_ = true;
    previewDirty_ = false;
    previewRequestPending_ = false;
#else
    (void)deltaTime;
#endif
}

void Text3DGenerator::EnsurePreviewObject(const GeneratedModelInfo& info) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        previewObject_ = nullptr;
        return;
    }

    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene->GetObject3dCommon()) {
        previewObject_ = nullptr;
        return;
    }

    Object3d* existing = FindPreviewObject();
    if (existing) {
        previewObject_ = existing;
    } else {
        auto object = std::make_unique<Object3d>();
        object->Initialize(scene->GetObject3dCommon());
        object->SetName(kPreviewObjectName);
        object->SetClassName("EditorOnly");
        object->SetSaveCategory("Object");
        object->SetIsLocked(true);
        object->SetCollisionAttribute(0);
        object->SetCollisionMask(0);
        previewObject_ = object.get();
        scene->AddObject(std::move(object));
    }

    if (!previewObject_) {
        return;
    }

    previewObject_->SetModel(info.modelName);
    ApplyPreviewTransform();
    previewObject_->SetIsVisible(true);
}

Vector3 Text3DGenerator::ResolvePreviewPosition() const {
    if (!previewAttachToCamera_) {
        return previewPosition_;
    }

    const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) {
        return previewPosition_;
    }

    const Vector3 eye = camera->GetEye();
    const Vector3 target = camera->GetTargetPoint();
    Vector3 forward{
        target.x - eye.x,
        target.y - eye.y,
        target.z - eye.z
    };

    const float length = std::sqrt(
        forward.x * forward.x +
        forward.y * forward.y +
        forward.z * forward.z);

    if (length > 0.0001f) {
        forward.x /= length;
        forward.y /= length;
        forward.z /= length;
    } else {
        forward = { 0.0f, 0.0f, 1.0f };
    }

    return {
        eye.x + forward.x * previewCameraDistance_,
        eye.y + forward.y * previewCameraDistance_,
        eye.z + forward.z * previewCameraDistance_
    };
}

void Text3DGenerator::ApplyPreviewTransform() {
    if (!previewObject_) {
        return;
    }

    previewObject_->SetColor({ modelColor_[0], modelColor_[1], modelColor_[2], modelColor_[3] });
    previewObject_->SetTranslate(ResolvePreviewPosition());
    previewObject_->SetScale({ previewScale_, previewScale_, previewScale_ });
    previewObject_->UpdateLocalMatrix();
    previewObject_->UpdateWorldMatrix();
}
void Text3DGenerator::RemovePreviewObject() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        previewObject_ = nullptr;
        hasPreviewModel_ = false;
        return;
    }

    if (Object3d* object = FindPreviewObject()) {
        sceneManager_->GetCurrentScene()->RequestRemoveObject(object);
    }
    previewObject_ = nullptr;
    hasPreviewModel_ = false;
}

void Text3DGenerator::MarkPreviewDirty() {
    previewDirty_ = true;
    if (previewAutoUpdate_) {
        previewRequestPending_ = true;
        previewDelayTimer_ = kPreviewRebuildDelay;
    } else {
        previewRequestPending_ = false;
        previewDelayTimer_ = 0.0f;
    }
}

Object3d* Text3DGenerator::FindPreviewObject() const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        return nullptr;
    }

    for (auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (object && object->GetName() == kPreviewObjectName) {
            return object.get();
        }
    }
    return nullptr;
}

void Text3DGenerator::UpdateOutputNameFromText() {
    CopyToBuffer(outputNameBuffer_, sizeof(outputNameBuffer_), MakeSafeStemFromText(textBuffer_));
}

void Text3DGenerator::SetNotice(const std::string& message, bool success) {
    noticeMessage_ = message;
    noticeSuccess_ = success;
    noticeTimer_ = 3.0f;
    DebugConsole::GetInstance()->AddLog(message);
}
