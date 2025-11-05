#pragma once
class ParticleSystem;
class SceneManager;

class ParticleEditor {
public:
    void Initialize(SceneManager* sceneManager);

    void Update(); // ★ ロジック用 (今は空)
    void DrawImGui(); // ★ ImGui描画用 (旧 Update)

private:
    SceneManager* sceneManager_ = nullptr;
    ParticleSystem* targetSystem_ = nullptr;
};