#pragma once

#include "IEditable.h"

#include <string>

class DebugEditor;

class AudioSettingsWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "音声設定 (Audio Settings)"; }

private:
    bool MatchesSearch(const std::string& text) const;

    DebugEditor* editor_ = nullptr;
    char searchBuffer_[128] = "";
    std::string statusText_ = "SE/BGMの音量を調整できます。";
};
