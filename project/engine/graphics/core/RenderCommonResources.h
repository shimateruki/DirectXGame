#pragma once
#include <memory>

class DirectXCommon;

namespace engine {
class Object3DCommon;
class SpriteCommon;
class ParticleCommon;
class SceneRenderCommands;

/// <summary>シーンが必要とする共有描画基盤の準備範囲。通常シーンはFullを使う。</summary>
enum class RenderPreparation { SpritesOnly, Minimal, Full };

// 担当：エンジン存続中に使う3D・Sprite・CPU粒子の共通描画基盤を所有する。
// 関連：Framework（所有）、SceneManager（準備要求）、各CommonとSceneRenderCommands（描画命令の窓口）
// 主な入口：Initialize / Prepare / PrepareStep / IsReady
class RenderCommonResources {
public:
    RenderCommonResources();
    ~RenderCommonResources();
    RenderCommonResources(const RenderCommonResources&) = delete;
    RenderCommonResources& operator=(const RenderCommonResources&) = delete;

    /// <summary>GPU生成はまだ行わず、Framework所有のDirectX基盤を借用する。</summary>
    void Initialize(DirectXCommon* dxCommon);
    /// <summary>初回起動・直接遷移用。必要範囲の準備が終わるまで進める。</summary>
    void Prepare(RenderPreparation preparation);
    /// <summary>ロード画面用に一段ずつ準備する。準備済みなら生成せずtrueを返す。</summary>
    bool PrepareStep(RenderPreparation preparation);
    /// <summary>指定した準備範囲が使える状態か返す。</summary>
    bool IsReady(RenderPreparation preparation) const;
    /// <summary>共有基盤だけの準備進捗。シーン固有の素材読み込みは含めない。</summary>
    float GetPreparationProgress(RenderPreparation preparation) const;

    Object3DCommon* GetObject3DCommon() const { return object3dCommon_.get(); }
    SpriteCommon* GetSpriteCommon() const { return spriteCommon_.get(); }
    ParticleCommon* GetParticleCommon() const { return particleCommon_.get(); }
    /// <summary>シーン描画用の限定した命令窓口を借用する。Initialize後から終了前まで有効。</summary>
    SceneRenderCommands* GetSceneRenderCommands() const { return sceneRenderCommands_.get(); }

private:
    // 所有しない。Frameworkは全Sceneとこの管理器の終了後にDirectXを解放する。
    DirectXCommon* dxCommon_ = nullptr;
    // Sceneは借用するだけで、シーン遷移時には破棄しない。
    std::unique_ptr<Object3DCommon> object3dCommon_;
    std::unique_ptr<SpriteCommon> spriteCommon_; // 全シーンで共有するSprite描画基盤。
    std::unique_ptr<ParticleCommon> particleCommon_; // 全シーンで共有するCPU粒子描画基盤。
    // GPU資源は所有せず、各Commonより先に破棄する描画命令の窓口。
    std::unique_ptr<SceneRenderCommands> sceneRenderCommands_;
    // タイトル向けの軽量準備と全材質の準備は別状態。軽量完了を全体完了と誤認しない。
    bool minimalReady_ = false;
    bool fullReady_ = false; // 全3D材質を含む描画準備が完了したか。
    bool fullPreparationStarted_ = false; // 段階的な全描画準備を開始済みか。
};
} // namespace engine
