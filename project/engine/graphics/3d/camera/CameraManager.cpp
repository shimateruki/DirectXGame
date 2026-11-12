#include "CameraManager.h"
#include <cmath>
#include "SceneManager.h"
#include "BaseScene.h"

CameraManager* CameraManager::GetInstance() {
    static CameraManager instance;
    return &instance;
}

// メインカメラを作り直します。入力先の設定はこの後に行います。
void CameraManager::Initialize() {
    ResetCameraRegions();
    mainCamera_ = std::make_unique<Camera>();
    mainCamera_->Initialize();
    activeCamera_ = nullptr;
    presentationPreviousActiveCamera_ = nullptr;
    mainCameraPresentationActive_ = false;
}

bool CameraManager::BeginMainCameraPresentation() {
    if (!mainCamera_ || mainCameraPresentationActive_) {
        return false;
    }

    presentationPreviousActiveCamera_ = activeCamera_;
    presentationPreviousInputEnabled_ = mainCamera_->IsInputEnabled();
    mainCameraPresentationActive_ = true;
    mainCamera_->SetInputEnabled(false);
    mainCamera_->SetFollowTarget(nullptr);
    mainCamera_->SetLockOnTarget(nullptr);
    mainCamera_->SetFollowMode(Camera::FollowMode::FixedPoint);
    activeCamera_ = mainCamera_.get();
    return true;
}

void CameraManager::PrepareMainCameraReturn(engine::Object3D* followTarget, const engine::Vector3& eye,
                                            const engine::Vector3& target) {
    if (!mainCameraPresentationActive_ || !mainCamera_) {
        return;
    }

    // 視線方向だけを回転へ反映し、通常カメラの追従距離は変更しない。
    const engine::Vector3 direction = target - eye;
    const float horizontal = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    const engine::Vector3 rotation = { std::atan2(-direction.y, horizontal),
                                       std::atan2(direction.x, direction.z), 0.0f };
    mainCamera_->SetEye(eye);
    mainCamera_->SetTarget(target);
    mainCamera_->SetRotation(rotation);
    mainCamera_->SetFollowTarget(followTarget);
    mainCamera_->SetFollowMode(Camera::FollowMode::Aimable);
}

void CameraManager::SnapMainCameraReturn(float distance, float height, float pitch) {
    if (mainCameraPresentationActive_ && mainCamera_) {
        mainCamera_->SnapToThirdPerson(distance, height, pitch);
    }
}

void CameraManager::EndMainCameraPresentation() {
    if (!mainCameraPresentationActive_) {
        return;
    }
    if (mainCamera_) {
        mainCamera_->SetInputEnabled(presentationPreviousInputEnabled_);
    }
    activeCamera_ = presentationPreviousActiveCamera_;
    presentationPreviousActiveCamera_ = nullptr;
    mainCameraPresentationActive_ = false;
}

void CameraManager::Update(float deltaTime) {
    if (mainCamera_) {
        const auto* scene = SceneManager::GetInstance()->GetCurrentScene();
        if (scene) {
            auto* subject = mainCamera_->GetFollowTarget();
            UpdateCameraRegions(scene->GetObjects(), subject, deltaTime,
                SceneManager::GetInstance()->IsPlaying() && subject && mainCamera_->IsInputEnabled(),
                mainCamera_->ConsumeManualLookInput());
        } else ResetCameraRegions();
        mainCamera_->Update(deltaTime);
        UpdateVisibility(deltaTime);
    }
}

void CameraManager::SetInputManager(engine::InputManager* inputManager) {
    if (mainCamera_) {
        mainCamera_->SetInputManager(inputManager);
    }
}

// 演出は現在のアクティブカメラへ送ります。カメラがなければ要求を破棄します。
void CameraManager::PlayShake(float duration, float amplitude, float frequency, const engine::Vector3& axisWeight) {
    if (Camera* camera = GetActiveCamera()) {
        camera->StartShake(duration, amplitude, frequency, axisWeight);
    }
}

