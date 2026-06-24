#pragma once
#include "IEditable.h"
#include <string>

class ParticleSystem;
class SceneManager;

/// <summary>
/// CPUパーティクルの対象システムを選び、発生やパラメータを調整する。
/// </summary>
class ParticleEditor : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);
    void Update();
    void DrawImGui() override;
    std::string GetName() override { return "Particle Editor"; }

private:
    SceneManager* sceneManager_ = nullptr;
    ParticleSystem* targetSystem_ = nullptr;
};
