#pragma once

#include "IEditable.h"
#include "VFXSequencer.h"
#include <string>
#include <vector>

// VFXSequencerEditorは、複数のVFXイベントをタイムライン上で並べて演出確認するツールです。
class VFXSequencerEditor : public IEditable {
public:
    void Initialize();
        // プレビュー再生中のタイムラインを進めます。
void Update(float deltaTime);
        // イベント一覧、タイムライン、保存読み込み、プレビュー操作UIを描画します。
void DrawImGui() override;

    std::string GetName() override { return "VFX Cue Editor"; }
        // 保存済みVFXシーケンスの一覧を更新します。
void RefreshFileList();

private:
    Vector3 ResolvePreviewPosition() const;
        // 現在編集中のシーケンスをプレビュー再生します。
void PlayPreview();
        // VFXイベントの発生タイミングをタイムラインとして表示します。
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
    bool isPreviewMode_ = true;
    float previewDistance_ = 5.0f;
    float previewRootScale_ = 1.0f;
    float previewPlaybackSpeed_ = 1.0f;
    int lastStagePlayRequestSerial_ = 0;
    int lastStageStopRequestSerial_ = 0;
    int lastStageSeekRequestSerial_ = 0;
};
