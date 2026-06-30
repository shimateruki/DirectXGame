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

class CaptureToolWindow : public IEditable {
public:
    enum class CaptureArea {
        GameView,
        Window,
        Desktop
    };

    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "キャプチャツール (Capture Tool)"; }

private:
    void CaptureScreenshot();
    void StartRecording();
    void StopRecording();
    void UpdateRecording();
    bool CaptureToFile(const std::filesystem::path& path);
    bool StartMediaFoundationRecording(const std::filesystem::path& outputPath, const RECT& captureRect);
    bool WriteRecordingFrame();
    void CloseRecordingResources();

    DebugEditor* editor_ = nullptr;
    CaptureArea captureArea_ = CaptureArea::GameView;
    bool recording_ = false;
    bool mediaFoundationStarted_ = false;
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
