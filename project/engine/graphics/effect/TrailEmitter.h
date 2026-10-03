#pragma once
#include <string>
#include "engine/utility/math/Math.h"

class Object3d;

/// <summary>移動軌跡に沿うVFXのプリセットと発生条件。</summary>
struct TrailEmitterConfig {
    std::string meshEffectPreset;             // エフェクトJSONファイル名 (拡張子なし, 空=無効)
    std::string gpuParticlePreset;            // GPUパーティクルプリセット名 (空=無効)
    float       emitDistance  = 0.3f;         // この距離を移動したら1個Emit
    Vector3     scale         = { 1, 1, 1 };  // エフェクトのスケール倍率
    bool        emitMesh      = true;         // メッシュエフェクトを出すか
    bool        emitParticle  = false;        // GPUパーティクルを出すか
    bool        autoOrient    = true;         // 移動方向に向きを自動合わせするか
};

/// <summary>対象の移動距離に応じてメッシュ・GPUパーティクルの軌跡を発生させる。</summary>
/// <remarks>Startで追跡し、毎フレームUpdate、終了時にStopを呼ぶ。</remarks>
class TrailEmitter {
public:
    /// <summary>追跡対象を設定し、軌跡の発生を開始する。</summary>
    /// <param name="target">移動を追跡するObject3d。所有しない。</param>
    void Start(Object3d* target);

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
    Vector3 GetLastEmitPos() const { return lastEmitPos_; }

private:
    TrailEmitterConfig config_;
    Object3d* target_     = nullptr;
    Vector3   lastEmitPos_ = { 0, 0, 0 };
    bool      hasLastPos_  = false;
    bool      isActive_    = false;
};
