#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "GimmickEventReceiver.h"
#include "GimmickBossGate.h"
#include "CameraManager.h"
#include "PostEffect.h"
#include "SceneManager.h"
#include "VFXSequencer.h"
#include "GPUParticleManager.h"
#include "SpriteLayoutScaler.h"
#include "TextureManager.h"
#include "game/system/CrownFinaleTuning.h"
#include <algorithm>
#include <cmath>

namespace {
float CrownEase(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
Vector3 CrownLerp(const Vector3& a, const Vector3& b, float t) { return a + (b - a) * t; }
Vector3 CrownLook(const Vector3& eye, const Vector3& target) {
    const Vector3 d = target - eye;
    return { -std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)), std::atan2(d.x, d.z), 0.0f };
}
}

bool GamePlayScene::StartCrownFinale(EnemyFalseKingSlime* boss) {
    if (crownFinale_.active || crownFinale_.prepared || !boss || !player_ ||
        player_->isDead || player_->IsCinematicLocked() || isGoal_) return false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera) return false;
    // 必須の足場を確認してから生成。攻撃プレビュー等では脱出を開始しません。
    bool hasFloor = false, hasArena = false;
    for (const auto& object : GetObjects()) {
        hasFloor |= object->GetName().find("Stage3_HighCrown_ArenaFloor_") != std::string::npos;
        hasArena |= object->GetName() == "Stage3_HighCrown_ArenaVisual";
    }
    if (!hasFloor || !hasArena) return false;
    auto& s = crownFinale_;
    const auto& tuning = CrownFinaleTuning::Get();
    s.active = s.prepared = true;
    s.time = 0.0f; s.duration = tuning.movieDuration;
    s.offset = { tuning.destinationX, 0.0f, 0.0f };
    s.boss = boss; s.bossStart = boss->GetTranslate();
    s.bossScale = boss->GetScale(); s.bossRotation = boss->GetRotation();
    s.playerYaw = player_->GetMoveYaw();
    s.hadControl = player_->IsControlActive();
    s.eye = camera->GetEye(); s.target = camera->GetTargetPoint(); s.fov = camera->GetFovY();
    if (auto* params = PostEffect::GetInstance()->GetParams()) s.bars = params->cinemaBarHeight;
    player_->BeginCinematicLock();
    s.start = player_->GetTranslate();
    s.playerScale = player_->GetScale();
    for (const auto& object : GetObjects()) {
        const std::string& name = object->GetName();
        const bool floor = name.find("Stage3_HighCrown_ArenaFloor_") != std::string::npos;
        const bool arena = name == "Stage3_HighCrown_ArenaVisual";
        const bool wall = name.find("Stage3_HighCrown_Wall") != std::string::npos;
        const bool throne = name == "Stage3_HighCrown_Throne";
        const bool reward = name == "Stage3_HighCrown_VictoryCrown";
        const bool foundation = name == "Stage3_HighCrown_Background_Foundation";
        const bool approach = name.find("Stage3_HighCrown_Approach") != std::string::npos ||
            name == "Stage3_HighCrown_StairSupport" ||
            name == "Stage3_HighCrown_Background_EntranceCitadel" ||
            name.find("Stage3_HighCrown_Entrance") != std::string::npos ||
            name.find("Stage3_HighCrown_BossGate") != std::string::npos ||
            name.find("Stage3_HighCrown_DecorTower") != std::string::npos ||
            name == "Stage3_PreBoss_CopyMemoryStation";
        if (floor || arena || wall || throne || reward || foundation || approach) {
            s.originalObjects.push_back({ object.get(), object->GetTranslate(), object->GetRotation(),
                object->GetCollisionAttribute(), object->GetCollisionMask(), object->GetIsVisible() });
            if (auto* gate = dynamic_cast<GimmickBossGate*>(object.get()))
                gate->SetCinematicOverride(true);
        }
        if (floor || arena || foundation) {
            auto copy = object->Clone();
            if (!copy) continue;
            copy->SetName("CrownFinalArena_" + name);
            copy->SetEventID(-1); copy->SetTargetID(-1);
            copy->SetTranslate(object->GetTranslate() + s.offset);
            if (arena) {
                copy->SetColor({ 0.72f, 0.84f, 1.0f, 1.0f });
                copy->SetEmissive(1.1f);
            }
            copy->UpdateWorldMatrix(true);
            s.generatedObjects.push_back(copy.get());
            AddObject(std::move(copy));
        }
    }
    s.fragment = std::make_unique<Object3d>();
    s.fragment->Initialize(GetObject3dCommon());
    s.fragment->SetModel("Characters/false_king_slime/false_king_crown");
    s.fragment->SetMaterialType(0);
    s.fragment->SetBlendMode(BlendMode::kNone);
    s.fragment->SetColliderType(ColliderType::kNone);
    s.fragment->SetCollisionAttribute(0); s.fragment->SetCollisionMask(0);
    s.fragment->SetScale({ 0.18f, 0.18f, 0.18f });
    s.fragment->SetColor({ 1.0f, 0.88f, 0.40f, 1.0f });
    s.fragment->SetEmissive(2.0f);
    s.fragment->SetTranslate(s.bossStart + Vector3{ 0.0f, 5.0f, 0.0f });
    s.fragment->UpdateWorldMatrix(true);
    // 戦闘用の王威・王翼と同じ立体モデルを使い、力の継承を形でもつなげます。
    auto makeVisual = [&](const char* model) {
        auto visual = std::make_unique<Object3d>();
        visual->Initialize(GetObject3dCommon()); visual->SetModel(model);
        visual->SetMaterialType(0); visual->SetBlendMode(BlendMode::kNone);
        visual->SetColliderType(ColliderType::kNone);
        visual->SetCollisionAttribute(0); visual->SetCollisionMask(0);
        visual->SetEmissive(1.5f); visual->SetIsVisible(false);
        return visual;
    };
    s.shockwave = makeVisual("Effects/false_king_royal_wave");
    s.wings = makeVisual("Effects/false_king_rush_wings");
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr); camera->SetLockOnTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    return true;
}

