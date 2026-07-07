#define NOMINMAX

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {

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
    ComPtr<IDWriteFactory> factory_;
    std::vector<std::wstring> fontPaths_;
    size_t nextIndex_ = 0;
    ComPtr<IDWriteFontFile> currentFile_;
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

struct RenderConfig {
    std::wstring text = L" ";
    std::wstring fontPath;
    std::wstring fontFamilyName;
    float fontSize = 72.0f;
    float padding = 16.0f;
    bool autoCanvas = true;
    int canvasWidth = 512;
    int canvasHeight = 256;
    bool bold = true;
    D2D1_COLOR_F textColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    bool outlineEnabled = false;
    float outlineWidth = 0.0f;
    D2D1_COLOR_F outlineColor = D2D1::ColorF(0.08f, 0.08f, 0.08f, 1.0f);
    bool shadowEnabled = false;
    float shadowOffsetX = 0.0f;
    float shadowOffsetY = 0.0f;
    D2D1_COLOR_F shadowColor = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f);
    std::wstring reportPath;
};

struct RenderResult {
    int width = 1;
    int height = 1;
    std::wstring usedFontFamily;
    bool fallbackUsed = false;
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

std::wstring MakeFontCollectionKey(const std::wstring& path) {
    std::wstring key = path;
    key.push_back(L'\0');
    key.push_back(L'\0');
    return key;
}

std::wstring GetLocalizedString(IDWriteLocalizedStrings* strings, const wchar_t* fallback) {
    if (!strings) return fallback ? fallback : L"";
    UINT32 index = 0;
    BOOL exists = FALSE;
    HRESULT hr = strings->FindLocaleName(L"ja-jp", &index, &exists);
    if (FAILED(hr) || !exists) {
        hr = strings->FindLocaleName(L"en-us", &index, &exists);
    }
    if (FAILED(hr) || !exists) {
        index = 0;
    }
    UINT32 length = 0;
    if (FAILED(strings->GetStringLength(index, &length))) {
        return fallback ? fallback : L"";
    }
    std::wstring value(length + 1, L'\0');
    if (FAILED(strings->GetString(index, value.data(), length + 1))) {
        return fallback ? fallback : L"";
    }
    value.resize(length);
    return value;
}

float ReadFloat(const nlohmann::json& json, const char* name, float fallback) {
    if (!json.contains(name) || !json[name].is_number()) return fallback;
    return json[name].get<float>();
}

int ReadInt(const nlohmann::json& json, const char* name, int fallback) {
    if (!json.contains(name) || !json[name].is_number_integer()) return fallback;
    return json[name].get<int>();
}

bool ReadBool(const nlohmann::json& json, const char* name, bool fallback) {
    if (!json.contains(name) || !json[name].is_boolean()) return fallback;
    return json[name].get<bool>();
}

std::wstring ReadWideString(const nlohmann::json& json, const char* name, const wchar_t* fallback = L"") {
    if (!json.contains(name) || !json[name].is_string()) return fallback;
    return Utf8ToWide(json[name].get<std::string>());
}

D2D1_COLOR_F ReadColor(const nlohmann::json& json, const char* name, D2D1_COLOR_F fallback) {
    if (!json.contains(name) || !json[name].is_array() || json[name].size() < 4) {
        return fallback;
    }
    return D2D1::ColorF(
        std::clamp(json[name][0].get<float>(), 0.0f, 1.0f),
        std::clamp(json[name][1].get<float>(), 0.0f, 1.0f),
        std::clamp(json[name][2].get<float>(), 0.0f, 1.0f),
        std::clamp(json[name][3].get<float>(), 0.0f, 1.0f));
}

float ReadArrayFloat(const nlohmann::json& json, const char* name, size_t index, float fallback) {
    if (!json.contains(name) || !json[name].is_array() || json[name].size() <= index) {
        return fallback;
    }
    return json[name][index].get<float>();
}

RenderConfig LoadConfig(const fs::path& configPath) {
    std::ifstream file(configPath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("config open failed");
    }

    nlohmann::json json;
    file >> json;

    RenderConfig cfg;
    cfg.text = ReadWideString(json, "text", L" ");
    if (cfg.text.empty()) cfg.text = L" ";
    cfg.fontPath = ReadWideString(json, "fontPath");
    cfg.fontFamilyName = ReadWideString(json, "fontFamilyName");
    cfg.fontSize = std::clamp(ReadFloat(json, "fontSize", 72.0f), 4.0f, 512.0f);
    cfg.padding = std::clamp(ReadFloat(json, "padding", 16.0f), 0.0f, 256.0f);
    cfg.autoCanvas = ReadBool(json, "autoCanvas", true);
    cfg.canvasWidth = std::clamp(ReadInt(json, "canvasWidth", 512), 1, 8192);
    cfg.canvasHeight = std::clamp(ReadInt(json, "canvasHeight", 256), 1, 8192);
    cfg.bold = ReadBool(json, "bold", true);
    cfg.textColor = ReadColor(json, "textColor", cfg.textColor);

    if (json.contains("outline") && json["outline"].is_object()) {
        const nlohmann::json& outline = json["outline"];
        cfg.outlineEnabled = ReadBool(outline, "enabled", false);
        cfg.outlineWidth = std::clamp(ReadFloat(outline, "width", 0.0f), 0.0f, 64.0f);
        cfg.outlineColor = ReadColor(outline, "color", cfg.outlineColor);
    }

    if (json.contains("shadow") && json["shadow"].is_object()) {
        const nlohmann::json& shadow = json["shadow"];
        cfg.shadowEnabled = ReadBool(shadow, "enabled", false);
        cfg.shadowOffsetX = ReadArrayFloat(shadow, "offset", 0, 0.0f);
        cfg.shadowOffsetY = ReadArrayFloat(shadow, "offset", 1, 0.0f);
        cfg.shadowColor = ReadColor(shadow, "color", cfg.shadowColor);
    }

    cfg.reportPath = ReadWideString(json, "reportPath");
    return cfg;
}

ComPtr<IDWriteFontCollection> CreateResourceFontCollection(
    IDWriteFactory* factory,
    ResourceFontCollectionLoader* loader,
    const std::wstring& fontPath,
    std::wstring& key) {
    if (!factory || !loader || fontPath.empty() || !fs::exists(fontPath)) {
        return nullptr;
    }

    key = MakeFontCollectionKey(fs::absolute(fontPath).wstring());
    ComPtr<IDWriteFontCollection> collection;
    HRESULT hr = factory->CreateCustomFontCollection(loader, key.data(), static_cast<UINT32>(key.size() * sizeof(wchar_t)), collection.GetAddressOf());
    if (FAILED(hr)) {
        return nullptr;
    }
    return collection;
}

bool CollectionHasFamily(IDWriteFontCollection* collection, const std::wstring& familyName) {
    if (!collection || familyName.empty()) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(collection->FindFamilyName(familyName.c_str(), &index, &exists)) && exists;
}

std::wstring GetFirstFamilyName(IDWriteFontCollection* collection) {
    if (!collection || collection->GetFontFamilyCount() == 0) return L"";
    ComPtr<IDWriteFontFamily> family;
    if (FAILED(collection->GetFontFamily(0, family.GetAddressOf()))) return L"";
    ComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(family->GetFamilyNames(names.GetAddressOf()))) return L"";
    return GetLocalizedString(names.Get(), L"");
}

