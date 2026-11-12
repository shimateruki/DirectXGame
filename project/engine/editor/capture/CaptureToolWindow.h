#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "IEditable.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

class DebugEditor;
struct IMFSinkWriter;

// 担当：Game View・ウィンドウ領域の撮影と録画・保存先を管理する。
// 関連：WinApp（画面領域）、DebugEditor（Game View）、GameEditorController（接続）
// 主な入口：Initialize / UpdateHotkeys / DrawImGui / CaptureGameViewToFile
class CaptureToolWindow : public IEditable {
#ifdef USE_IMGUI
#endif
public:
    enum class CaptureArea {
        GameView,
        Window,
        Desktop
    };

    enum class ScreenshotTag {
        Slides,
        Debug
    };

    ~CaptureToolWindow() override;

    // Game Viewの表示範囲を取得するため、Editor参照だけ保持します。
    void Initialize(DebugEditor* editor);

    /// キャプチャ範囲、保存先、録画状態、ショートカット案内を描画する。
    void DrawImGui() override;

    // ImGuiを非表示にしている撮影モードでも、ショートカットと録画更新を動かす。
    /// キャプチャ用ショートカット入力を監視し、撮影や録画開始を呼び出す。
    void UpdateHotkeys();

    void SetForceGameViewClientCapture(bool enabled) { forceGameViewClientCapture_ = enabled; }

    /// 外部のEditorツールからGame Viewを指定先へ保存する。
    bool CaptureGameViewToFile(const std::filesystem::path& path);

    std::string GetName() override { return "キャプチャツール (Capture Tool)"; }

private:
    /// 現在の設定に従って静止画を保存する。
    void CaptureScreenshot();

    /// 直前に手動撮影したPNGを選択中の用途フォルダへ移す。
    void RetagLastScreenshot();

    // Worker起動前に撮影矩形を確定し、無効な領域では録画を始めません。
    void StartRecording();

    // Workerの終了を待ってから結果を回収し、未書込の動画を成功扱いしません。
    void StopRecording();

    // Workerが非同期に終了した場合も、UI側で停止処理と結果表示を完了します。
    void UpdateRecording();

    /// 画面取得と動画エンコードをゲーム更新から分離して実行する。
    void RecordingWorkerMain(std::filesystem::path outputPath, RECT captureRect, float fps);

    // 停止要求を通知し、自スレッドをjoinしないようにしてWorkerを回収します。
    void StopRecordingWorker();

    // Worker共有メッセージをロック下で読み、UIの録画状態へ確定します。
    void ApplyRecordingWorkerResult();

    // 選択した画面範囲を確定してから画素を取得し、PNGへ保存します。
    bool CaptureToFile(const std::filesystem::path& path);

    /// Media FoundationのSinkWriterを準備し、指定範囲の録画を開始する。
    bool StartMediaFoundationRecording(
        const std::filesystem::path& outputPath,
        const RECT& captureRect,
        float fps,
        std::string& errorMessage);

    /// 現在フレームをキャプチャして動画ストリームへ1枚分書き込む。
    bool WriteRecordingFrame(LONGLONG sampleTime100ns, std::string& errorMessage);

    // Media FoundationのWriterを解放してからランタイムを終了します。
    void CloseRecordingResources();

    DebugEditor* editor_ = nullptr;

    CaptureArea captureArea_ = CaptureArea::GameView;
    ScreenshotTag screenshotTag_ = ScreenshotTag::Slides;

    bool recording_ = false;
    bool forceGameViewClientCapture_ = false;

    bool mediaFoundationStarted_ = false;
    bool recordingComInitialized_ = false;

    float recordFps_ = 30.0f;
    float recordingFps_ = 30.0f;

    IMFSinkWriter* sinkWriter_ = nullptr;
    DWORD videoStreamIndex_ = 0;

    int recordingWidth_ = 0;
    int recordingHeight_ = 0;
    RECT recordingRect_{};

    LONGLONG recordingFrameDuration100ns_ = 0; // 録画一フレームの長さ（100ナノ秒単位）。

    std::filesystem::path recordingOutputPath_;
    std::filesystem::path lastOutputPath_;
    std::filesystem::path lastScreenshotPath_;

    std::string recordingTimestamp_;

    std::string statusText_ = "撮影範囲を選んで、スクリーンショットまたは録画を開始してください。";

    // 録画中の重い画面取得とエンコードは専用スレッドだけが担当する。
    std::thread recordingWorker_;
    std::atomic_bool recordingStopRequested_{ false };
    std::atomic_bool recordingWorkerFinished_{ false };
    std::atomic_bool recordingWorkerSucceeded_{ false };
    std::atomic<std::uint64_t> recordedFrameCount_{ 0 };
    std::atomic<std::uint64_t> skippedFrameCount_{ 0 };
    std::atomic<float> lastRecordingFrameCostMs_{ 0.0f };
    std::mutex recordingWaitMutex_;
    std::condition_variable recordingWaitCondition_;
    std::mutex recordingResultMutex_;
    std::string recordingWorkerMessage_;
};
