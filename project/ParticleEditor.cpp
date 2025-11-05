#include "ParticleEditor.h"
#include "imgui.h"
#include "SceneManager.h" 
#include "engine/scene/BaseScene.h"    
#include "ParticleSystem.h" 

void ParticleEditor::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
}

// ロジック専用の Update (今は空)
void ParticleEditor::Update() {

}

// ImGui描画専用
void ParticleEditor::DrawImGui() {
    if (sceneManager_ == nullptr) return;

    // 1. シーンマネージャーから現在のシーンを取得
    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        ImGui::Text("No active scene.");
        return;
    }

    // 2. 現在のシーンから ParticleSystem を取得
    ParticleSystem* targetSystem = currentScene->GetParticleSystem();

    if (targetSystem == nullptr) {
        ImGui::Text("Current scene has no particle system.");
        return;
    }


    ParticleSystem::EmitterParams& params = targetSystem->params_;

    ImGui::Checkbox("Emit", &params.isEmitting);
    ImGui::DragFloat("Particles/Sec", &params.particlesPerSecond, 1.0f, 0.0f, 1000.0f);
    ImGui::DragFloat("Lifetime", &params.particleLifetime, 0.1f, 0.1f, 10.0f);
    ImGui::Separator();
    ImGui::DragFloat3("Spawn Pos", &params.spawnPosition.x, 0.1f);
    ImGui::DragFloat3("Spawn Area", &params.spawnArea.x, 0.1f);
    ImGui::DragFloat3("Velocity", &params.initialVelocity.x, 0.1f);
    ImGui::DragFloat3("Velocity Rand", &params.velocityRandomness.x, 0.1f);
    ImGui::Separator();
    ImGui::ColorEdit4("Start Color", &params.startColor.x);
    ImGui::ColorEdit4("End Color", &params.endColor.x);
    ImGui::DragFloat("Start Size", &params.startSize, 0.1f);
    ImGui::DragFloat("End Size", &params.endSize, 0.1f);
}