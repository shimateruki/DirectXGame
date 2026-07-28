#pragma once
#include "IEditable.h"
#include "TrailEmitter.h"
#include <string>
#include <vector>

class SceneManager;
class Object3d;
class BaseScene;

/// <summary>
/// TrailEmitterの追従対象、発生エフェクト、保存設定、ダミープレビューを編集する。
/// </summary>
// TrailEmitterEditorは、軌跡エフェクトの発生設定と保存読み込みを行う編集ツールです。
class TrailEmitterEditor : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);
        // プレビュー中のトレイル発生や寿命を更新します。
void Update(float deltaTime);
        // トレイルの幅、色、寿命、保存読み込みUIを描画します。
void DrawImGui() override;
    std::string GetName() override { return "Trail Emitter Editor"; }

        // 保存済み設定や利用可能リソースの一覧を更新します。
void RefreshFileLists();

private:
    void Save(const std::string& name);
    void Load(const std::string& name);
    void ResetDummyPreview(bool clearEffects);
    void StepDummyPreview(float deltaTime, bool allowLoop);

    SceneManager* sceneManager_ = nullptr;
    BaseScene* lastScene_ = nullptr;

    // 編集対象のトレイル本体。
    TrailEmitter emitter_;

    // シーン内オブジェクトへの追従状態。
    Object3d* targetObject_ = nullptr;
    bool isTracking_ = false;

    char configNameBuf_[64] = "myTrail";

    // 対象未選択時に動きを確認するためのダミープレビュー。
    bool isDummyRunning_ = false;
    float previewTime_ = 0.0f;
    float previewDuration_ = 2.0f;
    float previewRange_ = 5.0f;
    Vector3 dummyPos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 lastDummyPos_ = { 0.0f, 0.0f, 0.0f };
    bool dummyFirstFrame_ = true;
    int lastStagePlayRequestSerial_ = 0;
    int lastStageStopRequestSerial_ = 0;
    int lastStageSeekRequestSerial_ = 0;

    bool wasPlaying_ = false;

    // 選択肢として表示するファイル一覧。
    std::vector<std::string> meshEffectList_;
    std::vector<std::string> gpuParticleList_;
    std::vector<std::string> savedConfigList_;
};