void CameraManager::PlayFovPulse(float duration, float amountRadians, float attackRatio) {
    if (Camera* camera = GetActiveCamera()) {
        camera->StartFovPulse(duration, amountRadians, attackRatio);
    }
}

void CameraManager::ClearPresentationLayers() {
    if (Camera* camera = GetActiveCamera()) {
        camera->ClearPresentationLayers();
    }
}


namespace {
std::string RegionIdentity(const engine::Object3D& object) {
    // 名前変更と同名カメラに影響されない。未保存Objectには一時IDを使う。
    return object.GetPersistentGuid().empty()
        ? std::to_string(reinterpret_cast<uintptr_t>(&object)) : object.GetPersistentGuid();
}
}

bool CameraManager::ResolveSceneCameraPose(const engine::Object3D& object,
    const std::vector<std::unique_ptr<engine::Object3D>>& objects, engine::Vector3& eye, engine::Vector3& target, bool requireTarget) {
    if (!object.IsCameraObject()) return false;
    const auto& settings = object.GetSceneCameraSettings();
    auto find = [&](const std::string& name) -> const engine::Object3D* {
        if (name.empty()) return nullptr;
        for (const auto& candidate : objects)
            if (candidate && candidate->GetName() == name) return candidate.get();
        return nullptr;
    };
    const auto* origin = &object;
    if (settings.eyeSource != SceneCameraEyeSource::CameraObject) {
        origin = find(settings.eyeObjectName);
        if (!origin) return false;
    }
    if (settings.eyeSource == SceneCameraEyeSource::SceneObjectRelativeToReference) {
        const auto* reference = find(settings.eyeReferenceObjectName);
        if (!reference) return false;
        eye = Camera::ResolveRelativeEye(*origin, *reference, settings.eyeOffset);
    } else {
        eye = (origin->GetParent() ? origin->GetWorldPosition() : origin->GetTranslate()) + settings.eyeOffset;
    }
    auto forward = engine::Math::TransformNormal({0,0,1}, origin->GetWorldMatrix());
    const float length = engine::Math::Length(forward);
    forward = length > 0.0001f ? forward * (1.0f / length) : engine::Vector3{0,0,1};
    if (settings.targetMode == SceneCameraTargetMode::FixedPoint) {
        target = settings.fixedTarget;
    } else if (settings.targetMode == SceneCameraTargetMode::SceneObject) {
        const auto* targetObject = find(settings.targetObjectName);
        if (!targetObject && requireTarget) return false;
        target = targetObject ? targetObject->GetWorldPosition() + settings.targetOffset
            : eye + forward * settings.forwardDistance + settings.targetOffset;
    } else {
        target = eye + forward * settings.forwardDistance + settings.targetOffset;
    }
    return engine::Math::Length(target - eye) > 0.001f;
}

void CameraManager::ResetCameraRegions() {
    const auto serial = visibilityFrame_.serial;
    visibilityFrame_ = {};
    visibilityFrame_.serial = serial;
    // 他の演出が開始済みなら、そちらのOverrideを終了しない。
    if (mainCamera_ && ownsRegionOverride_ && mainCamera_->GetOverrideRevision() == regionOverrideRevision_)
        mainCamera_->EndOverride(0.0f);
    if (mainCamera_) mainCamera_->ClearRegionProjection();
    ownsRegionOverride_ = false;
    activeRegionId_.clear(); pendingRegionId_.clear(); activeRegionName_.clear();
    pendingRegionTime_ = manualRegionDelay_ = 0.0f;
    routeProgressId_.clear(); routeProgress_ = 0;
    regionBarrierCenter_ = {}; regionBarrierParameters_ = {};
    regionStatus_ = "範囲カメラなし";
}

