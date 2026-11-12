#pragma once

#include "IEditable.h"
#include "VfxSequencer.h"
#include <string>
#include <vector>

// 担当：複数VFXイベントを時間軸に配置し、保存・再生確認する。
// 関連：VfxSequencer（再生）、VfxEvent（定義）、EffectPreviewStage（プレビュー）
// 主な入口：Initialize / Update / DrawImGui / RequestOpenSequence
class VfxSequencerEditor : public IEditable {
public:
    // プレビュー用Sequencerを初期化し、参照できるSequenceを収集します。
    void Initialize();

        // プレビュー再生中のタイムラインを進めます。
void Update(float deltaTime);

        // イベント一覧、タイムライン、保存読み込み、プレビュー操作UIを描画します。
void DrawImGui() override;

    std::string GetName() override { return "VFX Cue Editor"; }

        // 保存済みVFXシーケンスの一覧を更新します。
void RefreshFileList();

    // 別ツールからの読込も確認を挟み、編集中の内容を保護します。
    void RequestOpenSequence(const std::string& name);

private:
    // 専用Stageを優先し、通常画面ではカメラ基準の再生位置へ戻します。
    engine::Vector3 ResolvePreviewPosition() const;

        // 現在編集中のシーケンスをプレビュー再生します。
void PlayPreview();

        // VFXイベントの発生タイミングをタイムラインとして表示します。
void DrawTimelinePreview();

    // 指定のVFXイベントの設定欄を描画する。
    void DrawEventEditor(int index, VfxEvent& event);

    // プリセット候補の一覧を表示し、選択名を更新する。
    void DrawPresetCombo(const char* label, std::string& value, const std::vector<std::string>& list);

    // Event型に応じた既存Presetを初期値にし、空の選択を減らします。
    void AddDefaultEvent(VfxEventType type);

    const char* GetEventTypeName(VfxEventType type) const;

    VfxSequencer previewSequencer_;
    std::string pendingSequence_;

    std::string fileStatus_;
    bool openRequested_ = false;

    int selectedEvent_ = -1; // 選択イベント。-1は未選択。
    int draggingEvent_ = -1;
    float dragStartTime_ = 0.0f;
    float dragTimelineDuration_ = 3.0f; // ドラッグ開始時の表示時間範囲（秒）。

    float timelineDuration_ = 5.0f; // タイムラインの表示時間範囲（秒）。
    bool snapTimeline_ = true;

    char sequenceNameInput_[64] = "UltimateMeteor";

    std::vector<std::string> particlePresetList_;
    std::vector<std::string> sequenceFileList_;
    std::vector<std::string> meshEffectList_;
    std::vector<std::string> seFileList_;

    bool isPreviewMode_ = true;
    float previewDistance_ = 5.0f;
    float previewRootScale_ = 1.0f;
    float previewPlaybackSpeed_ = 1.0f;

    int lastStagePlayRequestSerial_ = 0; // 前回処理したプレビュー再生要求の番号。
    int lastStageStopRequestSerial_ = 0; // 前回処理したプレビュー停止要求の番号。
    int lastStageSeekRequestSerial_ = 0; // 前回処理したプレビューシーク要求の番号。
};