ComPtr<IDWriteTextFormat> CreateTextFormatForConfig(
    IDWriteFactory* factory,
    IDWriteFontCollection* customCollection,
    const RenderConfig& cfg,
    RenderResult& result) {
    const DWRITE_FONT_WEIGHT weight = cfg.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_REGULAR;

    auto create = [&](const std::wstring& family, IDWriteFontCollection* collection) {
        ComPtr<IDWriteTextFormat> format;
        HRESULT hr = factory->CreateTextFormat(
            family.c_str(),
            collection,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            cfg.fontSize,
            L"ja-jp",
            format.GetAddressOf());
        if (FAILED(hr)) return ComPtr<IDWriteTextFormat>{};
        result.usedFontFamily = family;
        return format;
    };

    if (customCollection) {
        if (!cfg.fontFamilyName.empty() && CollectionHasFamily(customCollection, cfg.fontFamilyName)) {
            ComPtr<IDWriteTextFormat> format = create(cfg.fontFamilyName, customCollection);
            if (format) return format;
        }

        const std::wstring firstFamily = GetFirstFamilyName(customCollection);
        if (!firstFamily.empty()) {
            result.fallbackUsed = true;
            ComPtr<IDWriteTextFormat> format = create(firstFamily, customCollection);
            if (format) return format;
        }
    }

    const std::wstring systemCandidates[] = {
        cfg.fontFamilyName,
        L"Meiryo UI",
        L"Yu Gothic UI",
        L"Yu Gothic",
        L"Arial"
    };
    for (const std::wstring& family : systemCandidates) {
        if (family.empty()) continue;
        result.fallbackUsed = true;
        ComPtr<IDWriteTextFormat> format = create(family, nullptr);
        if (format) return format;
    }

    throw std::runtime_error("font format create failed");
}

