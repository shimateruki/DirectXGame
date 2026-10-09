#include "game/scene/gameplay/GamePlayScene.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "game/system/CrownFinaleTuning.h"
#include "CameraManager.h"
#include "InputManager.h"
#include "SceneManager.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include <algorithm>

namespace {
constexpr uint32_t kCrownMovieHiddenReason = 1u << 8;
}

bool GamePlayScene::BeginCrownMoviePreview(bool defeat) {
    if (crownMoviePreview_.active) return true;
    if (!player_ || crownFinale_.prepared || !GetSceneManager() || GetSceneManager()->IsPlaying()) return false;
    EnemyFalseKingSlime* sourceBoss = nullptr;
    bool hasFloor = false;
    for (const auto& object : GetObjects()) {
        if (object->IsEditorInternal() || object->IsReplayRemoved()) continue;
        if (auto* boss = dynamic_cast<EnemyFalseKingSlime*>(object.get())) sourceBoss = boss;
        hasFloor |= object->GetName().find("Stage3_HighCrown_ArenaFloor_") != std::string::npos;
    }
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!sourceBoss || !hasFloor || !camera) return false;
    auto& p = crownMoviePreview_;
    p.defeat = defeat;
    p.originalPlayer = player_;
    p.previousCameraOverride = CameraManager::GetInstance()->GetActiveCameraOverride();
    p.camera = std::make_unique<Camera>(); p.camera->Initialize();
    p.camera->SetClipRange(camera->GetNearClip(), camera->GetFarClip());
    p.camera->SetInputEnabled(false);
    CameraEditor::GetInstance()->SetEditorStateSaveBlocker(1u << 3, true);
    p.eye = camera->GetEye(); p.target = camera->GetTargetPoint(); p.rotation = camera->GetRotation();
    p.followMode = camera->GetFollowMode(); p.follow = camera->GetFollowTarget(); p.fov = camera->GetFovY();
    std::vector<std::unique_ptr<Object3d>> copies;
    EnemyFalseKingSlime* previewBoss = nullptr;
    // 生成前に走査を終え、元のScene保存値から表示だけを一時的に分離します。
    for (const auto& object : GetObjects()) {
        if (object->IsEditorInternal() || object->IsReplayRemoved() || object->IsCameraObject()) continue;
        p.hiddenObjects.push_back(object.get());
        if (object.get() == player_) continue;
        if (object.get() != sourceBoss && object->GetName().rfind("Stage3_HighCrown_", 0) != 0) continue;
        auto copy = object->Clone();
        if (!copy) continue;
        copy->ClearEditorHiddenReasons();
        copy->SetEditorInternal(true); copy->SetIsLocked(true);
        copy->SetEventID(-1); copy->SetTargetID(-1);
        copy->SetCollisionAttribute(0); copy->SetCollisionMask(0);
        if (object.get() == sourceBoss) {
            previewBoss = dynamic_cast<EnemyFalseKingSlime*>(copy.get());
            copy->SetIsVisible(true);
        }
        p.objects.push_back(copy.get()); copies.push_back(std::move(copy));
    }
    if (!previewBoss) {
        CameraEditor::GetInstance()->SetEditorStateSaveBlocker(1u << 3, false);
        p = CrownMoviePreviewState{}; return false;
    }
    auto player = std::make_unique<Player>();
    player->Initialize(GetObject3dCommon(), InputManager::GetInstance(), GetParticleSystem(), GetSpriteCommon());
    // Initializeだけでは本体モデルが設定されません。実Playerの基本形を明示します。
    player->SetModel("Characters/slimeBody"); player->SetColor({1.0f,1.0f,1.0f,1.0f});
    player->SetName("__Editor_CrownMovie_Player"); player->SetClassName("Player");
    player->SetEditorInternal(true); player->SetIsLocked(true);
    player->SetColliderConfig(player_->GetColliderConfig());
    player->SetCollisionAttribute(0); player->SetCollisionMask(0);
    player->SetMaterialType(25); player->SetEnableLighting(true); player->SetIsControlActive(false);
    player->SetScale(player_->GetScale()); player->SetGrounded(true);
    // 登場操作を毎回やり直さず、第二形態の中央位置で演出を開始します。
    const auto& tuning = CrownFinaleTuning::Get();
    const Vector3 bossPosition = sourceBoss->GetTranslate() + Vector3{0.0f, 0.0f, tuning.sealCenterOffsetZ};
    previewBoss->SetTranslate(bossPosition);
    previewBoss->SetRotation(sourceBoss->GetRotation()); previewBoss->UpdateWorldMatrix(true);
    player->SetTranslate({0.0f, 0.82f, 12.0f}); player->SetMoveYaw(3.14159265f);
    player->UpdateWorldMatrix(true);
    player_ = player.get(); p.objects.push_back(player.get()); copies.push_back(std::move(player));
    p.active = true;
    CameraManager::GetInstance()->SetActiveCamera(p.camera.get());
    for (Object3d* object : p.hiddenObjects) object->SetEditorHiddenReason(kCrownMovieHiddenReason, true);
    // Editor更新の安全な境界で登録し、開始時の床・壁の走査から参照可能にします。
    for (auto& copy : copies) AddObject(std::move(copy));
    objectManager_->ApplyPendingChanges();
    if (!StartCrownFinale(previewBoss)) { EndCrownMoviePreview(); return false; }
    if (defeat) {
        // 継承完了時の実際の闘技場を用意し、同じ配置・能力で撃破を確認します。
        crownFinale_.time=crownFinale_.duration;
        UpdateCrownFinale(0);FinishCrownFinale(true);
        CameraManager::GetInstance()->SetActiveCamera(p.camera.get());
        if (!StartCrownDefeat(previewBoss)) { EndCrownMoviePreview();return false; }
    }
    // 最終闘技場も保存対象へ混ざらない、プレビュー専用の複製です。
    StepCrownMoviePreview(0.0f);
    return true;
}

