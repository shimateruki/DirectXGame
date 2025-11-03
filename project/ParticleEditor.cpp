#include "ParticleEditor.h"
#include "imgui.h"

void ParticleEditor::Initialize(ParticleSystem* particleSystem) {
    targetSystem_ = particleSystem;
}

void ParticleEditor::Update() {
    if (targetSystem_ == nullptr) {
        return;
    }

    // ★ ImGuiウィンドウで EmitterParams を直接編集！
    if (ImGui::Begin("Particle Editor")) {

        // ターゲットのパラメータへの参照
        ParticleSystem::EmitterParams& params = targetSystem_->params_;

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
    ImGui::End();
}