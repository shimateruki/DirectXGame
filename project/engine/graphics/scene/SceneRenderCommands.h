#pragma once
#include <cstdint>

class DirectXCommon;
class Camera;
class GpuParticleManager;

namespace engine {
class SpriteCommon;

// 担当：シーン描画で必要なGPU操作を、初期化済みDirectX基盤へ委譲する。
// 関連：RenderCommonResourcesが所有し、BaseSceneが描画対象と実行順を決める。
// 主な入口：BindSprites / CopySceneColor / BeginDepthRead / EndDepthRead / RecordGpuParticles
class SceneRenderCommands {
public:
    /// <summary>GPU資源を生成せず、所有者より長く生存するDirectX基盤を借用する。</summary>
    explicit SceneRenderCommands(DirectXCommon& dxCommon);
    SceneRenderCommands(const SceneRenderCommands&) = delete;
    SceneRenderCommands& operator=(const SceneRenderCommands&) = delete;

    /// <summary>呼び出した時点のコマンドリストへ既存のSprite描画設定を適用する。</summary>
    void BindSprites(SpriteCommon& sprites);
    /// <summary>本編の画面色・深度を操作してはいけないカメラプレビュー描画中ならtrue。</summary>
    bool IsCameraPreviewRendering() const;
    /// <summary>現在までに描いた画面色をコピーする。コピー要否と回数は呼び出し側が決める。</summary>
    void CopySceneColor();
    /// <summary>深度を画像参照できる読み取り状態へ切り替える。EndDepthReadと必ず対で呼ぶ。</summary>
    void BeginDepthRead();
    /// <summary>深度の読み取り区間を終え、通常の深度書き込み状態へ戻す。</summary>
    void EndDepthRead();
    /// <summary>本編の深度画像を参照するSRV番号を返す。GPU資源の所有権は渡さない。</summary>
    uint32_t GetDepthSrvHandle() const;
    /// <summary>最後にコピーした本編の画面色を参照するSRV番号を返す。</summary>
    uint32_t GetSceneColorSrvHandle() const;
    /// <summary>指定カメラとテクスチャ番号で粒子の描画命令だけを記録する。深度切替と背景コピーは行わない。</summary>
    void RecordGpuParticles(GpuParticleManager& particles, const Camera& camera, uint32_t textureHandle);

private:
    // 非所有参照。コマンドリストは保持せず、各命令の実行時に現在のものを取得する。
    DirectXCommon& dxCommon_;
};
} // namespace engine