void GamePlayScene::UpdateCrownFinale(float dt) {
    auto& s = crownFinale_;
    if (!s.active) return;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera || !player_ || !s.boss || player_->isDead) { ResetCrownFinaleWorld(); return; }
    s.time += std::max(0.0f, dt);
    const float t = std::clamp(s.time / s.duration, 0.0f, 1.0f);
    const auto& tuning = CrownFinaleTuning::Get();
    const Vector3 destination = s.offset + Vector3{ 0.0f, 0.82f, 12.0f };
    const float flightYaw = std::atan2(destination.x - s.start.x, destination.z - s.start.z);
    // 最後の封印への衝突から、制御を失った王冠の暴走へつなげます。
    if (t < 0.12f) {
        const float charge = CrownEase(t / 0.10f);
        const float squash = 0.22f + std::sin(t * 260.0f) * charge * 0.055f;
        s.boss->SetScale({ s.bossScale.x * (1.0f + squash * 0.65f),
            s.bossScale.y * (1.0f - squash), s.bossScale.z * (1.0f + squash * 0.65f) });
        s.boss->SetTranslate(s.bossStart);
    } else s.boss->SetScale(s.bossScale);
    if (t >= 0.10f && !s.impacted) {
        s.impacted = true;
        VFXSequencer::PlayOneShot("false_king_phase_shift_cue", s.bossStart, { 2.2f, 2.2f, 2.2f });
    }
    const float shock = std::clamp((t - 0.10f) / 0.18f, 0.0f, 1.0f);
    s.shockwave->SetIsVisible(t >= 0.10f && t < 0.28f);
    s.shockwave->SetTranslate(s.bossStart + Vector3{ 0.0f, 0.12f, 0.0f });
    s.shockwave->SetScale({ 1.0f + shock * 32.0f, 0.6f * (1.0f - shock) + 0.02f, 1.0f + shock * 32.0f });
    s.fragment->SetIsVisible(t >= 0.10f && !s.granted);
    // フレーム落ちで受け取りカットを跨いでも、飛翔前に継承を一度だけ確定します。
    if (t >= 0.42f && !s.granted) {
        player_->SetScale(s.playerScale);
        s.granted = player_->ApplyStoredCopy("Crown", true);
        if (!s.granted) { ResetCrownFinaleWorld(); return; }
        s.fragment->SetIsVisible(false);
        VFXSequencer::PlayOneShot("crown_get_cue", s.start + Vector3{ 0.0f, 1.2f, 0.0f });
    }
    Vector3 playerPosition = s.start;
    Vector3 eye{}, target{};
    if (t < 0.12f) {
        // 解放された王冠をまずボスの側で見せ、力の出所を明確にします。
        const float rise = CrownEase(t / 0.12f);
        s.fragment->SetTranslate(s.bossStart + Vector3{ 0.0f, 5.0f + rise * 2.0f, 0.0f });
        s.fragment->SetRotation({ 0.0f, s.time * 0.65f, 0.0f });
        target = s.bossStart + Vector3{ 0.0f, 3.0f, 0.0f };
        eye = target + Vector3{ 7.0f, 2.0f, 17.0f };
    } else if (t < 0.28f) {
        const float move = CrownEase((t - 0.12f) / 0.16f);
        const Vector3 fragmentPos = CrownLerp(s.bossStart + Vector3{ 0.0f, 7.0f, 0.0f },
            s.start + Vector3{ 0.0f, 2.4f, 0.0f }, move) + Vector3{ 0.0f, std::sin(move * 3.14159265f) * 2.0f, 0.0f };
        s.fragment->SetTranslate(fragmentPos);
        s.fragment->SetRotation({ 0.0f, s.time * 2.0f, 0.0f });
        target = CrownLerp(s.bossStart + Vector3{ 0.0f, 3.0f, 0.0f }, s.start + Vector3{ 0.0f, 1.5f, 0.0f }, move);
        eye = target + Vector3{ 7.0f, 4.0f, 16.0f };
    } else if (t < 0.42f) {
        // 頭上で一拍止まり、身体へ降りてから姿を切り替えます。
        const float receive = CrownEase((t - 0.32f) / 0.10f);
        s.fragment->SetTranslate(s.start + Vector3{ 0.0f, 2.4f - receive * 1.5f, 0.0f });
        const float size = 0.18f * (1.0f - receive * 0.92f);
        s.fragment->SetScale({ size, size, size });
        s.fragment->SetEmissive(2.0f + receive * 2.0f);
        s.fragment->SetRotation({ 0.0f, s.time * 1.1f, 0.0f });
        player_->SetScale({ s.playerScale.x * (1.0f + receive * 0.12f),
            s.playerScale.y * (1.0f - receive * 0.20f), s.playerScale.z * (1.0f + receive * 0.12f) });
        target = s.start + Vector3{ 0.0f, 1.5f, 0.0f };
        eye = target + Vector3{ 3.6f, 1.6f, 8.5f };
    } else if (t < 0.58f) {
        const float reveal = CrownEase((t - 0.42f) / 0.16f);
        const float swell = std::sin(reveal * 3.14159265f) * (1.0f - reveal);
        const float crouch = CrownEase((t - 0.53f) / 0.05f);
        player_->SetScale({ s.playerScale.x * (1.0f - swell * 0.10f + crouch * 0.22f),
            s.playerScale.y * (1.0f + swell * 0.18f - crouch * 0.35f), s.playerScale.z * (1.0f + crouch * 0.18f) });
        const float turn = CrownEase((t - 0.49f) / 0.07f);
        const float deltaYaw = std::atan2(std::sin(flightYaw - s.playerYaw), std::cos(flightYaw - s.playerYaw));
        player_->SetMoveYaw(s.playerYaw + deltaYaw * turn);
        target = s.start + Vector3{ 0.0f, 1.3f, 0.0f };
        eye = target + Vector3{ 3.6f - reveal * 2.0f, 1.6f, 8.5f };
    } else if (t < 0.86f) {
        const float travel = std::clamp((t - 0.58f) / 0.28f, 0.0f, 1.0f);
        playerPosition = CrownLerp(s.start, destination, travel);
        playerPosition.y += 4.0f * travel * (1.0f - travel) * tuning.escapeHeight;
        const float stretch = std::exp(-travel * 18.0f);
        player_->SetScale({ s.playerScale.x * (1.0f - stretch * 0.16f),
            s.playerScale.y * (1.0f + stretch * 0.28f), s.playerScale.z });
        player_->SetMoveYaw(flightYaw);
        auto rotation = player_->GetRotation();
        rotation.x = -0.30f * (1.0f - travel * 2.0f);
        rotation.z = std::sin(travel * 6.2831853f) * 0.08f;
        player_->SetRotation(rotation);
        if (!s.launched) {
            s.launched = true;
            VFXSequencer::PlayOneShot("crown_victory_land_cue", s.start);
        }
        target = playerPosition + Vector3{ 0.0f, 1.0f, 0.0f };
        eye = target + Vector3{ -10.0f, 6.0f, 17.0f };
        if (static_cast<int>(s.time * 9.0f) != static_cast<int>((s.time - dt) * 9.0f))
            GPUParticleManager::GetInstance()->EmitDirected("false_king_rush_wake", playerPosition,
                { -std::sin(flightYaw), 0.0f, -std::cos(flightYaw) }, 0.7f);
    } else {
        playerPosition = destination;
        const float settle = CrownEase((t - 0.86f) / 0.08f);
        const float squash = (1.0f - settle) * std::cos(settle * 4.712389f);
        player_->SetScale({ s.playerScale.x * (1.0f + squash * 0.24f),
            s.playerScale.y * (1.0f - squash * 0.32f), s.playerScale.z * (1.0f + squash * 0.20f) });
        player_->SetMoveYaw(3.14159265f);
        auto rotation = player_->GetRotation(); rotation.x = rotation.z = 0.0f; player_->SetRotation(rotation);
        if (!s.landed) { s.landed = true; VFXSequencer::PlayOneShot("crown_victory_land_cue", destination); }
        const float reveal = CrownEase((t - 0.86f) / 0.14f);
        s.boss->SetTranslate(s.offset + Vector3{ 0.0f, 0.82f + (1.0f - reveal) * 8.0f, -12.0f });
        target = CrownLerp(destination + Vector3{ 0.0f, 1.3f, 0.0f }, s.offset + Vector3{ 0.0f, 3.0f, 0.0f }, reveal);
        eye = target + Vector3{ 8.0f, 8.0f, 24.0f };
    }
    // 原形を保存した床・壁を順番に落とします。編集モードへ戻す際は全て復元します。
    size_t index = 0;
    for (auto& pose : s.originalObjects) {
        if (!pose.object || pose.object->GetName() == "Stage3_HighCrown_VictoryCrown") continue;
        const float start = 0.53f + static_cast<float>(index++ % 7) * 0.018f;
        const float fall = CrownEase((t - start) / 0.30f);
        const float tremor = t >= 0.10f ? (1.0f - fall) * (0.09f + 0.16f * std::exp(-(t - 0.10f) * 28.0f)) : 0.0f;
        pose.object->SetTranslate(pose.position + Vector3{ std::sin(s.time * 24.0f + index) * tremor,
            -fall * (28.0f + (index % 4) * 6.0f) + std::sin(s.time * 31.0f + index) * tremor * 0.5f, 0.0f });
        pose.object->SetRotation(pose.rotation + Vector3{ fall * 0.18f, 0.0f, fall * ((index % 2) ? 0.22f : -0.22f) });
        if (fall > 0.0f) { pose.object->SetCollisionAttribute(0); pose.object->SetCollisionMask(0); }
        pose.object->UpdateWorldMatrix(true);
    }
    player_->SetTranslate(playerPosition);
    player_->SetVelocity({});
    player_->Object3d::Update(dt);
    s.fragment->Update(dt);
    // 翼は継承直後ではなく、踏み切る準備で開き、着地後に畳みます。
    const float open = CrownEase((t - 0.49f) / 0.07f) * (1.0f - CrownEase((t - 0.86f) / 0.07f));
    const float flap = t >= 0.58f && t < 0.86f ? std::sin((t - 0.58f) * s.duration * 9.0f) : 0.0f;
    s.wings->SetIsVisible(s.granted && open > 0.001f);
    s.wings->SetTranslate(playerPosition + Vector3{ 0.0f, 0.85f, 0.0f });
    s.wings->SetScale({ tuning.movieWingScale * open * (0.95f + flap * 0.12f),
        tuning.movieWingScale * open * (0.75f + flap * 0.20f), tuning.movieWingScale * open * 0.85f });
    s.wings->SetRotation({ flap * 0.16f, player_->GetMoveYaw(), 0.0f });
    s.wings->Update(dt); s.shockwave->Update(dt);
    s.boss->UpdateWorldMatrix(true);
    const float impactShake = t >= 0.10f ? 0.48f * std::exp(-(t - 0.10f) * 48.0f) : 0.0f;
    const float collapseShake = 0.10f * CrownEase((t - 0.50f) / 0.05f) * (1.0f - CrownEase((t - 0.63f) / 0.06f));
    const float shake = (impactShake + collapseShake) * tuning.movieShake;
    const Vector3 jitter{ std::sin(s.time * 47.0f) * shake, std::sin(s.time * 61.0f) * shake * 0.65f, 0.0f };
    eye = eye + jitter; target = target + jitter * 0.35f;
    const Vector3 rotation = CrownLook(eye, target);
    camera->SetFollowTarget(nullptr); camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->SetEye(eye); camera->SetTarget(target); camera->SetRotation(rotation);
    camera->ConfigFixedPoint(eye, rotation); camera->SetFovY(0.58f);
    camera->SetInputEnabled(false);
    CameraManager::GetInstance()->SetActiveCamera(camera);
    if (auto* params = PostEffect::GetInstance()->GetParams())
        params->cinemaBarHeight = s.bars + 0.075f * CrownEase(t / 0.06f) * (1.0f - CrownEase((t - 0.92f) / 0.08f));
    if (t >= 1.0f) FinishCrownFinale(true);
}

