#pragma once
#include "IEditable.h"
#include <string>

namespace engine { class ParticleSystem; }
class SceneManager;

// 担当：CPU粒子の対象システムを選び、発生条件や見た目を調整する。
// 関連：ParticleSystem（対象）、ParticleManager（設定管理）、EffectPreviewStage
// 主な入口：Initialize / Update / DrawImGui
class ParticleEditor : public IEditable {
public:
        // パーティクル編集に必要なシーン参照を保持します。
void Initialize(SceneManager* sceneManager);

    // シーンを切り替えた場合は古いParticleSystemを参照せず、毎フレーム現在のシーンから取り直す。
    void Update(float deltaTime, bool sceneIsPlaying = false);

        // パーティクル設定を調整するUIを描画します。
void DrawImGui() override;

    std::string GetName() override { return "Particle Editor"; }

private:
    SceneManager* sceneManager_ = nullptr; // 現在シーンの取得元。借用参照。
    engine::ParticleSystem* targetSystem_ = nullptr;
    float previewTime_ = 0.0f;
    int lastStagePlayRequestSerial_ = 0; // 前回処理したプレビュー再生要求の番号。
    int lastStageStopRequestSerial_ = 0; // 前回処理したプレビュー停止要求の番号。
    int lastStageSeekRequestSerial_ = 0; // 前回処理したプレビューシーク要求の番号。
    char textureSearch_[128] = {};
    char presetSearch_[128] = {};
    char saveName_[64] = "NewEffect";
};
