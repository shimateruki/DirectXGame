#define NOMINMAX
#include "CaptureToolWindow.h"

#include "DebugEditor.h"
#include "DirectXCommon.h"
#include "IconsFontAwesome5.h"
#include "InputManager.h"
#include "WinApp.h"
#include "imgui.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace fs = std::filesystem;

namespace {

struct CapturedImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> bgra;
};

std::string HresultToString(HRESULT hr) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
    return stream.str();
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
    }    if (root.filename() == L"project" && root.has_parent_path()) {
        return root.parent_path() / "captures";
    }
    return root / "captures";}

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

RECT MakeEvenSizedRect(const RECT& source) {
    RECT result = source;
    int width = static_cast<int>(result.right - result.left);
    int height = static_cast<int>(result.bottom - result.top);
    if (width % 2 != 0) {
        --width;
    }
    if (height % 2 != 0) {
        --height;
    }
    result.right = result.left + std::max(2, width);
    result.bottom = result.top + std::max(2, height);
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

bool ResolveClientRect(RECT& outRect, std::string& errorMessage) {
    HWND hwnd = GetMainWindowHandle();
    if (!hwnd || !::IsWindow(hwnd)) {
        errorMessage = "メインウィンドウを取得できませんでした。";
        return false;
    }

    RECT client{};
    if (!::GetClientRect(hwnd, &client)) {
        errorMessage = "クライアント領域を取得できませんでした。";
        return false;
    }

    POINT topLeft{ client.left, client.top };
    POINT bottomRight{ client.right, client.bottom };
    ::ClientToScreen(hwnd, &topLeft);
    ::ClientToScreen(hwnd, &bottomRight);
    outRect = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
    return true;
}
bool ResolveCaptureRect(DebugEditor* editor, CaptureToolWindow::CaptureArea area, bool forceGameViewClientCapture, RECT& outRect, std::string& errorMessage) {
    switch (area) {    case CaptureToolWindow::CaptureArea::GameView: {
        if (forceGameViewClientCapture) {
            return ResolveClientRect(outRect, errorMessage);
        }

        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        if (!editor || !editor->GetGameViewScreenRect(left, top, right, bottom)) {
            return ResolveClientRect(outRect, errorMessage);
        }
        outRect = { left, top, right, bottom };
        outRect = ClampRectToVirtualScreen(outRect);
        return true;
    }    case CaptureToolWindow::CaptureArea::Window: {
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
        errorMessage = "撮影範囲のサイズが不正です。";
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
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
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

} // namespace

CaptureToolWindow::~CaptureToolWindow() {
    StopRecordingWorker();
    CloseRecordingResources();
}

void CaptureToolWindow::Initialize(DebugEditor* editor) {
    editor_ = editor;
}

void CaptureToolWindow::DrawImGui() {
#ifdef USE_IMGUI
    UpdateRecording();

    const ImGuiIO& io = ImGui::GetIO();
    const bool canUseShortcut =
        !io.WantTextInput &&
        !io.KeyCtrl &&
        !io.KeyAlt &&
        !io.KeySuper;
    if (canUseShortcut) {
        if (!recording_ && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            CaptureScreenshot();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            if (recording_) {
                StopRecording();
            } else {
                StartRecording();
            }
        }
    }

    ImGui::TextColored(ImVec4(0.45f, 0.95f, 1.0f, 1.0f), ICON_FA_CAMERA " キャプチャツール");
    ImGui::TextWrapped("スクリーンショットはPNG、録画はWindows Media Foundationで直接MP4として保存します。ffmpegは不要です。");
    ImGui::TextDisabled("ショートカット: F = スクリーンショット / G = 録画開始・停止 / F10 = 撮影モード切替");
    ImGui::TextDisabled("保存先: %s", PathToUtf8(GetCaptureRoot()).c_str());
    ImGui::Separator();

    ImGui::BeginDisabled(recording_);
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
    ImGui::EndDisabled();
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
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "REC MP4");

        ImGui::TextDisabled(
            "非同期録画: %llu frames / 取りこぼし %llu / 直近 %.1f ms",
            static_cast<unsigned long long>(recordedFrameCount_.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(skippedFrameCount_.load(std::memory_order_relaxed)),
            lastRecordingFrameCostMs_.load(std::memory_order_relaxed));
    }

    ImGui::Separator();
    ImGui::TextWrapped("%s", statusText_.c_str());
    if (!lastOutputPath_.empty()) {
        ImGui::TextWrapped("最後の出力: %s", PathToUtf8(lastOutputPath_).c_str());
    }
#endif
}

void CaptureToolWindow::UpdateHotkeys() {
    UpdateRecording();

#ifdef USE_IMGUI
    bool canUseShortcut = true;
    if (ImGui::GetCurrentContext()) {
        const ImGuiIO& io = ImGui::GetIO();
        canUseShortcut = !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    }
    if (!canUseShortcut) {
        return;
    }
#endif

    InputManager* input = InputManager::GetInstance();
    if (!input) {
        return;
    }

    if (!recording_ && input->IsKeyTriggered(DIK_F)) {
        CaptureScreenshot();
    }
    if (input->IsKeyTriggered(DIK_G)) {
        if (recording_) {
            StopRecording();
        } else {
            StartRecording();
        }
    }
}

void CaptureToolWindow::CaptureScreenshot() {
    const fs::path output = GetCaptureRoot() / ("screenshot_" + MakeTimestamp() + ".png");
    if (CaptureToFile(output)) {
        lastOutputPath_ = output;
        statusText_ = "スクリーンショットを保存しました。";
    }
}

bool CaptureToolWindow::CaptureGameViewToFile(const fs::path& path) {
    const CaptureArea previousArea = captureArea_;
    captureArea_ = CaptureArea::GameView;
    const bool succeeded = CaptureToFile(path);
    captureArea_ = previousArea;

    if (succeeded) {
        lastOutputPath_ = path;
        statusText_ = "シーン視覚監査用のGame Viewを保存しました。";
    }
    return succeeded;
}

void CaptureToolWindow::StartRecording() {
    if (recording_) {
        return;
    }

    RECT captureRect{};
    std::string errorMessage;
    if (!ResolveCaptureRect(editor_, captureArea_, forceGameViewClientCapture_, captureRect, errorMessage)) {
        statusText_ = errorMessage;
        return;
    }

    captureRect = MakeEvenSizedRect(captureRect);
    const int width = static_cast<int>(captureRect.right - captureRect.left);
    const int height = static_cast<int>(captureRect.bottom - captureRect.top);
    if (width < 2 || height < 2) {
        statusText_ = "録画範囲が小さすぎます。";
        return;
    }

    std::error_code ec;
    fs::create_directories(GetCaptureRoot(), ec);
    if (ec) {
        statusText_ = "録画保存先フォルダを作成できませんでした: " + ec.message();
        return;
    }

    StopRecordingWorker();
    recordingTimestamp_ = MakeTimestamp();
    recordingOutputPath_ = GetCaptureRoot() / ("recording_" + recordingTimestamp_ + ".mp4");
    recordingFps_ = std::clamp(recordFps_, 1.0f, 60.0f);
    recordingStopRequested_.store(false, std::memory_order_release);
    recordingWorkerFinished_.store(false, std::memory_order_release);
    recordingWorkerSucceeded_.store(false, std::memory_order_release);
    recordedFrameCount_.store(0, std::memory_order_relaxed);
    skippedFrameCount_.store(0, std::memory_order_relaxed);
    lastRecordingFrameCostMs_.store(0.0f, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(recordingResultMutex_);
        recordingWorkerMessage_.clear();
    }

    try {
        recordingWorker_ = std::thread(
            &CaptureToolWindow::RecordingWorkerMain,
            this,
            recordingOutputPath_,
            captureRect,
            recordingFps_);
    } catch (const std::system_error& error) {
        statusText_ = "録画ワーカーを開始できませんでした: " + std::string(error.what());
        return;
    }

    recording_ = true;
    statusText_ = "録画を開始しました。画面取得とMP4圧縮はバックグラウンドで実行しています。";
}

void CaptureToolWindow::StopRecording() {
    if (!recording_ && !recordingWorker_.joinable()) {
        return;
    }

    StopRecordingWorker();
    ApplyRecordingWorkerResult();
}

void CaptureToolWindow::UpdateRecording() {
    if (!recording_) {
        return;
    }

    if (recordingWorkerFinished_.load(std::memory_order_acquire)) {
        StopRecordingWorker();
        ApplyRecordingWorkerResult();
    }
}

void CaptureToolWindow::RecordingWorkerMain(fs::path outputPath, RECT captureRect, float fps) {
    std::string resultMessage;
    bool succeeded = false;

    try {
        succeeded = StartMediaFoundationRecording(outputPath, captureRect, fps, resultMessage);
        if (succeeded) {
            using Clock = std::chrono::steady_clock;
            const auto frameInterval = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(std::max(fps, 1.0f))));
            const auto recordingStart = Clock::now();
            auto nextFrameTime = recordingStart;

            while (!recordingStopRequested_.load(std::memory_order_acquire)) {
                {
                    std::unique_lock<std::mutex> lock(recordingWaitMutex_);
                    if (recordingWaitCondition_.wait_until(lock, nextFrameTime, [this]() {
                            return recordingStopRequested_.load(std::memory_order_acquire);
                        })) {
                        break;
                    }
                }

                const auto frameStart = Clock::now();
                const LONGLONG sampleTime100ns = static_cast<LONGLONG>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(frameStart - recordingStart).count() / 100);
                if (!WriteRecordingFrame(sampleTime100ns, resultMessage)) {
                    succeeded = false;
                    break;
                }

                recordedFrameCount_.fetch_add(1, std::memory_order_relaxed);
                const auto frameEnd = Clock::now();
                const float frameCostMs = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
                lastRecordingFrameCostMs_.store(frameCostMs, std::memory_order_relaxed);

                nextFrameTime += frameInterval;
                if (nextFrameTime <= frameEnd) {
                    const auto skipped = static_cast<std::uint64_t>((frameEnd - nextFrameTime) / frameInterval) + 1u;
                    skippedFrameCount_.fetch_add(skipped, std::memory_order_relaxed);
                    nextFrameTime += frameInterval * static_cast<Clock::duration::rep>(skipped);
                }
            }

            HRESULT finalizeResult = S_OK;
            if (sinkWriter_) {
                finalizeResult = sinkWriter_->Finalize();
            }
            if (FAILED(finalizeResult)) {
                succeeded = false;
                resultMessage = "MP4の保存確定に失敗しました: " + HresultToString(finalizeResult);
            } else if (succeeded) {
                resultMessage = "録画を停止し、MP4を保存しました。";
            }
        }
    } catch (const std::exception& error) {
        succeeded = false;
        resultMessage = "録画ワーカーで例外が発生しました: " + std::string(error.what());
    } catch (...) {
        succeeded = false;
        resultMessage = "録画ワーカーで不明な例外が発生しました。";
    }

    CloseRecordingResources();
    {
        std::lock_guard<std::mutex> lock(recordingResultMutex_);
        recordingWorkerMessage_ = std::move(resultMessage);
    }
    recordingWorkerSucceeded_.store(succeeded, std::memory_order_release);
    recordingWorkerFinished_.store(true, std::memory_order_release);
}