void CameraManager::UpdateCameraRegions(const std::vector<std::unique_ptr<engine::Object3D>>& objects,
    engine::Object3D* subject, float deltaTime, bool enabled, bool manualInput) {
    if (!mainCamera_) return;
    // 区画外や演出中へ、直前の戦闘用設定を持ち越さない。
    regionBarrierCenter_ = {}; regionBarrierParameters_ = {};
    const float frameDeltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
    auto& camera = *mainCamera_;
    if (ownsRegionOverride_ && camera.GetOverrideRevision() != regionOverrideRevision_) {
        // 所有権を失ったら演出を尊重し、保存していたObjectのIDだけを破棄する。
        ownsRegionOverride_ = false; activeRegionId_.clear(); activeRegionName_.clear();
        pendingRegionId_.clear(); pendingRegionTime_ = 0.0f;
    }
    if (!enabled || !subject) { ResetCameraRegions(); return; }
    auto exitRegion = [&] {
        if (ownsRegionOverride_ && camera.IsOverridden()) camera.EndOverride(regionExitDuration_);
        activeRegionId_.clear(); activeRegionName_.clear();
    };
    if (manualInput && !camera.IsGuidedGameplay()) {
        manualRegionDelay_ = camera.GetVisibilitySettings().manualReturnDelay;
        if (ownsRegionOverride_) {
            const auto manualRotation = camera.GetRotation();
            camera.ResumeManualView(); camera.SetRotation(manualRotation);
            camera.EndOverride(0.0f); camera.ClearRegionProjection();
            ownsRegionOverride_ = false;
        }
        activeRegionId_.clear(); activeRegionName_.clear(); pendingRegionId_.clear(); pendingRegionTime_ = 0.0f;
    } else manualRegionDelay_ = (std::max)(0.0f, manualRegionDelay_ - frameDeltaTime);

    if (mainCameraPresentationActive_ || activeCamera_ ||
        (!ownsRegionOverride_ && (camera.IsOverridden() || camera.GetOverrideWeight() > 0.0f))) {
        regionStatus_ = "演出カメラを優先"; return;
    }
    // 誘導モードはロックオン中も配置構図を保ち、自由視点だけ追従へ切り替えます。
    if (manualRegionDelay_ > 0.0f || camera.IsAimCameraRequested() || (camera.GetFollowMode() == Camera::FollowMode::LockOn && !camera.IsGuidedGameplay()) ||
        camera.GetFollowMode() == Camera::FollowMode::FirstPerson) {
        exitRegion(); pendingRegionId_.clear(); pendingRegionTime_ = 0.0f;
        regionStatus_ = "手動・照準・ロックオンを優先"; return;
    }

    // 見た目の高さ保持と区画選択を揃える。別の対象を明示した呼び出しは実座標を使う。
    const auto regionPosition = camera.GetFollowTarget() == subject
        ? camera.UpdateFollowReferencePosition() : subject->GetWorldPosition();
    const engine::Object3D* best = nullptr;
    engine::Vector3 bestEye{}, bestTarget{};
    for (const auto& object : objects) {
        if (!object || !object->IsCameraObject() || !object->GetIsVisible()) continue;
        const auto& settings = object->GetSceneCameraSettings();
        const auto id = RegionIdentity(*object);
        if (!settings.enabled || !settings.region.enabled ||
            !settings.region.Contains(regionPosition, id == activeRegionId_)) continue;
        engine::Vector3 eye{}, target{};
        if (!ResolveSceneCameraPose(*object, objects, eye, target, true)) continue;
        const bool better = !best || settings.region.priority > best->GetSceneCameraSettings().region.priority ||
            (settings.region.priority == best->GetSceneCameraSettings().region.priority &&
             (id == activeRegionId_ || (RegionIdentity(*best) != activeRegionId_ && id < RegionIdentity(*best))));
        if (better) { best = object.get(); bestEye = eye; bestTarget = target; }
    }
    const std::string bestId = best ? RegionIdentity(*best) : "";
    float bestFov = best ? best->GetSceneCameraSettings().fovY : camera.GetFovY();
    bool routeApplied = false;
    if (best && camera.IsGuidedGameplay() && best->GetSceneCameraSettings().region.routeEnabled) {
        const auto& route = best->GetSceneCameraSettings().region;
        const engine::Object3D* end = nullptr;
        for (const auto& candidate : objects)
            if (candidate && candidate.get() != best && candidate->IsCameraObject() &&
                candidate->GetPersistentGuid() == route.endCameraGuid) { end = candidate.get(); break; }
        auto axis = route.pathEnd - route.pathStart; axis.y = 0;
        const float lengthSquared = axis.x*axis.x + axis.z*axis.z;
        engine::Vector3 endEye{}, endTarget{};
        if (end && end->GetSceneCameraSettings().enabled && lengthSquared > .01f &&
            ResolveSceneCameraPose(*end, objects, endEye, endTarget, true)) {
            const float length = std::sqrt(lengthSquared);
            const auto delta = subject->GetWorldPosition() - route.pathStart;
            const float progress = std::clamp((delta.x*axis.x+delta.z*axis.z)/lengthSquared,0.0f,1.0f);
            if (routeProgressId_ != bestId) { routeProgressId_ = bestId; routeProgress_ = progress; }
            // 短い往復では道上の基準点を動かさない。余白を越えた分を時間補間する。
            const float slack = camera.GetGuidedDeadZoneWidth()*engine::Math::Length(bestEye-bestTarget)*std::tan(bestFov*.5f)/length;
            const float goal = std::clamp(routeProgress_,(std::max)(0.0f,progress-slack),(std::min)(1.0f,progress+slack));
            routeProgress_ += (goal-routeProgress_)*(1.0f-std::exp(-camera.GetGuidedFramingResponse()*frameDeltaTime));
            const float lookProgress = std::clamp(routeProgress_+camera.GetGuidedLookAhead()/length,0.0f,1.0f);
            bestEye = bestEye + (endEye-bestEye)*routeProgress_;
            bestTarget = bestTarget + (endTarget-bestTarget)*lookProgress;
            bestFov += (end->GetSceneCameraSettings().fovY-bestFov)*routeProgress_;
            routeApplied = true;
        }
    }
    if (!routeApplied) routeProgressId_.clear();
    if (bestId != pendingRegionId_) { pendingRegionId_ = bestId; pendingRegionTime_ = 0.0f; }
    pendingRegionTime_ += frameDeltaTime;
    const float delay = best ? best->GetSceneCameraSettings().region.activationDelay : 0.2f;
    if (bestId != activeRegionId_ && pendingRegionTime_ >= delay) {
        if (best) {
            const auto& settings = best->GetSceneCameraSettings();
            Camera::CameraOverrideParams params;
            params.duration = settings.blendInDuration;
            params.easing = static_cast<Camera::OverrideEasing>(settings.easing);
            params.fixedEyePos = bestEye; params.fixedTargetPos = bestTarget;
            params.trackTargetX = params.trackTargetZ = false;
            // 自機への追従だけは、通常カメラと同じジャンプ高さの保持を使う。
            params.trackTargetY = settings.targetMode == SceneCameraTargetMode::SceneObject &&
                settings.targetObjectName == subject->GetName();
            params.trackEyeY = settings.eyeSource != SceneCameraEyeSource::CameraObject &&
                settings.eyeObjectName == subject->GetName();
            params.eyeFollowMode = static_cast<Camera::OverrideFollowMode>(settings.eyeFollowMode);
            params.eyeFollowResponse = settings.eyeFollowResponse;
            params.targetFollowMode = static_cast<Camera::OverrideFollowMode>(settings.targetFollowMode);
            params.targetFollowResponse = settings.targetFollowResponse;
            camera.StartOverride(params);
            regionOverrideRevision_ = camera.GetOverrideRevision(); ownsRegionOverride_ = true;
            activeRegionId_ = bestId; activeRegionName_ = best->GetName();
            regionExitDuration_ = settings.blendOutDuration;
        } else exitRegion();
    }
    if (best && bestId == activeRegionId_ && ownsRegionOverride_) {
        const auto& settings = best->GetSceneCameraSettings();
        const auto& region = settings.region;
        if (region.clearForegroundBarriers) {
            regionBarrierCenter_ = {region.center.x, region.center.y, region.center.z, 1.0f};
            regionBarrierParameters_ = {region.barrierFloorHeight, region.barrierEdgeHeight, region.barrierOpacity, 1.0f};
        }
        camera.UpdateRegionPose(bestEye, bestTarget);
        camera.SetRegionProjection(bestFov, settings.nearClip, settings.farClip, subject);
    }
    if (ownsRegionOverride_ && !camera.IsOverridden() && camera.GetOverrideWeight() <= 0.0f) {
        camera.ClearRegionProjection(); ownsRegionOverride_ = false;
    }
    regionStatus_ = activeRegionName_.empty() ? "通常追従 / 範囲外または切替待ち" : "適用中: " + activeRegionName_;
}

