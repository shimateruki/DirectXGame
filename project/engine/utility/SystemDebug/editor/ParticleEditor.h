#pragma once
#include "IEditable.h"
#include <string>

class ParticleSystem;
class SceneManager;

class ParticleEditor : public IEditable {
public:
    void Initialize(SceneManager* sceneManager);

    void Update(); // ロジック更新用

    // Inspectorに表示するUI描画処理
    void DrawImGui() override;

    // Inspector上部に表示される名前
    std::string GetName() override { return "Particle Editor"; }

private:
    SceneManager* sceneManager_ = nullptr;
    ParticleSystem* targetSystem_ = nullptr;
};