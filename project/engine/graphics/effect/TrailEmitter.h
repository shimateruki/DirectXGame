#pragma once
#include <string>
#include "engine/utility/math/Math.h"

namespace engine { class Object3D; }

/// <summary>移動軌跡に沿うVFXのプリセットと発生条件。</summary>
// 関連：TrailEmitter（この型を使う機能の本体）
struct TrailEmitterConfig {
    std::string meshEffectPreset;             // エフェクトJSONファイル名 (拡張子なし, 空=無効)
    std::string gpuParticlePreset;            // GPUパーティクルプリセット名 (空=無効)
    float       emitDistance  = 0.3f;         // この距離を移動したら1個Emit
    engine::Vector3     scale         = { 1, 1, 1 };  // エフェクトのスケール倍率
    bool        emitMesh      = true;         // メッシュエフェクトを出すか
    bool        emitParticle  = false;        // GPUパーティクルを出すか
    bool        autoOrient    = true;         // 移動方向に向きを自動合わせするか
};

// 担当：対象の移動距離に応じて、メッシュ・GPU粒子の軌跡を発生させる。
// 関連：TrailEmitterConfig（設定）、Object3D（追従）、MeshEffectManager / GpuParticleManager
// 主な入口：Start / Update / Stop
class TrailEmitter {
public:
    /// <summary>追跡対象を設定し、軌跡の発生を開始する。</summary>
    /// <param name="target">移動を追跡するObject3d。所有しない。</param>
    // 対象は借用する。対象を破棄する側は先に Stop し、追従中の無効参照を残さない。
    void Start(engine::Object3D* target);

    /// <summary>軌跡の発生を停止する。</summary>
    void Stop();

    /// <summary>対象の移動距離を調べ、設定距離ごとにVFXを発生させる。</summary>
    void Update(float deltaTime);

    /// <summary>追跡中かを返す。</summary>
    bool IsActive() const { return isActive_; }

    /// <summary>編集可能な発生設定を返す。</summary>
    TrailEmitterConfig&       GetConfig()       { return config_; }

    /// <summary>現在の発生設定を返す。</summary>
    const TrailEmitterConfig& GetConfig() const { return config_; }

    /// <summary>最後にエフェクトを発生させた位置を返す。</summary>
    engine::Vector3 GetLastEmitPos() const { return lastEmitPos_; }

private:
    TrailEmitterConfig config_;
    engine::Object3D* target_     = nullptr;
    engine::Vector3 lastEmitPos_ = engine::Vector3::ZERO;
    bool      hasLastPos_  = false;
    bool      isActive_    = false;
};
