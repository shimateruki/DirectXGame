#pragma once
#include <string>
#include "engine/utility/math/Math.h"

namespace engine { class Object3d; }

/// <summary>Object3dに追従してGPUパーティクルを定期発生させる。</summary>
class GPUParticleEmitter {
public:
    GPUParticleEmitter() = default;
    ~GPUParticleEmitter() = default;

    /// <summary>使用するプリセット名と追従対象を設定する。</summary>
    /// <param name="presetName">GPUParticleManagerに登録されたプリセット名。</param>
    /// <param name="targetObject">発生元のObject3d。所有しない。未指定時はオフセットをワールド座標として使う。</param>
    void Initialize(const std::string& presetName, engine::Object3d* targetObject = nullptr);

    /// <summary>再生中なら経過時間に応じて粒子を発生させる。</summary>
    /// <param name="deltaTime">前回更新からの経過秒数。</param>
    void Update(float deltaTime);

    /// <summary>定期発生を開始する。次の更新で最初の粒子を出す。</summary>
    void Play();

    /// <summary>定期発生を停止する。</summary>
    void Stop();

    /// <summary>再生状態に関係なく一度だけ発生させる。</summary>
    void EmitOnce();

    /// <summary>発生元からの位置オフセットを指定する。</summary>
    void SetOffset(const engine::Vector3& offset) { offset_ = offset; }

    /// <summary>発生間隔を秒単位で指定する。0以下は更新ごとに一度発生させる。</summary>
    void SetInterval(float interval) { emitInterval_ = interval; }

    /// <summary>使用する粒子プリセットを切り替える。</summary>
    void SetPresetName(const std::string& presetName) { presetName_ = presetName; }

    /// <summary>定期発生中かを返す。</summary>
    bool IsPlaying() const { return isPlaying_; }

    /// <summary>現在のプリセット名を返す。</summary>
    const std::string& GetName() const { return presetName_; }

private:
    std::string presetName_ = "";
    engine::Object3d* targetObject_ = nullptr; // 所有しない追従先。
    engine::Vector3 offset_ = { 0.0f, 0.0f, 0.0f };

    bool isPlaying_ = false;
    float emitTimer_ = 0.0f;
    float emitInterval_ = 0.1f;
};
