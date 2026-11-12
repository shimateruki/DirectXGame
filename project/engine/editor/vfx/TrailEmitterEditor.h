#pragma once
#include "IEditable.h"
#include "TrailEmitter.h"
#include <string>
#include <vector>

class SceneManager;
namespace engine { class Object3D; }
class BaseScene;

// 担当：軌跡VFXの追従・発生設定を編集し、ダミー対象で確認する。
// 関連：TrailEmitter（再生）、TrailEmitterConfig（設定）、EffectPreviewStage（確認環境）
// 主な入口：Initialize / Update / DrawImGui
class TrailEmitterEditor : public IEditable {
public:
    // シーンへの接続と、軌跡の編集・ダミー確認を準備する。
    void Initialize(SceneManager* sceneManager);

        // プレビュー中のトレイル発生や寿命を更新します。
void Update(float deltaTime);

        // トレイルの幅、色、寿命、保存読み込みUIを描画します。
void DrawImGui() override;

    std::string GetName() override { return "Trail Emitter Editor"; }

        // 保存済み設定や利用可能リソースの一覧を更新します。
void RefreshFileLists();

private:
    // 軌跡の参照プリセットと放出条件をJSONに書き出し、一覧へ即時反映する。
    void Save(const std::string& name);

    // 保存された放出条件をエミッターへ反映し、参照する素材名も復元する。
    void Load(const std::string& name);

    // 再生時計は常に戻し、既存の効果を消すかは呼び出し側の指定に従う。
    void ResetDummyPreview(bool clearEffects);

    // 実キャラクターがいなくても往復するダミーを動かし、Trail発生条件を確認する。
    void StepDummyPreview(float deltaTime, bool allowLoop);

    SceneManager* sceneManager_ = nullptr; // 現在シーンの取得元。借用参照。
    BaseScene* lastScene_ = nullptr;

    // 編集対象のトレイル本体。
    TrailEmitter emitter_;

    // シーン内オブジェクトへの追従状態。
    engine::Object3D* targetObject_ = nullptr;
    bool isTracking_ = false;

    char configNameBuf_[64] = "myTrail";

    // 対象未選択時に動きを確認するためのダミープレビュー。
    bool isDummyRunning_ = false;
    float previewTime_ = 0.0f;
    float previewDuration_ = 2.0f; // ダミー軌跡の再生秒数。
    float previewRange_ = 5.0f;
    engine::Vector3 dummyPos_ = engine::Vector3::ZERO;
    engine::Vector3 lastDummyPos_ = engine::Vector3::ZERO;
    bool dummyFirstFrame_ = true;
    int lastStagePlayRequestSerial_ = 0; // 前回処理したプレビュー再生要求の番号。
    int lastStageStopRequestSerial_ = 0; // 前回処理したプレビュー停止要求の番号。
    int lastStageSeekRequestSerial_ = 0; // 前回処理したプレビューシーク要求の番号。

    bool wasPlaying_ = false;

    // 選択肢として表示するファイル一覧。
    std::vector<std::string> meshEffectList_;
    std::vector<std::string> gpuParticleList_;
    std::vector<std::string> savedConfigList_;
};
