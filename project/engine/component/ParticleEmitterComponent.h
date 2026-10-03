#pragma once

#include "ObjectComponent.h"

#include <string>

/// <summary>Objectへ追従するCPU/GPU粒子プリセットと発生間隔の状態を保持する。</summary>
class ParticleEmitterComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "ParticleEmitter";

    /// <summary>保存と型判定に使うComponent種別を返す。</summary>
    std::string_view GetTypeId() const override { return kTypeId; }

    // CPU/GPU粒子のプリセット名。
    const std::string& GetCpuParticle() const { return cpuParticle_; }
    void SetCpuParticle(const std::string& name) { cpuParticle_ = name; }

    const std::string& GetGpuParticle() const { return gpuParticle_; }
    void SetGpuParticle(const std::string& name) { gpuParticle_ = name; }

    // 発生間隔の経過時間。非const版は呼び出し側から更新できる。
    float& GetEmissionTimer() { return emissionTimer_; }
    float GetEmissionTimer() const { return emissionTimer_; }
    void ResetEmissionTimer() { emissionTimer_ = 0.0f; }

private:
    std::string cpuParticle_;
    std::string gpuParticle_;
    float emissionTimer_ = 0.0f;
};
