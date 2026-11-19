#include "RenderCommonResources.h"
#include "Object3DCommon.h"
#include "SpriteCommon.h"
#include "ParticleCommon.h"
#include "engine/graphics/scene/SceneRenderCommands.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace engine {
RenderCommonResources::RenderCommonResources() = default;
RenderCommonResources::~RenderCommonResources() = default;

void RenderCommonResources::Initialize(engine::DirectXCommon* dxCommon) {
    // Deviceより長く生存しない。再初期化で既存PSOを黙って差し替える使い方は拒否する。
    if (!dxCommon || dxCommon_) {
        throw std::logic_error("RenderCommonResources requires one valid DirectX initialization");
    }
    sceneRenderCommands_ = std::make_unique<SceneRenderCommands>(*dxCommon);
    dxCommon_ = dxCommon;
}

// Spriteは全経路で必須。軽量・全体準備ではCPU粒子と、要求された範囲の3Dパイプラインの完了も待つ。
bool RenderCommonResources::IsReady(RenderPreparation preparation) const {
    if (!spriteCommon_) return false;
    if (preparation == RenderPreparation::SpritesOnly) return true;
    return particleCommon_ && (preparation == RenderPreparation::Minimal ? minimalReady_ : fullReady_);
}

void RenderCommonResources::Prepare(RenderPreparation preparation) {
    // 同期で初期化する入口も、ロード画面と同じ準備処理を再利用する。
    while (!PrepareStep(preparation)) {}
}

bool RenderCommonResources::PrepareStep(RenderPreparation preparation) {
    assert(dxCommon_);
    if (IsReady(preparation)) return true;

    // どの準備範囲でもSpriteを先に用意し、SpritesOnlyならこの段階で完了とします。
    if (!spriteCommon_) {
        auto common = std::make_unique<SpriteCommon>();
        common->Initialize(dxCommon_);
        spriteCommon_ = std::move(common);
        return preparation == RenderPreparation::SpritesOnly;
    }
    if (preparation == RenderPreparation::SpritesOnly) return true;

    if (!object3dCommon_) object3dCommon_ = std::make_unique<Object3DCommon>();
    if (preparation == RenderPreparation::Minimal) {
        if (!minimalReady_) {
            // 既存の起動時軽量経路を利用し、不要な材質の一括生成を避ける。
            object3dCommon_->InitializeForTitle(dxCommon_);
            minimalReady_ = true;
            // 全体準備の途中で軽量経路へ切り替わった場合も、次回は不足分を再走査する。
            fullPreparationStarted_ = false;
            return false;
        }
    } else if (!fullReady_) {
        if (!fullPreparationStarted_) {
            object3dCommon_->BeginInitialize(dxCommon_);
            fullPreparationStarted_ = true;
        }
        if (!object3dCommon_->InitializeStep()) return false;
        fullReady_ = true;
        minimalReady_ = true;
        return false;
    }

    // 軽量・全体のどちらでも3D側の要求範囲を満たしてから、共有のCPU粒子パイプラインを用意します。
    if (!particleCommon_) {
        auto common = std::make_unique<ParticleCommon>();
        common->Initialize(dxCommon_);
        particleCommon_ = std::move(common);
    }
    return IsReady(preparation);
}

float RenderCommonResources::GetPreparationProgress(RenderPreparation preparation) const {
    if (IsReady(preparation)) return 1.0f;
    if (preparation == RenderPreparation::SpritesOnly) return 0.0f;
    const float objectProgress = preparation == RenderPreparation::Minimal
        ? (minimalReady_ ? 1.0f : 0.0f)
        : (fullReady_ ? 1.0f : (fullPreparationStarted_ ? object3dCommon_->GetInitializationProgress() : 0.0f));
    // Sprite、3D、CPU粒子の3枠を等分します。処理時間の割合ではなく準備段階の目安です。
    return std::clamp(((spriteCommon_ ? 1.0f : 0.0f) + objectProgress +
                       (particleCommon_ ? 1.0f : 0.0f)) / 3.0f, 0.0f, 1.0f);
}
} // namespace engine