void CameraManager::UpdateVisibility(float deltaTime) {
    ++visibilityFrame_.serial;
    visibilityFrame_.enabled = false;
    visibilityFrame_.parameters = {};
    visibilityFrame_.foregroundBarrierCenter = {};
    visibilityFrame_.foregroundBarrierParameters = {};
    auto* camera = GetActiveCamera();
    if (!camera || camera != mainCamera_.get() || mainCameraPresentationActive_ ||
        !SceneManager::GetInstance()->IsPlaying()) return;
    auto* subject = camera->GetFollowTarget();
    const auto& settings = camera->GetVisibilitySettings();
    if (!subject || !settings.enabled || (camera->IsOverridden() && !ownsRegionOverride_) || camera->GetFollowMode() == Camera::FollowMode::FirstPerson) return;
    // 物理境界の中心を使い、巨大なaimHeightや装飾のサイズに透過円を引きずらせない。
    const Aabb bounds = subject->GetAabb();
    const auto extent = bounds.max - bounds.min;
    const bool validBounds = extent.y > 0.01f && extent.y < 100.0f;
    const auto center = validBounds ? (bounds.min + bounds.max) * 0.5f
        : subject->GetWorldPosition() + engine::Vector3{0, 0.8f, 0};
    const float radius = (std::max)(settings.radius, validBounds ? extent.y * 0.65f : 0.8f);
    visibilityFrame_.centerRadius = {center.x, center.y, center.z, radius};
    visibilityFrame_.parameters = {1.0f - settings.minimumOpacity, settings.feather,
        validBounds ? bounds.min.y : subject->GetWorldPosition().y,
        validBounds ? (std::max)(0.15f, (std::max)(extent.x, extent.z) * 0.5f) : 0.3f};
    visibilityFrame_.deltaTime = std::isfinite(deltaTime) ? std::clamp(deltaTime, 0.0f, 0.1f) : 0.0f;
    visibilityFrame_.foregroundBarrierCenter = regionBarrierCenter_;
    visibilityFrame_.foregroundBarrierCenter.w *= camera->GetOverrideWeight();
    visibilityFrame_.foregroundBarrierParameters = regionBarrierParameters_;
    visibilityFrame_.enabled = true;
}

bool CameraManager::CanFadeObject(const engine::Object3D* object, const Camera* camera) const {
    if (!visibilityFrame_.enabled || !object || camera != mainCamera_.get() ||
        (activeCamera_ && activeCamera_ != camera)) return false;
    const bool explicitlyFade = object->GetCameraOcclusionMode() == engine::Object3D::CameraOcclusionMode::Fade;
    bool automatic = true;
    for (const auto* current = object; current; current = current->GetParent()) {
        const auto kind = current->GetClassName();
        // 自機や敵本体は透かさない。敵の子に付けたバリア等は個別指定で許可できる。
        if (current == camera->GetFollowTarget() || kind == "Player" || kind == "Item" || kind == "Camera" ||
            (current == object && kind == "Enemy") ||
            current->GetCameraOcclusionMode() == engine::Object3D::CameraOcclusionMode::KeepVisible) return false;
        if (kind == "Enemy" || kind == "EnemyVisualPart" || kind == "Effect") automatic = false;
    }
    return explicitlyFade || automatic;
}
