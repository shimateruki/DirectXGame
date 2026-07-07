#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "IEditable.h"

#include <chrono>
#include <filesystem>
#include <string>

class DebugEditor;
struct IMFSinkWriter;

/// Game Viewやウィンドウ領域のスクリーンショット、録画、保存先管理を行うキャプチャツール。
class CaptureToolWindow : public IEditable {
public:
    enum class CaptureArea {
        GameView,
        Window,
        Desktop
    };    void Initialize(DebugEditor* editor);
    /// キャプチャ範囲、保存先、録画状態、ショートカット案内を描画する。
    void DrawImGui() override;

    // ImGuiを非表示にしている撮影モードでも、ショートカットと録画更新を動かす。
    /// キャプチャ用ショートカット入力を監視し、撮影や録画開始を呼び出す。
    void UpdateHotkeys();
    void SetForceGameViewClientCapture(bool enabled) { forceGameViewClientCapture_ = enabled; }

    std::string GetName() override { return "キャプチャツール (Capture Tool)"; }
private:
    /// 現在の設定に従って静止画を保存する。
    void CaptureScreenshot();
    void StartRecording();
    void StopRecording();
    void UpdateRecording();
    bool CaptureToFile(const std::filesystem::path& path);
    /// Media FoundationのSinkWriterを準備し、指定範囲の録画を開始する。
    bool StartMediaFoundationRecording(const std::filesystem::path& outputPath, const RECT& captureRect);
    /// 現在フレームをキャプチャして動画ストリームへ1枚分書き込む。
    bool WriteRecordingFrame();
    void CloseRecordingResources();

    DebugEditor* editor_ = nullptr;
    CaptureArea captureArea_ = CaptureArea::GameView;    bool recording_ = false;
    bool forceGameViewClientCapture_ = false;    bool mediaFoundationStarted_ = false;
    bool recordingComInitialized_ = false;
    float recordFps_ = 30.0f;
    float recordingFps_ = 30.0f;
    IMFSinkWriter* sinkWriter_ = nullptr;
    DWORD videoStreamIndex_ = 0;
    int recordingWidth_ = 0;
    int recordingHeight_ = 0;
    RECT recordingRect_{};
    LONGLONG recordingFrameDuration100ns_ = 0;
    LONGLONG recordingNextSampleTime100ns_ = 0;
    std::chrono::steady_clock::time_point nextFrameTime_{};
    std::filesystem::path recordingOutputPath_;
    std::filesystem::path lastOutputPath_;
    std::string recordingTimestamp_;
    std::string statusText_ = "撮影範囲を選んで、スクリーンショットまたは録画を開始してください。";
};