void CaptureToolWindow::StopRecordingWorker() {
    recordingStopRequested_.store(true, std::memory_order_release);
    recordingWaitCondition_.notify_all();
    if (recordingWorker_.joinable() && recordingWorker_.get_id() != std::this_thread::get_id()) {
        recordingWorker_.join();
    }
}

void CaptureToolWindow::ApplyRecordingWorkerResult() {
    recording_ = false;

    std::string workerMessage;
    {
        std::lock_guard<std::mutex> lock(recordingResultMutex_);
        workerMessage = recordingWorkerMessage_;
    }

    if (recordingWorkerSucceeded_.load(std::memory_order_acquire)) {
        lastOutputPath_ = recordingOutputPath_;
        statusText_ = workerMessage.empty() ? "録画を停止し、MP4を保存しました。" : workerMessage;
    } else {
        statusText_ = workerMessage.empty() ? "録画を完了できませんでした。" : workerMessage;
    }
}

bool CaptureToolWindow::CaptureToFile(const fs::path& path) {
    RECT rect{};
    std::string errorMessage;
    if (!ResolveCaptureRect(editor_, captureArea_, forceGameViewClientCapture_, rect, errorMessage)) {
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

bool CaptureToolWindow::StartMediaFoundationRecording(
    const fs::path& outputPath,
    const RECT& captureRect,
    float fps,
    std::string& errorMessage) {
    CloseRecordingResources();

    HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(coResult) && coResult != RPC_E_CHANGED_MODE) {
        errorMessage = "COMの初期化に失敗しました: " + HresultToString(coResult);
        return false;
    }
    recordingComInitialized_ = SUCCEEDED(coResult);

    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        if (recordingComInitialized_) {
            CoUninitialize();
            recordingComInitialized_ = false;
        }
        errorMessage = "Media Foundationの初期化に失敗しました: " + HresultToString(hr);
        return false;
    }
    mediaFoundationStarted_ = true;

    recordingRect_ = captureRect;
    recordingWidth_ = static_cast<int>(recordingRect_.right - recordingRect_.left);
    recordingHeight_ = static_cast<int>(recordingRect_.bottom - recordingRect_.top);
    recordingFrameDuration100ns_ = static_cast<LONGLONG>(
        10000000.0 / static_cast<double>(std::max(fps, 1.0f)));

    // 対応GPUがある環境ではH.264のハードウェア変換を優先し、SinkWriter側の待機は無効化する。
    Microsoft::WRL::ComPtr<IMFAttributes> writerAttributes;
    if (SUCCEEDED(MFCreateAttributes(&writerAttributes, 2))) {
        writerAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        writerAttributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    }

    Microsoft::WRL::ComPtr<IMFSinkWriter> writer;
    hr = MFCreateSinkWriterFromURL(
        outputPath.wstring().c_str(),
        nullptr,
        writerAttributes.Get(),
        &writer);
    if (FAILED(hr)) {
        CloseRecordingResources();
        errorMessage = "MP4書き込み先の作成に失敗しました: " + HresultToString(hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IMFMediaType> outputType;
    hr = MFCreateMediaType(&outputType);
    if (SUCCEEDED(hr)) hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, 8000000);
    if (SUCCEEDED(hr)) hr = outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(recordingWidth_), static_cast<UINT32>(recordingHeight_));
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(std::round(fps)), 1);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = writer->AddStream(outputType.Get(), &videoStreamIndex_);

    Microsoft::WRL::ComPtr<IMFMediaType> inputType;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inputType);
    if (SUCCEEDED(hr)) hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(recordingWidth_), static_cast<UINT32>(recordingHeight_));
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(std::round(fps)), 1);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(videoStreamIndex_, inputType.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = writer->BeginWriting();

    if (FAILED(hr)) {
        CloseRecordingResources();
        errorMessage = "MP4エンコーダーの準備に失敗しました: " + HresultToString(hr);
        return false;
    }

    sinkWriter_ = writer.Detach();
    return true;
}