void GamePlayScene::FinishCrownFinale(bool completed) {
    auto& s = crownFinale_;
    if (!s.active) return;
    s.active = false;
    if (auto* params = PostEffect::GetInstance()->GetParams()) params->cinemaBarHeight = s.bars;
    if (player_) {
        player_->EndCinematicLock(s.hadControl);
        player_->SetScale(s.playerScale);
        auto rotation = player_->GetRotation(); rotation.x = rotation.z = 0.0f; player_->SetRotation(rotation);
        player_->SetTranslate(completed ? s.offset + Vector3{ 0.0f, 0.82f, 12.0f } : s.start);
        player_->SetVelocity({});
        player_->SetGrounded(true);
        player_->UpdateWorldMatrix(true);
        if (completed) {
            player_->StartEvasionInvincibility(2.5f);
            if (!s.landed) VFXSequencer::PlayOneShot("crown_victory_land_cue", player_->GetWorldPosition());
        }
    }
    if (completed && s.boss) {
        s.boss->BeginFinalArena(s.offset + Vector3{ 0.0f, 0.82f, -12.0f });
        for (auto& pose : s.originalObjects) {
            if (pose.object->GetName() == "Stage3_HighCrown_VictoryCrown") {
                if (auto* receiver = dynamic_cast<GimmickEventReceiver*>(pose.object))
                    receiver->RelocateRuntimeBase(pose.position + s.offset);
                pose.object->SetTranslate(pose.position + s.offset);
                pose.object->UpdateWorldMatrix(true);
            }
        }
    }
    if (!completed && s.boss) {
        s.boss->SetScale(s.bossScale); s.boss->SetRotation(s.bossRotation);
        s.boss->SetTranslate(s.bossStart); s.boss->AbortCrownFinale();
    }
    if (!completed && player_) player_->ClearCrownPower();
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetFovY(s.fov);
        camera->SetFollowTarget(player_); camera->SetLockOnTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::kAimable);
        camera->SetRotation({ 0.24f, 3.14159265f, 0.0f });
        camera->SnapToThirdPerson(18.0f, 2.5f, 0.24f);
        camera->SetInputEnabled(true);
    }
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
    s.fragment.reset();
    s.shockwave.reset(); s.wings.reset();
}

