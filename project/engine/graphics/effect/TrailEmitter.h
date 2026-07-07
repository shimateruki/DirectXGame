#pragma once
#include <string>
#include "engine/utility/math/Math.h"

class Object3d;

// ===================================================
// トレイルエミッターの設定データ
// ===================================================
// TrailEmitterConfigは、移動軌跡に沿って発生させるVFXのプリセットと発生条件をまとめます。
struct TrailEmitterConfig {
    std::string meshEffectPreset;             // エフェクトJSONファイル名 (拡張子なし, 空=無効)
    std::string gpuParticlePreset;            // GPUパーティクルプリセット名 (空=無効)
    float       emitDistance  = 0.3f;         // この距離を移動したら1個Emit
    Vector3     scale         = { 1, 1, 1 };  // エフェクトのスケール倍率
    bool        emitMesh      = true;         // メッシュエフェクトを出すか
    bool        emitParticle  = false;        // GPUパーティクルを出すか
    bool        autoOrient    = true;         // 移動方向に向きを自動合わせするか
};

// ===================================================
// TrailEmitter
//   対象Object3dの移動軌跡に沿って、一定距離ごとに
//   メッシュエフェクト / GPUパーティクルを発生させる。
//
//   使い方:
//     emitter.Start(swordTipObject);  // 攻撃開始時
//     emitter.Update(dt);             // 毎フレーム
//     emitter.Stop();                 // 攻撃終了時
// ===================================================
// TrailEmitterは、対象Object3dの移動距離に応じて軌跡エフェクトを連続発生させます。
class TrailEmitter {
public:
    // 追跡開始 (攻撃モーション開始時などに呼ぶ)
        // 追跡対象を設定し、軌跡エフェクトの発生を開始します。
void Start(Object3d* target);

    // 追跡停止
        // 軌跡エフェクトの発生を停止します。
void Stop();

    // 毎フレーム呼ぶ
        // 対象の移動距離を測り、条件を満たしたらVFXを発生させます。
void Update(float deltaTime);

    bool IsActive() const { return isActive_; }

    TrailEmitterConfig&       GetConfig()       { return config_; }
    const TrailEmitterConfig& GetConfig() const { return config_; }
    Vector3 GetLastEmitPos() const { return lastEmitPos_; }

private:
    TrailEmitterConfig config_;
    Object3d* target_     = nullptr;
    Vector3   lastEmitPos_ = { 0, 0, 0 };
    bool      hasLastPos_  = false;
    bool      isActive_    = false;
};
