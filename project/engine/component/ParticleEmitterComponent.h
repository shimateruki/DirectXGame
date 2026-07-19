#pragma once

#include "ObjectComponent.h"

#include <string>

// ParticleEmitterComponentは、Objectへ追従するCPU/GPU Particle設定を所有します。
class ParticleEmitterComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "ParticleEmitter";

    std::string_view GetTypeId() const override { return kTypeId; }

    const std::string& GetCpuParticle() const { return cpuParticle_; }
    void SetCpuParticle(const std::string& name) { cpuParticle_ = name; }

    const std::string& GetGpuParticle() const { return gpuParticle_; }
    void SetGpuParticle(const std::string& name) { gpuParticle_ = name; }

    float& GetEmissionTimer() { return emissionTimer_; }
    float GetEmissionTimer() const { return emissionTimer_; }
    void ResetEmissionTimer() { emissionTimer_ = 0.0f; }

private:
    std::string cpuParticle_;
    std::string gpuParticle_;
    float emissionTimer_ = 0.0f;
};
