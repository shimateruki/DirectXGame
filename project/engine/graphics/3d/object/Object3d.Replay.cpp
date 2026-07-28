#include "Object3d.h"

#include <atomic>

namespace {
std::atomic<uint64_t> gNextReplayId{ 1 };
}

uint64_t Object3d::EnsureReplayId() {
    if (replayId_ == 0) {
        replayId_ = gNextReplayId.fetch_add(1, std::memory_order_relaxed);
    }
    return replayId_;
}

Object3d::ReplayState Object3d::CaptureReplayState() const {
    ReplayState state;
    state.scale = transform_.scale;
    state.rotation = transform_.rotate;
    state.translation = transform_.translate;
    state.quaternion = transform_.quaternion;
    state.quaternionMaster = transform_.isQuaternionMaster;

    state.visible = isVisible_;
    state.dead = isDead;
    state.collecting = isCollecting_;
    state.collectTimer = collectTimer_;
    state.animationTime = animationTime_;
    state.animationName = animName_;
    state.animationLoop = isAnimLoop_;
    state.animatorControllerPath = animatorControllerPath_;
    state.animatorSnapshot = animatorControllerRuntime_.CaptureSnapshot();

    state.hasParameter = param_.has_value();
    if (param_) {
        state.parameter = *param_;
    }
    state.collisionAttribute = GetCollisionAttribute();
    state.collisionMask = GetCollisionMask();

    state.modelName = GetModelName();
    state.texturePath = GetTexturePath();
    state.materialType = GetMaterialType();
    state.color = GetColor();
    state.emissive = GetEmissive();

    state.particleEmitterComponent = particleEmitterComponent_;
    state.meshEffectComponent = meshEffectComponent_;
    state.pathMoverComponent = pathMoverComponent_;
    state.gameplayLinkComponent = gameplayLinkComponent_;
    state.navAgentComponent = navAgentComponent_;

    state.replayRemoved = replayRemoved_;
    CaptureReplayCustomState(state.custom);
    return state;
}

void Object3d::RestoreReplayState(const ReplayState& state) {
    transform_.scale = state.scale;
    transform_.rotate = state.rotation;
    transform_.translate = state.translation;
    transform_.quaternion = state.quaternion;
    transform_.isQuaternionMaster = state.quaternionMaster;

    isVisible_ = state.visible;
    isDead = state.dead;
    isCollecting_ = state.collecting;
    collectTimer_ = state.collectTimer;
    animationTime_ = state.animationTime;
    animName_ = state.animationName;
    isAnimLoop_ = state.animationLoop;
    if (state.animatorControllerPath != animatorControllerPath_) {
        SetAnimatorController(state.animatorControllerPath);
    }
    if (state.animatorSnapshot.valid) {
        animatorControllerRuntime_.RestoreSnapshot(state.animatorSnapshot);
        ApplyAnimatorControllerPose(GetModel(), true);
    }

    if (state.hasParameter) {
        param_ = state.parameter;
    } else {
        param_.reset();
    }
    SetCollisionAttribute(state.collisionAttribute);
    SetCollisionMask(state.collisionMask);

    if (GetModelName() != state.modelName && !state.modelName.empty()) {
        SetModel(state.modelName);
    }
    if (GetTexturePath() != state.texturePath && !state.texturePath.empty()) {
        SetTexture(state.texturePath);
    }
    SetMaterialType(state.materialType);
    SetColor(state.color);
    SetEmissive(state.emissive);

    const bool particleAssetChanged =
        particleEmitterComponent_.has_value() != state.particleEmitterComponent.has_value() ||
        (particleEmitterComponent_ && state.particleEmitterComponent &&
            (particleEmitterComponent_->GetCpuParticle() != state.particleEmitterComponent->GetCpuParticle() ||
             particleEmitterComponent_->GetGpuParticle() != state.particleEmitterComponent->GetGpuParticle()));
    const bool meshEffectChanged =
        meshEffectComponent_.has_value() != state.meshEffectComponent.has_value() ||
        (meshEffectComponent_ && state.meshEffectComponent &&
            (meshEffectComponent_->GetPrimaryEffect() != state.meshEffectComponent->GetPrimaryEffect() ||
             meshEffectComponent_->GetSecondaryEffect() != state.meshEffectComponent->GetSecondaryEffect()));

    particleEmitterComponent_ = state.particleEmitterComponent;
    meshEffectComponent_ = state.meshEffectComponent;
    pathMoverComponent_ = state.pathMoverComponent;
    gameplayLinkComponent_ = state.gameplayLinkComponent;
    navAgentComponent_ = state.navAgentComponent;

    // Assetが変わった場合だけ実行用Instanceを破棄します。
    // 同じフレームを復元するたびにEffectやGhost Pathを再生成しないようにします。
    if (particleAssetChanged) {
        gpuEmitter_.reset();
    }
    if (meshEffectChanged) {
        currentMeshEffect1_.clear();
        currentMeshEffect2_.clear();
        attachedEffects1_.clear();
        attachedEffects2_.clear();
    }

    replayRemoved_ = state.replayRemoved;
    RestoreReplayCustomState(state.custom);
    UpdateLocalMatrix();
    UpdateWorldMatrix();
}

void Object3d::CaptureReplayCustomState(json& state) const {
    state = json::object();
}

void Object3d::RestoreReplayCustomState(const json& state) {
    (void)state;
}