void GamePlayScene::RewindCrownMoviePreview() {
    if (crownMoviePreview_.active && crownMoviePreview_.defeat) {
        crownDefeat_.time=0;UpdateCrownDefeat(0);return;
    }
    if (!crownMoviePreview_.active || !crownFinale_.active) return;
    auto& s = crownFinale_;
    player_->ClearCrownPower(); player_->EndCinematicPullVisual();
    player_->SetModel(s.playerModel); player_->SetMaterialType(s.playerMaterial); player_->SetColor(s.playerColor);
    player_->GetMeshRenderer()->GetMaterialData()->portalClipEnabled = 0.0f;
    player_->BeginCinematicLock(); player_->SetScale(s.playerScale); player_->SetMoveYaw(s.playerYaw);
    auto rotation = player_->GetRotation(); rotation.x = rotation.z = 0.0f; player_->SetRotation(rotation);
    player_->SetTranslate(s.start); player_->SetVelocity({});
    s.time = 0.0f; s.duration = CrownFinaleTuning::Get().movieDuration;
    s.bodyFormed = s.crownStarted = s.wingsStarted = false;
    s.granted = s.impacted = s.fragmentBroken = s.fragmentLanded = s.pullStarted = s.launched = s.landed = false;
    for (const auto& pose : s.originalObjects) {
        pose.object->SetTranslate(pose.position); pose.object->SetRotation(pose.rotation);
        pose.object->SetCollisionAttribute(pose.attribute); pose.object->SetCollisionMask(pose.mask);
        pose.object->SetIsVisible(pose.visible);
    }
    StepCrownMoviePreview(0.0f);
}

void GamePlayScene::StepCrownMoviePreview(float deltaTime) {
    if (crownMoviePreview_.active && crownMoviePreview_.defeat) {
        UpdateCrownDefeat(deltaTime);
        for(Object3d* object:crownMoviePreview_.objects)object->UpdateWorldMatrix(true);
        for(Object3d* object:crownFinale_.generatedObjects)object->UpdateWorldMatrix(true);
        return;
    }
    if (!crownMoviePreview_.active || !crownFinale_.active) return;
    UpdateCrownFinale(deltaTime);
    // Object3d::Updateは描画定数を確定しません。カメラの確定後に全モデルへ転送します。
    for (Object3d* object : crownMoviePreview_.objects) object->UpdateWorldMatrix(true);
    for (Object3d* object : crownFinale_.generatedObjects) object->UpdateWorldMatrix(true);
}

void GamePlayScene::EndCrownMoviePreview() {
    auto& p = crownMoviePreview_;
    if (!p.active) return;
    ResetCrownDefeat();
    ResetCrownFinaleWorld();
    objectManager_->ApplyPendingChanges();
    player_ = p.originalPlayer;
    for (Object3d* object : p.hiddenObjects) object->SetEditorHiddenReason(kCrownMovieHiddenReason, false);
    for (Object3d* object : p.objects) {
        object->SetIsVisible(false); object->SetReplayRemoved(true);
        RequestRemoveObject(object);
    }
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetFollowTarget(p.follow); camera->SetFollowMode(p.followMode);
        camera->SetEye(p.eye); camera->SetTarget(p.target); camera->SetRotation(p.rotation);
        if (p.followMode == Camera::FollowMode::kFixedPoint) camera->ConfigFixedPoint(p.eye, p.rotation);
        camera->SetFovY(p.fov); camera->SetInputEnabled(true); camera->Update();
    }
    CameraManager::GetInstance()->SetActiveCamera(p.previousCameraOverride);
    CameraEditor::GetInstance()->SetEditorStateSaveBlocker(1u << 3, false);
    objectManager_->ApplyPendingChanges();
    p = CrownMoviePreviewState{};
}
