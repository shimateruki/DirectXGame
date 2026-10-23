#pragma once
#include "IEditable.h"
#include <string>

namespace engine { class ParticleSystem; }
class SceneManager;

/// <summary>
/// CPUパーティクルの対象システムを選び、発生やパラメータを調整する。
/// </summary>
// ParticleEditorは、CPU側の通常パーティクル設定を編集するためのシンプルなツールです。
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
    SceneManager* sceneManager_ = nullptr;
    engine::ParticleSystem* targetSystem_ = nullptr;
    float previewTime_ = 0.0f;
    int lastStagePlayRequestSerial_ = 0;
    int lastStageStopRequestSerial_ = 0;
    int lastStageSeekRequestSerial_ = 0;
    char textureSearch_[128] = {};
    char presetSearch_[128] = {};
    char saveName_[64] = "NewEffect";
};
