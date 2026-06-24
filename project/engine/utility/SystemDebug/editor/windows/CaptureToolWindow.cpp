#define NOMINMAX
#include "CaptureToolWindow.h"

#include "DebugEditor.h"
#include "DirectXCommon.h"
#include "IconsFontAwesome5.h"
#include "WinApp.h"
#include "imgui.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace fs = std::filesystem;

namespace {

struct CapturedImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> bgra;
};

std::wstring Quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::string PathToUtf8(const fs::path& path) {
    return WideToUtf8(path.wstring());
}

std::string MakeTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    localtime_s(&localTime, &now);

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y%m%d_%H%M%S");
    return stream.str();
}

fs::path GetCaptureRoot() {
    std::error_code ec;
    fs::path root = fs::current_path(ec);
    if (ec || root.empty()) {
        root = fs::path(L".");
    }
    return root / "output" / "captures";
}

std::string FormatFrameName(int frameIndex) {
    std::ostringstream stream;
    stream << "frame_" << std::setw(6) << std::setfill('0') << frameIndex << ".png";
    return stream.str();
}

RECT ClampRectToVirtualScreen(const RECT& source) {
    RECT screen{
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)
    };

    RECT result{
        std::max(source.left, screen.left),
        std::max(source.top, screen.top),
        std::min(source.right, screen.right),
        std::min(source.bottom, screen.bottom)
    };
    return result;
}

HWND GetMainWindowHandle() {
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    if (dxCommon && dxCommon->GetWinApp()) {
        HWND hwnd = dxCommon->GetWinApp()->GetHwnd();
        if (hwnd) {
            return hwnd;
        }
    }

    HWND active = GetActiveWindow();
    if (active) {
        return active;
    }
    return GetForegroundWindow();
}

bool ResolveCaptureRect(DebugEditor* editor, CaptureToolWindow::CaptureArea area, RECT& outRect, std::string& errorMessage) {
    switch (area) {
    case CaptureToolWindow::CaptureArea::GameView: {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        if (!editor || !editor->GetGameViewScreenRect(left, top, right, bottom)) {
            errorMessage = "Game Viewの位置がまだ取得できていません。Game Viewを一度表示してから撮影してください。";
            return false;
        }
        outRect = { left, top, right, bottom };
        outRect = ClampRectToVirtualScreen(outRect);
        break;
    }
    case CaptureToolWindow::CaptureArea::Window: {
        HWND hwnd = GetMainWindowHandle();
        if (!hwnd || !GetWindowRect(hwnd, &outRect)) {
            errorMessage = "ウィンドウ全体の撮影範囲を取得できませんでした。";
            return false;
        }
        outRect = ClampRectToVirtualScreen(outRect);
        break;
    }
    case CaptureToolWindow::CaptureArea::Desktop:
        outRect = {
            GetSystemMetrics(SM_XVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)
        };
        break;
    }

    if (outRect.right <= outRect.left || outRect.bottom <= outRect.top) {
        errorMessage = "撮影範囲のサイズが0です。";
        return false;
    }
    return true;
}

bool CaptureScreenRect(const RECT& rect, CapturedImage& outImage, std::string& errorMessage) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        errorMessage = "撮影範囲のサイズが不正です。";
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) {
        errorMessage = "画面のDCを取得できませんでした。";
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        ReleaseDC(nullptr, screenDc);
        errorMessage = "キャプチャ用ビットマップを作成できませんでした。";
        return false;
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    if (!memoryDc) {
        DeleteObject(bitmap);
        ReleaseDC(nullptr, screenDc);
        errorMessage = "キャプチャ用DCを作成できませんでした。";
        return false;
    }

    HGDIOBJ oldObject = SelectObject(memoryDc, bitmap);
    BOOL copied = BitBlt(memoryDc, 0, 0, width, height, screenDc, rect.left, rect.top, SRCCOPY | CAPTUREBLT);

    if (oldObject) {
        SelectObject(memoryDc, oldObject);
    }
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    if (!copied) {
        DeleteObject(bitmap);
        errorMessage = "画面のコピーに失敗しました。";
        return false;
    }

    const size_t byteSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    outImage.width = width;
    outImage.height = height;
    outImage.bgra.resize(byteSize);
    std::memcpy(outImage.bgra.data(), bits, byteSize);
    DeleteObject(bitmap);

    for (size_t i = 3; i < outImage.bgra.size(); i += 4) {
        outImage.bgra[i] = 255;
    }
    return true;
}

