#pragma once
class ParticleSystem;
class SceneManager;

class ParticleEditor {
public:
    void Initialize(SceneManager* sceneManager);

    void Update(); // ロジック用
    void DrawImGui(); //ImGui描画用

private:
    SceneManager* sceneManager_ = nullptr;
    ParticleSystem* targetSystem_ = nullptr;
};