void GamePlayScene::ResetCrownFinaleWorld() {
    auto& s = crownFinale_;
    if (!s.prepared) return;
    FinishCrownFinale(false);
    if (player_) player_->ClearCrownPower();
    for (auto& pose : s.originalObjects) {
        if (!pose.object) continue;
        if (auto* gate = dynamic_cast<GimmickBossGate*>(pose.object))
            gate->SetCinematicOverride(false);
        if (auto* receiver = dynamic_cast<GimmickEventReceiver*>(pose.object))
            receiver->RelocateRuntimeBase(pose.position);
        pose.object->SetTranslate(pose.position); pose.object->SetRotation(pose.rotation);
        pose.object->SetCollisionAttribute(pose.attribute); pose.object->SetCollisionMask(pose.mask);
        pose.object->SetIsVisible(pose.visible); pose.object->UpdateWorldMatrix(true);
    }
    for (auto* object : s.generatedObjects) RequestRemoveObject(object);
    s = CrownFinaleState{};
}

void GamePlayScene::DrawCrownFinaleWorld() {
    if (!crownFinale_.active) return;
    for (auto* visual : { crownFinale_.fragment.get(), crownFinale_.shockwave.get(), crownFinale_.wings.get() }) {
        if (!visual || !visual->GetIsVisible()) continue;
        visual->RefreshRenderCameraData();
        visual->Draw(pointLightResource_.Get(), spotLightResource_.Get());
    }
}