ComPtr<ID2D1SolidColorBrush> CreateBrush(ID2D1RenderTarget* renderTarget, D2D1_COLOR_F color) {
    ComPtr<ID2D1SolidColorBrush> brush;
    HRESULT hr = renderTarget->CreateSolidColorBrush(color, brush.GetAddressOf());
    if (FAILED(hr)) {
        throw std::runtime_error("brush create failed");
    }
    return brush;
}

void DrawTextLayoutWithOffset(
    ID2D1RenderTarget* renderTarget,
    IDWriteTextLayout* layout,
    ID2D1Brush* brush,
    D2D1_POINT_2F origin,
    float offsetX,
    float offsetY) {
    renderTarget->DrawTextLayout(D2D1::Point2F(origin.x + offsetX, origin.y + offsetY), layout, brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

void SaveBitmapAsPng(IWICImagingFactory* wicFactory, IWICBitmap* bitmap, const fs::path& outputPath) {
    fs::create_directories(outputPath.parent_path());

    ComPtr<IWICStream> stream;
    HRESULT hr = wicFactory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) throw std::runtime_error("WIC stream create failed");

    hr = stream->InitializeFromFilename(outputPath.wstring().c_str(), GENERIC_WRITE);
    if (FAILED(hr)) throw std::runtime_error("output stream open failed");

    ComPtr<IWICBitmapEncoder> encoder;
    hr = wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (FAILED(hr)) throw std::runtime_error("PNG encoder create failed");

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) throw std::runtime_error("PNG encoder initialize failed");

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> propertyBag;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), propertyBag.GetAddressOf());
    if (FAILED(hr)) throw std::runtime_error("PNG frame create failed");

    hr = frame->Initialize(propertyBag.Get());
    if (FAILED(hr)) throw std::runtime_error("PNG frame initialize failed");

    UINT width = 0;
    UINT height = 0;
    bitmap->GetSize(&width, &height);
    frame->SetSize(width, height);

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppPBGRA;
    frame->SetPixelFormat(&pixelFormat);
    hr = frame->WriteSource(bitmap, nullptr);
    if (FAILED(hr)) throw std::runtime_error("PNG write failed");

    frame->Commit();
    encoder->Commit();
}

