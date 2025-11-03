#pragma once
#include "ParticleSystem.h" 

class ParticleEditor {
public:
    void Initialize(ParticleSystem* particleSystem);
    void Update(); // ImGui のウィンドウを描画

private:
    ParticleSystem* targetSystem_ = nullptr;
};