bool SavePngWithWic(const fs::path& path, const CapturedImage& image, std::string& errorMessage) {
    if (image.width <= 0 || image.height <= 0 || image.bgra.empty()) {
        errorMessage = "保存する画像データが空です。";
        return false;
    }

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        errorMessage = "保存先フォルダを作成できませんでした: " + ec.message();
        return false;
    }

    HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool shouldUninitialize = SUCCEEDED(coResult);
    if (FAILED(coResult) && coResult != RPC_E_CHANGED_MODE) {
        errorMessage = "COMの初期化に失敗しました。";
        return false;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        errorMessage = "WIC Factoryを作成できませんでした。";
        return false;
    }

    Microsoft::WRL::ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE);
    }

    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(hr)) {
        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(&frame, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Initialize(nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->SetSize(static_cast<UINT>(image.width), static_cast<UINT>(image.height));
    }
    if (SUCCEEDED(hr)) {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        hr = frame->SetPixelFormat(&format);
    }
    if (SUCCEEDED(hr)) {
        const UINT stride = static_cast<UINT>(image.width * 4);
        const UINT bufferSize = static_cast<UINT>(image.bgra.size());
        hr = frame->WritePixels(static_cast<UINT>(image.height), stride, bufferSize, const_cast<BYTE*>(image.bgra.data()));
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }

    if (FAILED(hr)) {
        errorMessage = "PNG保存に失敗しました。";
        return false;
    }
    return true;
}

bool RunHiddenProcessAndWait(const std::wstring& commandLine, DWORD& exitCode) {
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created) {
        exitCode = GetLastError();
        return false;
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

} // namespace

void CaptureToolWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

void CaptureToolWindow::DrawImGui() {
#ifdef USE_IMGUI
    UpdateRecording();

    ImGui::TextColored(ImVec4(0.45f, 0.95f, 1.0f, 1.0f), ICON_FA_CAMERA " キャプチャツール");
    ImGui::TextWrapped("Game Viewだけ、ウィンドウ全体、デスクトップ全体を選んで撮影できます。録画はPNG連番を保存し、ffmpegが使える環境では停止時にMP4も作成します。");
    ImGui::Separator();

    int areaIndex = static_cast<int>(captureArea_);
    constexpr std::array<const char*, 3> kCaptureAreas = {
        "ゲーム画面のみ (Game View)",
        "ウィンドウ全体",
        "デスクトップ全体"
    };
    if (ImGui::Combo("撮影範囲", &areaIndex, kCaptureAreas.data(), static_cast<int>(kCaptureAreas.size()))) {
        captureArea_ = static_cast<CaptureArea>(std::clamp(areaIndex, 0, static_cast<int>(kCaptureAreas.size()) - 1));
    }

    ImGui::SliderFloat("録画FPS", &recordFps_, 1.0f, 60.0f, "%.0f fps");
    ImGui::Checkbox("停止時にMP4化する (ffmpeg)", &encodeMp4OnStop_);
    ImGui::Separator();

    ImGui::BeginDisabled(recording_);
    if (ImGui::Button(ICON_FA_CAMERA " スクリーンショット", ImVec2(210.0f, 0.0f))) {
        CaptureScreenshot();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (!recording_) {
        if (ImGui::Button(ICON_FA_VIDEO " 録画開始", ImVec2(150.0f, 0.0f))) {
            StartRecording();
        }
    } else {
        if (ImGui::Button(ICON_FA_STOP " 録画停止", ImVec2(150.0f, 0.0f))) {
            StopRecording();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "REC %d frames", frameIndex_);
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", statusText_.c_str());
    if (!lastOutputPath_.empty()) {
        ImGui::TextWrapped("最後の出力: %s", PathToUtf8(lastOutputPath_).c_str());
    }
#endif
}

void CaptureToolWindow::CaptureScreenshot() {
    const fs::path output = GetCaptureRoot() / ("screenshot_" + MakeTimestamp() + ".png");
    if (CaptureToFile(output)) {
        lastOutputPath_ = output;
        statusText_ = "スクリーンショットを保存しました。";
    }
}

void CaptureToolWindow::StartRecording() {
    if (recording_) {
        return;
    }

    recordingTimestamp_ = MakeTimestamp();
    recordingDir_ = GetCaptureRoot() / ("recording_" + recordingTimestamp_);
    std::error_code ec;
    fs::create_directories(recordingDir_, ec);
    if (ec) {
        statusText_ = "録画フォルダを作成できませんでした: " + ec.message();
        return;
    }

    frameIndex_ = 0;
    recordingFps_ = std::clamp(recordFps_, 1.0f, 60.0f);
    nextFrameTime_ = std::chrono::steady_clock::now();
    recording_ = true;
    lastOutputPath_ = recordingDir_;
    statusText_ = "録画を開始しました。";
}

void CaptureToolWindow::StopRecording() {
    if (!recording_) {
        return;
    }

    recording_ = false;
    if (frameIndex_ <= 0) {
        statusText_ = "録画フレームがありませんでした。";
        return;
    }

    statusText_ = "録画を停止しました。PNG連番を保存済みです。";
    if (encodeMp4OnStop_) {
        if (EncodeRecordingToMp4()) {
            statusText_ = "録画を停止し、MP4を作成しました。";
        }
    }
}

void CaptureToolWindow::UpdateRecording() {
    if (!recording_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < nextFrameTime_) {
        return;
    }

    const fs::path framePath = recordingDir_ / FormatFrameName(frameIndex_);
    if (CaptureToFile(framePath)) {
        ++frameIndex_;
    }

    const double secondsPerFrame = 1.0 / static_cast<double>(std::max(recordingFps_, 1.0f));
    nextFrameTime_ = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(secondsPerFrame));
}

bool CaptureToolWindow::CaptureToFile(const fs::path& path) {
    RECT rect{};
    std::string errorMessage;
    if (!ResolveCaptureRect(editor_, captureArea_, rect, errorMessage)) {
        statusText_ = errorMessage;
        return false;
    }

    CapturedImage image;
    if (!CaptureScreenRect(rect, image, errorMessage)) {
        statusText_ = errorMessage;
        return false;
    }

    if (!SavePngWithWic(path, image, errorMessage)) {
        statusText_ = errorMessage;
        return false;
    }

    return true;
}

bool CaptureToolWindow::EncodeRecordingToMp4() {
    if (recordingDir_.empty() || frameIndex_ <= 0) {
        return false;
    }

    fs::path mp4Path = GetCaptureRoot() / ("recording_" + recordingTimestamp_ + ".mp4");
    const int fps = static_cast<int>(std::round(std::clamp(recordingFps_, 1.0f, 60.0f)));
    const fs::path inputPattern = recordingDir_ / "frame_%06d.png";
    const std::wstring command =
        L"ffmpeg -y -framerate " + std::to_wstring(fps) +
        L" -i " + Quote(inputPattern.wstring()) +
        L" -pix_fmt yuv420p " + Quote(mp4Path.wstring());

    DWORD exitCode = 1;
    if (!RunHiddenProcessAndWait(command, exitCode) || exitCode != 0) {
        statusText_ = "MP4化に失敗しました。ffmpegが無い場合はPNG連番を利用してください: " + PathToUtf8(recordingDir_);
        return false;
    }

    lastOutputPath_ = mp4Path;
    return true;
}