RenderResult RenderToPng(const RenderConfig& cfg, const fs::path& outputPath) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool ownsCom = (hr == S_OK || hr == S_FALSE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        throw std::runtime_error("COM initialize failed");
    }

    RenderResult result;
    ComPtr<IWICImagingFactory> wicFactory;
    ComPtr<ID2D1Factory> d2dFactory;
    ComPtr<IDWriteFactory> dwriteFactory;

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory.GetAddressOf()));
    if (FAILED(hr)) throw std::runtime_error("WIC factory create failed");

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf());
    if (FAILED(hr)) throw std::runtime_error("D2D factory create failed");

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) throw std::runtime_error("DirectWrite factory create failed");

    ComPtr<ResourceFontCollectionLoader> loader;
    loader.Attach(new ResourceFontCollectionLoader());
    dwriteFactory->RegisterFontCollectionLoader(loader.Get());

    std::wstring collectionKey;
    ComPtr<IDWriteFontCollection> customCollection = CreateResourceFontCollection(dwriteFactory.Get(), loader.Get(), cfg.fontPath, collectionKey);
    ComPtr<IDWriteTextFormat> textFormat = CreateTextFormatForConfig(dwriteFactory.Get(), customCollection.Get(), cfg, result);
    textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    const float outlinePad = (cfg.outlineEnabled && cfg.outlineWidth > 0.0f) ? cfg.outlineWidth : 0.0f;
    const float edgePadding = std::ceil(cfg.padding + outlinePad + 2.0f);
    const float shadowPadX = cfg.shadowEnabled ? std::abs(cfg.shadowOffsetX) : 0.0f;
    const float shadowPadY = cfg.shadowEnabled ? std::abs(cfg.shadowOffsetY) : 0.0f;
    const float negativeShadowX = cfg.shadowEnabled ? std::max(0.0f, -cfg.shadowOffsetX) : 0.0f;
    const float negativeShadowY = cfg.shadowEnabled ? std::max(0.0f, -cfg.shadowOffsetY) : 0.0f;

    const float layoutWidth = cfg.autoCanvas ? 4096.0f : std::max(1.0f, static_cast<float>(cfg.canvasWidth) - edgePadding * 2.0f - shadowPadX);
    const float layoutHeight = cfg.autoCanvas ? 4096.0f : std::max(1.0f, static_cast<float>(cfg.canvasHeight) - edgePadding * 2.0f - shadowPadY);

    ComPtr<IDWriteTextLayout> textLayout;
    hr = dwriteFactory->CreateTextLayout(
        cfg.text.c_str(),
        static_cast<UINT32>(cfg.text.size()),
        textFormat.Get(),
        layoutWidth,
        layoutHeight,
        textLayout.GetAddressOf());
    if (FAILED(hr)) throw std::runtime_error("text layout create failed");

    DWRITE_TEXT_METRICS metrics{};
    textLayout->GetMetrics(&metrics);
    const float measuredWidth = std::max(metrics.widthIncludingTrailingWhitespace, metrics.width);
    const float measuredHeight = std::max(metrics.height, cfg.fontSize);

    int width = cfg.canvasWidth;
    int height = cfg.canvasHeight;
    if (cfg.autoCanvas) {
        width = static_cast<int>(std::ceil(measuredWidth + edgePadding * 2.0f + shadowPadX));
        height = static_cast<int>(std::ceil(measuredHeight + edgePadding * 2.0f + shadowPadY));
    }
    width = std::clamp(width, 1, 8192);
    height = std::clamp(height, 1, 8192);
    result.width = width;
    result.height = height;

    ComPtr<IWICBitmap> bitmap;
    hr = wicFactory->CreateBitmap(
        static_cast<UINT>(width),
        static_cast<UINT>(height),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnLoad,
        bitmap.GetAddressOf());
    if (FAILED(hr)) throw std::runtime_error("bitmap create failed");

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);

    ComPtr<ID2D1RenderTarget> renderTarget;
    hr = d2dFactory->CreateWicBitmapRenderTarget(bitmap.Get(), props, renderTarget.GetAddressOf());
    if (FAILED(hr)) throw std::runtime_error("render target create failed");

    ComPtr<ID2D1SolidColorBrush> textBrush = CreateBrush(renderTarget.Get(), cfg.textColor);
    ComPtr<ID2D1SolidColorBrush> outlineBrush = CreateBrush(renderTarget.Get(), cfg.outlineColor);
    ComPtr<ID2D1SolidColorBrush> shadowBrush = CreateBrush(renderTarget.Get(), cfg.shadowColor);

    const D2D1_POINT_2F origin = D2D1::Point2F(
        edgePadding - metrics.left + negativeShadowX,
        edgePadding - metrics.top + negativeShadowY);

    renderTarget->BeginDraw();
    renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    if (cfg.shadowEnabled && cfg.shadowColor.a > 0.0f) {
        DrawTextLayoutWithOffset(renderTarget.Get(), textLayout.Get(), shadowBrush.Get(), origin, cfg.shadowOffsetX, cfg.shadowOffsetY);
    }

    if (cfg.outlineEnabled && cfg.outlineWidth > 0.0f && cfg.outlineColor.a > 0.0f) {
        const int radius = std::clamp(static_cast<int>(std::ceil(cfg.outlineWidth)), 1, 64);
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (x == 0 && y == 0) continue;
                if (x * x + y * y > radius * radius) continue;
                DrawTextLayoutWithOffset(renderTarget.Get(), textLayout.Get(), outlineBrush.Get(), origin, static_cast<float>(x), static_cast<float>(y));
            }
        }
    }

    DrawTextLayoutWithOffset(renderTarget.Get(), textLayout.Get(), textBrush.Get(), origin, 0.0f, 0.0f);

    hr = renderTarget->EndDraw();
    if (FAILED(hr)) throw std::runtime_error("render target draw failed");

    SaveBitmapAsPng(wicFactory.Get(), bitmap.Get(), outputPath);

    // DirectWrite/D2D/WIC のCOMオブジェクトを明示的に解放してから、
    // フォントローダー解除とCOM終了を行う。順序が逆だと終了処理中に落ちることがある。
    textBrush.Reset();
    outlineBrush.Reset();
    shadowBrush.Reset();
    renderTarget.Reset();
    bitmap.Reset();
    textLayout.Reset();
    textFormat.Reset();
    customCollection.Reset();

    if (dwriteFactory && loader) {
        dwriteFactory->UnregisterFontCollectionLoader(loader.Get());
    }
    loader.Reset();
    dwriteFactory.Reset();
    d2dFactory.Reset();
    wicFactory.Reset();

    if (ownsCom) {
        CoUninitialize();
    }
    return result;
}

