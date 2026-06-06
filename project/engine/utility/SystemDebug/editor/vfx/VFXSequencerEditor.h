#pragma once

#include "IEditable.h"
#include "VFXSequencer.h"
#include <string>
#include <vector>

class VFXSequencerEditor : public IEditable {
public:
    void Initialize();
    void Update(float deltaTime);
    void DrawImGui() override;

    std::string GetName() override { return "VFX Cue Editor"; }
    void RefreshFileList();

private:
    void DrawTimelinePreview();
    void DrawEventEditor(int index, VFXEvent& event);
    void DrawPresetCombo(const char* label, std::string& value, const std::vector<std::string>& list);
    void AddDefaultEvent(VFXEventType type);
    const char* GetEventTypeName(VFXEventType type) const;

    VFXSequencer previewSequencer_;
    char sequenceNameInput_[64] = "UltimateMeteor";
    std::vector<std::string> particlePresetList_;
    std::vector<std::string> sequenceFileList_;
    std::vector<std::string> meshEffectList_;
    std::vector<std::string> seFileList_;
};
