#pragma once

#include "IEditable.h"

class SceneManager;

/// <summary>
/// 敵タイプがFactoryへ登録された後に拡張するためのPreview入口です。
/// </summary>
class EnemyAttackPreviewWindow : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);
    void Finalize();
    void Update(float deltaTime);
    void DrawImGui() override;
    void DrawTimelineWindow();
    std::string GetName() override { return "Enemy Attack Preview"; }
    bool IsEnabled() const { return false; }
    void ApplyEffectPlaybackState() {}

private:
    SceneManager* sceneManager_ = nullptr;
};