bool CaptureToolWindow::WriteRecordingFrame(LONGLONG sampleTime100ns, std::string& errorMessage) {
    if (!sinkWriter_) {
        errorMessage = "録画ライターが初期化されていません。";
        return false;
    }

    // 同じ録画スレッド内で画素バッファを再利用し、毎フレームの再確保を避ける。
    thread_local CapturedImage image;
    if (!CaptureScreenRect(recordingRect_, image, errorMessage)) {
        return false;
    }

    const DWORD bufferSize = static_cast<DWORD>(image.bgra.size());
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(bufferSize, &buffer);
    BYTE* destination = nullptr;
    DWORD maxLength = 0;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&destination, &maxLength, nullptr);
    if (SUCCEEDED(hr)) {
        if (maxLength < bufferSize) {
            hr = E_FAIL;
        } else {
            const size_t stride = static_cast<size_t>(image.width) * 4u;
            for (int y = 0; y < image.height; ++y) {
                const size_t sourceOffset = static_cast<size_t>(image.height - 1 - y) * stride;
                const size_t destinationOffset = static_cast<size_t>(y) * stride;
                std::memcpy(destination + destinationOffset, image.bgra.data() + sourceOffset, stride);
            }
        }
    }
    if (destination) {
        buffer->Unlock();
    }
    if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(bufferSize);

    Microsoft::WRL::ComPtr<IMFSample> sample;
    if (SUCCEEDED(hr)) hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(hr)) hr = sample->SetSampleTime(sampleTime100ns);
    if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(recordingFrameDuration100ns_);
    if (SUCCEEDED(hr)) hr = sinkWriter_->WriteSample(videoStreamIndex_, sample.Get());

    if (FAILED(hr)) {
        errorMessage = "録画フレームの書き込みに失敗しました: " + HresultToString(hr);
        return false;
    }

    return true;
}

void CaptureToolWindow::CloseRecordingResources() {
    if (sinkWriter_) {
        sinkWriter_->Release();
        sinkWriter_ = nullptr;
    }
    if (mediaFoundationStarted_) {
        MFShutdown();
        mediaFoundationStarted_ = false;
    }
    if (recordingComInitialized_) {
        CoUninitialize();
        recordingComInitialized_ = false;
    }
}