void GamePlayScene::DrawCrownFinaleUI() {
    if (!player_ || !SceneManager::GetInstance()->IsPlaying() || isGoal_ ||
        (controlsGuideOverlay_ && controlsGuideOverlay_->IsActive()) ||
        (pauseMenuOverlay_ && pauseMenuOverlay_->IsActive())) return;
    auto* boss = FindFalseKingBossForHUD();
    if (!boss || !boss->IsEncounterHudActive()) return;
    int index = boss->GetBattlePhase() == 2 ? 0 : (boss->GetBattlePhase() == 3 ? 2 : -1);
    float alpha = 1.0f;
    if (arenaBossIntroActive_) {
        index = 3;
        alpha = CrownEase((arenaBossIntroTimer_ - 2.7f) / 0.35f) *
            (1.0f - CrownEase((arenaBossIntroTimer_ - 5.1f) / 0.35f));
    } else if (crownFinale_.active) {
        const float t = crownFinale_.time / crownFinale_.duration;
        index = t < 0.28f ? 4 : (t < 0.42f ? 1 : 5);
        const float begin = index == 4 ? 0.0f : (index == 1 ? 0.28f : 0.42f);
        const float end = index == 4 ? 0.28f : (index == 1 ? 0.42f : 0.58f);
        alpha = CrownEase((t - begin) / 0.025f) * (1.0f - CrownEase((t - end + 0.025f) / 0.025f));
    }
    if (index < 0) return;
    if (alpha <= 0.001f) return;
    if (!crownFinaleMessages_[index]) {
        const char* paths[] = { "ui/crown_finale/seals.png", "ui/crown_finale/inherit.png", "ui/crown_finale/controls.png",
            "ui/crown_finale/boss_intro.png", "ui/crown_finale/release.png", "ui/crown_finale/awaken.png" };
        crownFinaleMessages_[index] = std::make_unique<Sprite>();
        const uint32_t texture = Sprite::LoadTexture(paths[index]);
        crownFinaleMessages_[index]->Initialize(spriteCommon_.get(), texture);
        crownFinaleMessages_[index]->SetTextureName(paths[index]);
        crownFinaleMessages_[index]->SetAnchorPoint({ 0.5f, 0.5f });
    }
    auto& message = crownFinaleMessages_[index];
    message->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
    const bool movie = crownFinale_.active || arenaBossIntroActive_;
    // 透過文字の実寸比率を維持し、通常時は左下のHPと中央のプレイヤーを避けます。
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(message->GetTextureHandle());
    const float fit = std::min((movie ? 1020.0f : 820.0f) / std::max(1.0f, static_cast<float>(metadata.width)),
        (movie ? 72.0f : 64.0f) / std::max(1.0f, static_cast<float>(metadata.height)));
    message->SetPosition(SpriteLayoutScaler::ScaleDesignPosition({ movie ? 640.0f : 830.0f, 662.0f }, { 1280.0f, 720.0f }));
    message->SetSize(SpriteLayoutScaler::ScaleDesignSize({ metadata.width * fit, metadata.height * fit }, { 1280.0f, 720.0f }));
    message->Update();
    message->Draw();
}
