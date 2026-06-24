#pragma once

#include "IEditable.h"

#include <chrono>
#include <filesystem>
#include <string>

class DebugEditor;

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
    bool EncodeRecordingToMp4();

    DebugEditor* editor_ = nullptr;
    CaptureArea captureArea_ = CaptureArea::GameView;
    bool recording_ = false;
    bool encodeMp4OnStop_ = true;
    float recordFps_ = 30.0f;
    float recordingFps_ = 30.0f;
    int frameIndex_ = 0;
    std::filesystem::path recordingDir_;
    std::filesystem::path lastOutputPath_;
    std::chrono::steady_clock::time_point nextFrameTime_{};
    std::string recordingTimestamp_;
    std::string statusText_ = "撮影範囲を選んで、スクリーンショットまたは録画を開始してください。";
};