void WriteReport(const fs::path& reportPath, const RenderResult& result, const fs::path& outputPath) {
    if (reportPath.empty()) return;
    fs::create_directories(reportPath.parent_path());

    nlohmann::json report;
    report["width"] = result.width;
    report["height"] = result.height;
    report["output"] = WideToUtf8(outputPath.generic_wstring());
    report["usedFontFamily"] = WideToUtf8(result.usedFontFamily);
    report["fallbackUsed"] = result.fallbackUsed;

    std::ofstream file(reportPath, std::ios::binary | std::ios::trunc);
    file << report.dump(2);
}

fs::path GetOptionValue(int argc, wchar_t** argv, const wchar_t* optionName) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (_wcsicmp(argv[i], optionName) == 0) {
            return fs::path(argv[i + 1]);
        }
    }
    return {};
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 2 || _wcsicmp(argv[1], L"render") != 0) {
            std::wcerr << L"Usage: TextPngTool.exe render -config <path> -out <path>\n";
            return 2;
        }

        const fs::path configPath = GetOptionValue(argc, argv, L"-config");
        const fs::path outputPath = GetOptionValue(argc, argv, L"-out");
        if (configPath.empty() || outputPath.empty()) {
            std::wcerr << L"config and out are required.\n";
            return 2;
        }

        RenderConfig config = LoadConfig(configPath);
        RenderResult result = RenderToPng(config, outputPath);
        WriteReport(config.reportPath, result, outputPath);
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "TextPngTool failed: " << ex.what() << "\n";
        return 1;
    }
}
