#pragma once

#include "ObjectComponent.h"

#include <string>

// 担当：CPU・GPU粒子の設定名と、発生間隔の経過時間を保持する。
// 関連：ObjectComponent（共通型）、Object3D（発生処理）、ParticleManager / GpuParticleManager
// 主な入口：GetCpuParticle / GetGpuParticle / ResetEmissionTimer
class ParticleEmitterComponent final : public ObjectComponent {
public:
    static constexpr std::string_view kTypeId = "ParticleEmitter"; // 保存と実行時の型判定に使う固定ID。

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

    // 発生間隔の経過秒数を0へ戻す。
    void ResetEmissionTimer() { emissionTimer_ = 0.0f; }

private:
    std::string cpuParticle_; // CPU粒子の設定名。空なら未設定。
    std::string gpuParticle_; // GPU粒子の設定名。空なら未設定。
    float emissionTimer_ = 0.0f; // 前回の発生からの蓄積秒数。
};
