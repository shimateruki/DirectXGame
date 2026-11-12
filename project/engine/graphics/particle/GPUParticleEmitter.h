#pragma once
#include <string>
#include "engine/utility/math/Math.h"

namespace engine { class Object3D; }

// 担当：Object3dに追従する位置からGPU粒子を定期発生させる。
// 関連：Object3D（追従対象）、GpuParticleManager（発生先）
// 主な入口：Initialize / Play / Update / EmitOnce / Stop
class GpuParticleEmitter {
public:
    GpuParticleEmitter() = default;

    ~GpuParticleEmitter() = default;

    /// <summary>使用するプリセット名と追従対象を設定する。</summary>
    /// <param name="presetName">GPUParticleManagerに登録されたプリセット名。</param>
    /// <param name="targetObject">発生元のObject3d。所有しない。未指定時はオフセットをワールド座標として使う。</param>
    void Initialize(const std::string& presetName, engine::Object3D* targetObject = nullptr);

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
    engine::Object3D* targetObject_ = nullptr; // 所有しない追従先。
    engine::Vector3 offset_ = engine::Vector3::ZERO; // 対象からの位置補正。対象なしならワールド位置。

    bool isPlaying_ = false;
    float emitTimer_ = 0.0f; // 次回発生へ向けた蓄積秒数。
    float emitInterval_ = 0.1f; // 発生間隔（秒）。0以下は更新ごとに一度。
};
