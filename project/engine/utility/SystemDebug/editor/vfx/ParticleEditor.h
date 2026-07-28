#pragma once
#include "IEditable.h"
#include <string>

class ParticleSystem;
class SceneManager;

/// <summary>
/// CPUパーティクルの対象システムを選び、発生やパラメータを調整する。
/// </summary>
// ParticleEditorは、CPU側の通常パーティクル設定を編集するためのシンプルなツールです。
class ParticleEditor : public IEditable {
public:
        // パーティクル編集に必要なシーン参照を保持します。
void Initialize(SceneManager* sceneManager);
    void Update(float deltaTime, bool sceneIsPlaying = false);
        // パーティクル設定を調整するUIを描画します。
void DrawImGui() override;
    std::string GetName() override { return "Particle Editor"; }

private:
    SceneManager* sceneManager_ = nullptr;
    ParticleSystem* targetSystem_ = nullptr;
    float previewTime_ = 0.0f;
    int lastStagePlayRequestSerial_ = 0;
    int lastStageStopRequestSerial_ = 0;
    int lastStageSeekRequestSerial_ = 0;
};
