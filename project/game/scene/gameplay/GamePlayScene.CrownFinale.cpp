#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "Easing.h"
#include <numbers>
#include "Log.h"
#include "game/ui/ControlsGuideOverlay.h"
#include "game/ui/PauseMenuOverlay.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "GimmickEventReceiver.h"
#include "GimmickBossGate.h"
#include "CameraManager.h"
#include "PostEffect.h"
#include "SceneManager.h"
#include "VFXSequencer.h"
#include "GPUParticleManager.h"
#include "DebrisEffectManager.h"
#include "game/ui/SpriteLayoutScaler.h"
#include "TextureManager.h"
#include "game/crown/CrownFinaleTuning.h"
#include "game/crown/CrownBarrageCamera.h"
#include "game/actor/combat/CombatGroundQuery.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <unordered_set>

namespace {
constexpr const char* kCrownFinalePath = "Resources/json/scenario/crown_finale.json";
constexpr std::pair<const char*, float CrownFinaleTuning::*> kFinaleMarkers[] = {
    {"break", &CrownFinaleTuning::movieFragmentBreakTime}, {"land", &CrownFinaleTuning::movieFragmentLandTime},
    {"rage", &CrownFinaleTuning::movieRageTime}, {"pull", &CrownFinaleTuning::movieFragmentPullTime},
    {"absorb", &CrownFinaleTuning::movieFragmentAbsorbTime}, {"body", &CrownFinaleTuning::movieBodyTime},
    {"crown", &CrownFinaleTuning::movieCrownTime}, {"wings", &CrownFinaleTuning::movieWingsTime},
    {"escape", &CrownFinaleTuning::movieEscapeTime}, {"arrive", &CrownFinaleTuning::movieArriveTime}};



engine::Vector3 CrownLerp(const engine::Vector3& a, const engine::Vector3& b, float t) {
    // 演出側で決めた補間率をそのまま使い、位置の行き過ぎも表現できる。
    return a + (b - a) * t;
}

engine::Vector3 CrownLook(const engine::Vector3& eye, const engine::Vector3& target) {
    // カメラから注視点への俯角と水平方向の角度を作る。
    const engine::Vector3 d = target - eye;
    return {-std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)), std::atan2(d.x, d.z), 0.0f};
}
} // namespace

// 王冠決着の専用進行です。通常戦闘を止めた後、演出用状態を更新し、終了時にワールドを復元します。

bool GamePlayScene::StartSealBarrageMovie(EnemyFalseKingSlime* boss, const engine::Vector3& strike) {
    if (!SceneManager::GetInstance()->IsPlaying() || !boss || !player_ || player_->IsDead() ||
        player_->IsCinematicLocked() || crownFinale_.active || sealBarrageMovie_.active ||
        isGoal_)
        return false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera)
        return false;

    auto& movie = sealBarrageMovie_;
    movie.active = true;
    movie.time = 0.0f;
    movie.hadControl = player_->IsControlActive();
    movie.boss = boss->GetWorldPosition();
    movie.strike = strike;
    movie.eye = camera->GetEye();
    movie.target = camera->GetTargetPoint();
    movie.rotation = camera->GetRotation();
    movie.fov = camera->GetFovY();
    movie.follow = camera->GetFollowTarget();
    movie.followMode = camera->GetFollowMode();
    if (auto* params = PostEffect::GetInstance()->GetParams())
        movie.bars = params->cinemaBarHeight;

    // 通常のCinematicLockはGameRuleがダメージを無効にするため、この技では操作だけ止めます。
    player_->SetIsControlActive(false);
    player_->SetVelocity({});
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetLockOnTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    return true;
}

void GamePlayScene::UpdateSealBarrageMovie(float deltaTime) {
    // 封印失敗時の専用カメラを進め、通常の戦闘カメラと切り分ける。
    auto& movie = sealBarrageMovie_;
    if (!movie.active)
        return;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    if (!camera || !player_ || crownFinale_.active) {
        FinishSealBarrageMovie();
        return;
    }

    movie.time += std::max(0.0f, deltaTime);
    const float duration = CrownFinaleTuning::Get().sealBarrageMovieDuration;

    if (movie.time >= duration) {
        FinishSealBarrageMovie();
        return;
    }

    player_->SetIsControlActive(false);
    player_->SetVelocity({});
    auto pose = EvaluateCrownBarrageCamera(movie.time, movie.boss, movie.strike, CrownFinaleTuning::Get());
    const float finish = Easing::SmoothStep01((movie.time - duration + 0.42f) / 0.42f);
    pose.eye = CrownLerp(pose.eye, movie.eye, finish);
    pose.target = CrownLerp(pose.target, movie.target, finish);
    pose.fov += (movie.fov - pose.fov) * finish;
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->SetEye(pose.eye);
    camera->SetTarget(pose.target);
    camera->SetRotation(CrownLook(pose.eye, pose.target));
    camera->ConfigFixedPoint(pose.eye, CrownLook(pose.eye, pose.target));
    camera->SetFovY(pose.fov);
    camera->Update();
    if (auto* params = PostEffect::GetInstance()->GetParams())
        params->cinemaBarHeight = movie.bars + 0.065f *
            Easing::SmoothStep01(movie.time / 0.22f) * (1.0f - Easing::SmoothStep01((movie.time - duration + 0.40f) / 0.40f));
}

void GamePlayScene::FinishSealBarrageMovie() {
    // 演出で切り替えたカメラと操作状態を通常の戦闘へ戻す。
    auto& movie = sealBarrageMovie_;
    if (!movie.active)
        return;
    movie.active = false;
    if (auto* params = PostEffect::GetInstance()->GetParams())
        params->cinemaBarHeight = movie.bars;
    if (player_ && !player_->IsDead()) {
        player_->SetVelocity({});
        player_->SetIsControlActive(movie.hadControl);
    }

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetFovY(movie.fov);
        camera->SetFollowTarget(movie.follow);
        camera->SetFollowMode(movie.followMode);
        camera->SetEye(movie.eye);
        camera->SetTarget(movie.target);
        camera->SetRotation(movie.rotation);
        camera->SetInputEnabled(true);
        camera->Update();
    }
}

CrownFinaleTuning GamePlayScene::GetCrownFinaleTiming() const {
    auto tuning = CrownFinaleTuning::Get();
    if (crownFinaleSequence_.duration <= 0) return tuning;
    tuning.movieDuration = crownFinaleSequence_.GetAuthoredDuration();
    for (const auto& [name, member] : kFinaleMarkers)
        tuning.*member = crownFinaleSequence_.GetSignalTime(name, tuning.*member * tuning.movieDuration) / tuning.movieDuration;
    return tuning;
}

bool GamePlayScene::StartCrownFinale(EnemyFalseKingSlime* boss) {
    if (crownFinale_.active || crownFinale_.prepared || !boss || !player_ || player_->IsDead() ||
        player_->IsCinematicLocked() || isGoal_)
        return false;
    Camera* camera = crownMoviePreview_.active ? crownMoviePreview_.camera.get() : CameraManager::GetInstance()->GetMainCamera();
    if (!camera)
        return false;

    // 必須の足場を確認してから生成。攻撃プレビュー等では脱出を開始しません。
    bool hasFloor = false, hasArena = false;

    for (const auto& object : GetObjects()) {
        hasFloor |= object->GetName().find("Stage3_HighCrown_ArenaFloor_") != std::string::npos;
        hasArena |= object->GetName() == "Stage3_HighCrown_ArenaVisual";
    }

    if (!hasFloor || !hasArena)
        return false;
    if (!crownFinaleSequence_.Load(kCrownFinalePath))
        return false;
    auto& s = crownFinale_;
    const auto tuning = GetCrownFinaleTiming();
    // 編集した節目が欠けたり逆転した場合は、世界を変更する前に停止する。
    float previous = 0;
    for (const auto& [name, member] : kFinaleMarkers) {
        const float time = crownFinaleSequence_.GetSignalTime(name, -1) / tuning.movieDuration;
        const float gap = member == &CrownFinaleTuning::movieFragmentPullTime ? .03f :
            (member == &CrownFinaleTuning::movieCrownTime || member == &CrownFinaleTuning::movieWingsTime ? .012f : 0);
        if (!(time > previous + gap && time < 1)) {
            LOG_ERROR("Crown inheritance: invalid required marker {}", name);
            return false;
        }
        previous = time;
    }
    s.active = s.prepared = true;
    s.time = 0.0f;
    s.duration = tuning.movieDuration;
    s.offset = {tuning.destinationX, 0.0f, 0.0f};
    s.boss = boss;
    s.bossStart = boss->GetTranslate();
    s.bossScale = boss->GetScale();
    s.bossRotation = boss->GetRotation();
    s.bossModel = boss->GetModelName();
    s.playerYaw = player_->GetMoveYaw();
    s.hadControl = player_->IsControlActive();
    s.fov = camera->GetFovY();
    if (auto* params = PostEffect::GetInstance()->GetParams())
        s.bars = params->cinemaBarHeight;
    player_->BeginCinematicLock();
    s.start = player_->GetTranslate();
    s.playerScale = player_->GetScale();
    s.playerModel = player_->GetModelName();
    s.playerColor = player_->GetColor();
    s.playerMaterial = player_->GetMaterialType();
    std::vector<std::unique_ptr<engine::Object3d>> previewArenaCopies;

    for (const auto& object : GetObjects()) {
        // プレビューでは複製だけを崩壊させ、編集対象のTransformを変更しません。
        if (crownMoviePreview_.active &&
            std::find(crownMoviePreview_.objects.begin(), crownMoviePreview_.objects.end(), object.get()) ==
                crownMoviePreview_.objects.end())
            continue;
        const std::string& name = object->GetName();
        const bool floor = name.find("Stage3_HighCrown_ArenaFloor_") != std::string::npos;
        const bool arena = name == "Stage3_HighCrown_ArenaVisual";
        const bool wall = name.find("Stage3_HighCrown_Wall") != std::string::npos;
        const bool throne = name == "Stage3_HighCrown_Throne";
        const bool reward = name == "Stage3_HighCrown_VictoryCrown";
        const bool foundation = name == "Stage3_HighCrown_Background_Foundation";
        const bool approach =
            name.find("Stage3_HighCrown_Approach") != std::string::npos || name == "Stage3_HighCrown_StairSupport" ||
            name == "Stage3_HighCrown_Background_EntranceCitadel" ||
            name.find("Stage3_HighCrown_Entrance") != std::string::npos ||
            name.find("Stage3_HighCrown_BossGate") != std::string::npos ||
            name.find("Stage3_HighCrown_DecorTower") != std::string::npos || name == "Stage3_PreBoss_CopyMemoryStation";
        if (floor || arena || wall || throne || reward || foundation || approach) {
            s.originalObjects.push_back({object.get(), object->GetTranslate(), object->GetRotation(),
                                         object->GetCollisionAttribute(), object->GetCollisionMask(),
                                         object->GetIsVisible()});
            if (auto* gate = dynamic_cast<GimmickBossGate*>(object.get()))
                gate->SetCinematicOverride(true);
        }

        if (arena) {
            for (int i = 0; i < 64; ++i) {
                const int x = i / 8, z = i % 8;
                if ((x <= 1 || x >= 6) && (z == 0 || z == 7))
                    continue;
                auto chunk = object->Clone();
                if (!chunk)
                    continue;
                char model[128];
                std::snprintf(model, sizeof(model), "Stages/high_crown/high_crown_fracture_%02d.obj", i);
                chunk->SetModel(model);
                chunk->SetName("CrownFracture_" + std::to_string(i));
                chunk->ClearEditorHiddenReasons();
                chunk->SetEditorInternal(crownMoviePreview_.active);
                chunk->SetEventID(-1);
                chunk->SetTargetID(-1);
                chunk->SetCollisionAttribute(0);
                chunk->SetCollisionMask(0);
                const engine::Vector3 position = engine::Math::Transform({-35.0f + x * 10.0f, 0.0f, 35.0f - z * 10.0f}, object->GetWorldMatrix());
                chunk->SetTranslate(position);
                chunk->SetIsVisible(false);
                chunk->UpdateWorldMatrix(true);
                s.fractureChunks.push_back({chunk.get(), position, chunk->GetRotation(), 0, 0, false});
                s.generatedObjects.push_back(chunk.get());
                previewArenaCopies.push_back(std::move(chunk));
            }
        }

        if (floor || arena || foundation) {
            auto copy = object->Clone();
            if (!copy)
                continue;
            copy->SetName("CrownFinalArena_" + name);

            if (crownMoviePreview_.active) {
                copy->SetEditorInternal(true);
                copy->SetIsLocked(true);
            }

            copy->SetEventID(-1);
            copy->SetTargetID(-1);
            copy->SetTranslate(object->GetTranslate() + s.offset);

            if (arena) {
                copy->SetColor({0.72f, 0.84f, 1.0f, 1.0f});
                copy->SetEmissive(1.1f);
            }

            copy->UpdateWorldMatrix(true);
            s.generatedObjects.push_back(copy.get());
            if (crownMoviePreview_.active)
                previewArenaCopies.push_back(std::move(copy));
            else AddObject(std::move(copy));
        }
    }

    for (auto& copy : previewArenaCopies)
        AddObjectImmediate(std::move(copy));

    s.lastSealPosition = boss->GetLastBrokenCrownSealPosition();
    const auto bossCollider = boss->GetColliderConfig();
    boss->SetModel("Characters/false_king_slime/false_king_slime_without_emblem.obj");
    boss->SetColliderConfig(bossCollider);
    boss->UpdateWorldMatrix(true);
    s.fragmentStart = engine::Math::Transform({0.0f, 3.44f, -1.76f}, boss->GetWorldMatrix());
    const engine::Vector3 towardBoss = s.bossStart - s.start;
    const float planarLength = std::sqrt(towardBoss.x * towardBoss.x + towardBoss.z * towardBoss.z);
    const engine::Vector3 direction = planarLength > 0.01f
                                  ? engine::Vector3{towardBoss.x / planarLength, 0.0f, towardBoss.z / planarLength}
                                  : engine::Vector3{std::sin(s.playerYaw), 0.0f, std::cos(s.playerYaw)};
    const engine::Vector3 sample = s.start + direction * tuning.movieFragmentDistance;
    s.fragmentGround = CombatGroundQuery::FindGroundPoint(sample, player_, {8.0f, 24.0f}, s.start.y - 0.75f);
    s.fragmentGround.y += 0.51f * s.bossScale.y * tuning.movieFragmentScale;
    // 装飾・追従・カメラは同じDirector素材から再生し、ゲーム固有の位置だけ接続する。
    crownFinalePlayer_.Initialize(SceneManager::GetInstance());
    crownFinalePlayer_.SetBindingResolver([this](const CinematicObjectBinding& binding) -> engine::Object3d* {
        if (binding.targetName == "boss" || binding.targetName == "$Actor") return crownFinale_.boss;
        if (binding.targetName == "player" || binding.targetName == "$Gate") return player_;
        return CinematicPlayer::ResolveSceneTarget(this, binding);
    });
    crownFinalePlayer_.SetPositionResolver([this](const CinematicObjectBinding& binding)
        -> std::optional<engine::Vector3> {
        const auto& state = crownFinale_;
        if (binding.targetName == "fragment_ground") return state.fragmentGround;
        if (binding.targetName == "player_start") return state.start;
        if (binding.targetName == "boss_start") return state.bossStart;
        if (binding.targetName == "destination") return state.offset + engine::Vector3{0,.82f,12};
        if (binding.targetName == "final_arena_focus") return state.offset + engine::Vector3{0,3,0};
        auto direction = state.fragmentGround - state.start;
        direction.y = 0;
        const float length = engine::Math::Length(direction);
        direction = length > .001f ? direction * (1.0f / length) : engine::Vector3{0,0,1};
        if (binding.targetName == "mouth") return state.start + direction * .70f + engine::Vector3{0,.22f,0};
        if (binding.targetName == "transform_eye") return state.start + direction * 5.5f + engine::Vector3{1.6f,2.1f,0};
        return std::nullopt;
    });
    crownFinalePlayer_.SetSequence(&crownFinaleSequence_);
    const auto issues = crownFinalePlayer_.ValidateBindings();
    if (!issues.empty()) {
        for (const auto& issue : issues) LOG_ERROR("Crown inheritance: {}", issue);
        ResetCrownFinaleWorld();
        return false;
    }
    s.fragment = crownFinalePlayer_.ResolveBinding({"fragment",-1});
    auto* sealAnchor = crownFinalePlayer_.ResolveBinding({"seal_anchor",-1});
    auto* releaseAnchor = crownFinalePlayer_.ResolveBinding({"release_anchor",-1});
    if (!s.fragment || !sealAnchor || !releaseAnchor) {
        ResetCrownFinaleWorld();
        return false;
    }
    s.fragment->SetTranslate(s.fragmentStart);
    s.fragment->SetScale(s.bossScale);
    s.fragment->SetRotation(s.bossRotation);
    s.fragment->SetIsVisible(true);
    sealAnchor->SetTranslate(s.lastSealPosition);
    releaseAnchor->SetTranslate(s.fragmentStart);
    releaseAnchor->SetScale(s.bossScale);
    crownFinalePlayer_.Play(false);
    if (!crownFinalePlayer_.IsPlaying()) {
        ResetCrownFinaleWorld();
        return false;
    }
    UpdateCrownFinale(0);
    return true;
}

void GamePlayScene::UpdateCrownFinale(float dt) {
    auto& s = crownFinale_;
    if (!s.active)
        return;
    Camera* camera = crownMoviePreview_.active ? crownMoviePreview_.camera.get() : CameraManager::GetInstance()->GetMainCamera();

    if (!camera || !player_ || !s.boss || player_->IsDead()) {
        ResetCrownFinaleWorld();
        return;
    }
    const auto tuning = GetCrownFinaleTiming();
    const float previousTime = s.time;
    s.time += std::max(0.0f, dt);
    const float t = std::clamp(s.time / s.duration, 0.0f, 1.0f);
    const float breakTime = tuning.movieFragmentBreakTime, landTime = tuning.movieFragmentLandTime;
    const float rageTime = tuning.movieRageTime, pullTime = tuning.movieFragmentPullTime;
    const float absorbTime = tuning.movieFragmentAbsorbTime, bodyTime = tuning.movieBodyTime;
    const float crownTime = tuning.movieCrownTime, wingsTime = tuning.movieWingsTime;
    const float escapeTime = tuning.movieEscapeTime, arriveTime = tuning.movieArriveTime;
    const float bodyEnd = crownTime - 0.012f, crownEnd = wingsTime - 0.012f;
    const engine::Vector3 destination = s.offset + engine::Vector3{0.0f, 0.82f, 12.0f};
    const float flightYaw = std::atan2(destination.x - s.start.x, destination.z - s.start.z);
    auto crossed = [&](float when) { return previousTime < when * s.duration && s.time >= when * s.duration; };
    auto periodic = [&](float rate) { return dt > 0.0f && static_cast<int>(s.time * rate) != static_cast<int>(previousTime * rate); };

    // 一度だけの破壊音と破片は時刻の通過で発火し、フレーム落ちでも重複させません。
    if (crossed(.005f)) {
        VFXSequencer::PlayOneShot("pull_catch_cue",s.lastSealPosition,{1.4f,1.4f,1.4f});
        DebrisEffectConfig debris;

        if (DebrisEffectManager::GetInstance()->LoadConfig("Resources/json/debris/false_king_seal_shatter.json",debris)) {
            debris.color={.85f,.30f,1,1};
            debris.groundY=s.lastSealPosition.y-2;
            DebrisEffectManager::GetInstance()->SpawnFromConfig(debris,s.lastSealPosition);
        }
    }

    // 破断の衝撃と暴走は別の動作です。欠片を地面で見せ終わるまで床は崩しません。
    auto bossPose = [&](float time) {
        const float crack = time < breakTime ? std::sin(time * 320.0f) * Easing::SmoothStep01(time / breakTime) * 0.025f : 0.0f;
        const float rage = Easing::SmoothStep01((time - rageTime + 0.018f) / 0.04f);
        const float throb = time < escapeTime ? std::sin(time * s.duration * 15.0f) * rage * 0.075f : 0.0f;
        return engine::Vector3{s.bossScale.x * (1.0f + rage * 0.08f + throb),
                       s.bossScale.y * (1.0f - crack - rage * 0.09f - throb), s.bossScale.z * (1.0f + rage * 0.08f)};
    };

    s.boss->SetScale(bossPose(t));
    s.boss->SetTranslate(s.bossStart);
    s.boss->UpdateWorldMatrix(false);
    const engine::Vector3 release = engine::Math::Transform({0.0f, 3.44f, -1.76f}, engine::Math::MakeAffineMatrix(bossPose(breakTime), s.bossRotation, s.bossStart));
    const engine::Vector3 towardFragment = s.fragmentGround - s.start;
    const float pullYaw = std::atan2(towardFragment.x, towardFragment.z);
    const engine::Vector3 pullDirection{std::sin(pullYaw), 0.0f, std::cos(pullYaw)};
    const engine::Vector3 mouth = s.start + pullDirection * 0.70f + engine::Vector3{0.0f, 0.22f, 0.0f};
    engine::Vector3 fragmentPosition = release, fragmentRotation = s.bossRotation, fragmentScale = s.bossScale;

    // 王冠片は本体への固定、落下、着地、吸収の順に同じObjectの姿勢を切り替えます。
    if (t < breakTime) {
        fragmentPosition = engine::Math::Transform({0.0f, 3.44f, -1.76f}, s.boss->GetWorldMatrix());
        fragmentScale = bossPose(t);
    } else if (t < landTime) {
        const float fall = (t - breakTime) / (landTime - breakTime);
        fragmentPosition = CrownLerp(release, s.fragmentGround, fall);
        fragmentPosition.y += std::sin(fall * std::numbers::pi_v<float>) * tuning.movieFragmentFallArc;
        fragmentRotation = s.bossRotation + engine::Vector3{std::sin(fall * std::numbers::pi_v<float>) * 0.55f, fall * (2.0f * std::numbers::pi_v<float>), fall * 0.16f};
        fragmentScale = s.bossScale * (1.0f + (tuning.movieFragmentScale - 1.0f) * Easing::SmoothStep01(fall));
    } else if (t < pullTime) {
        const float settle = std::clamp((t - landTime) / 0.04f, 0.0f, 1.0f);
        fragmentPosition = s.fragmentGround + engine::Vector3{0.0f, std::fabs(std::sin(settle * (2.0f * std::numbers::pi_v<float>))) * (1.0f - settle) * 0.25f, 0.0f};
        fragmentRotation = s.bossRotation + engine::Vector3{0.0f, (2.0f * std::numbers::pi_v<float>), 0.16f};
        fragmentScale = s.bossScale * tuning.movieFragmentScale;
    } else {
        const float receive = Easing::SmoothStep01((t - pullTime) / (absorbTime - pullTime));
        fragmentPosition = CrownLerp(s.fragmentGround, mouth, receive);
        fragmentPosition.y += std::sin(receive * std::numbers::pi_v<float>) * 0.22f;
        fragmentScale = s.bossScale * (tuning.movieFragmentScale * std::max(0.001f, 1.0f - Easing::SmoothStep01((receive - 0.65f) / 0.35f)));
        fragmentRotation = s.bossRotation + engine::Vector3{0.0f, (2.0f * std::numbers::pi_v<float>) + receive * 1.2f, 0.16f};
    }

    s.fragment->SetIsVisible(t < absorbTime);
    s.fragment->SetTranslate(fragmentPosition);
    s.fragment->SetScale(fragmentScale);
    s.fragment->SetRotation(fragmentRotation);
    s.fragment->SetEmissive(1.2f + (t >= landTime ? 0.4f + 0.15f * std::sin(s.time * 6.0f) : 0.0f));
    if (crossed(0.04f))
        VFXSequencer::PlayOneShot("pull_catch_cue", release, {0.7f,0.7f,0.7f});
    if (crossed(breakTime)) {
        VFXSequencer::PlayOneShot("pull_catch_cue", release, {0.9f,0.9f,0.9f});
    }

    if (t>=landTime && t<pullTime && periodic(5))
        GPUParticleManager::GetInstance()->EmitDirected("crown_goal_idle_sparkle",s.fragmentGround,{0,1,0},.4f);

    // プレイヤーの向きを先に欠片へ寄せ、引き寄せ区間でだけ操作用のフック表示を使います。
    engine::Vector3 playerPosition=s.start;
    const float notice=Easing::SmoothStep01((t-rageTime-.03f)/(pullTime-rageTime-.03f));
    const float turnDelta=std::atan2(std::sin(pullYaw-s.playerYaw),std::cos(pullYaw-s.playerYaw));
    if (t<pullTime)
        player_->SetMoveYaw(s.playerYaw+turnDelta*notice);
    if (t>=pullTime && t<absorbTime) {
        const float receive=std::clamp((t-pullTime)/(absorbTime-pullTime),0.0f,1.0f);
        player_->SetMoveYaw(pullYaw);
        player_->StepCinematicPullVisual(dt,towardFragment,receive);

        if (engine::Object3d* marker=player_->GetHookMarker()) {
            const float shot=Easing::SmoothStep01((t-pullTime)/.012f);
            const engine::Vector3 tip=CrownLerp(mouth,fragmentPosition,shot),diff=tip-mouth;
            marker->SetIsVisible(true);
            marker->SetColor({.10f,.82f,1,1});
            marker->SetTranslate(mouth+diff*.5f);
            marker->SetRotation({std::atan2(-diff.y,std::sqrt(diff.x*diff.x+diff.z*diff.z)),std::atan2(diff.x,diff.z),0});
            marker->SetScale({.10f,.10f,std::max(.01f,engine::Math::Length(diff))});
            marker->UpdateWorldMatrix(true);
        }

    }

    // 吸収が完了した境目で引き寄せ表示を閉じ、通常サイズへ戻します。
    if (crossed(absorbTime)) {
        player_->EndCinematicPullVisual();
        player_->SetScale(s.playerScale);
        player_->SetMoveYaw(pullYaw);
        VFXSequencer::PlayOneShot("pull_catch_cue",mouth,{.8f,.8f,.8f});
    }

    if (t>=absorbTime && t<bodyTime) {
        player_->SetMaterialType(1);
        player_->SetColor({.72f,.95f,1,.48f});
        player_->SetScale(s.playerScale*(1+.04f*std::sin(s.time*11)));

        if (periodic(8))
            GPUParticleManager::GetInstance()->EmitDirected("goal_clear_gold_motes",s.start+engine::Vector3{0,.8f,0},{0,1,0},.45f);
    }

    // 実Playerの置換面だけを設定する。金色の仮ボディは共通再生器が逆側から形成する。
    if (t>=bodyTime && t<bodyEnd) {
        const float progress = Easing::SmoothStep01((t-bodyTime)/(bodyEnd-bodyTime));
        player_->SetScale(s.playerScale);
        player_->SetMaterialType(25);
        player_->SetColor(s.playerColor);
        engine::Object3d::PortalClipSettings clipSettings;
        clipSettings.enabled = progress < .999f;
        clipSettings.progress = 0;
        clipSettings.center = s.start + engine::Vector3{0,-.1f+1.75f*progress+.52f,0};
        clipSettings.normal = {0,-1,0};
        clipSettings.edgeWidth = .035f;
        clipSettings.dissolve = .055f;
        clipSettings.color = {1,.85f,.3f,1};
        player_->SetPortalClip(clipSettings);
    }

    // 形成が終わってから実モデルを交換し、途中のクリップ設定を持ち越しません。
    if (t>=bodyEnd && !s.bodyFormed) {
        s.bodyFormed=true;
        player_->ClearPortalClip();
        player_->SetModel("Characters/slime_crown/slime_crown_body.gltf");
        player_->SetMaterialType(0);
        player_->SetColor({1,.77f,.25f,1});
    }

    // 能力付与は見た目の形成完了と一致させ、初めから装備済みに見えないようにします。
    if (t>=crownEnd && !s.granted) {
        s.granted=player_->ApplyStoredCopy("Crown",true);

        if (!s.granted) {
            ResetCrownFinaleWorld();
            return;
        }
        player_->SetScale(s.playerScale);
        player_->SetMoveYaw(pullYaw);
    }

    // 羽の出現と飛び出しは別の時刻にし、発進前に向きとつぶれを作ります。

    if (t>=wingsTime && t<escapeTime) {
        const float anticipation=Easing::SmoothStep01((t-escapeTime+.025f)/.025f);
        const float turn=Easing::SmoothStep01((t-wingsTime)/(escapeTime-wingsTime));
        player_->SetMoveYaw(pullYaw+std::atan2(std::sin(flightYaw-pullYaw),std::cos(flightYaw-pullYaw))*turn);
        player_->SetScale({s.playerScale.x*(1+anticipation*.20f),s.playerScale.y*(1-anticipation*.30f),s.playerScale.z});
    }

    // 飛行中は弧と伸びを評価し、着地後は位置を固定してつぶれを減衰させます。
    if (t>=escapeTime && t<arriveTime) {
        const float travel=(t-escapeTime)/(arriveTime-escapeTime);
        playerPosition=CrownLerp(s.start,destination,travel);
        playerPosition.y+=std::sin(travel*std::numbers::pi_v<float>)*tuning.escapeHeight;
        const float stretch=std::exp(-travel*18);
        player_->SetScale({s.playerScale.x*(1-stretch*.14f),s.playerScale.y*(1+stretch*.24f),s.playerScale.z});
        player_->SetMoveYaw(flightYaw);
        player_->SetRotation({-.25f*(1-2*travel),flightYaw+player_->GetVisualYawOffset(),std::sin(travel*(2.0f * std::numbers::pi_v<float>))*.12f});

        if (periodic(15))
            GPUParticleManager::GetInstance()->EmitDirected("false_king_rush_wake",playerPosition,{-std::sin(flightYaw),0,-std::cos(flightYaw)},.75f);
    } else if (t>=arriveTime) {
        playerPosition=destination;
        const float settle=Easing::SmoothStep01((t-arriveTime)/.035f),squash=(1-settle)*std::cos(settle*(std::numbers::pi_v<float> * 1.5f));
        player_->SetScale({s.playerScale.x*(1+squash*.23f),s.playerScale.y*(1-squash*.30f),s.playerScale.z*(1+squash*.18f)});
        player_->SetMoveYaw(std::numbers::pi_v<float>);
        player_->SetRotation({0,std::numbers::pi_v<float>+player_->GetVisualYawOffset(),0});

        if (!s.landed) {
            s.landed=true;
            VFXSequencer::PlayOneShot("crown_victory_land_cue",destination);
        }
        s.boss->SetTranslate(s.offset+engine::Vector3{0,.82f+(1-Easing::SmoothStep01((t-arriveTime)/.065f))*8,-12});
    }

    player_->SetTranslate(playerPosition);
    player_->SetVelocity({});
    player_->UpdateWorldMatrix(false);
    // 表示メッシュを実際に分割した床片です。外周から割れ、最後に足元が抜けます。

    for (auto& pose:s.fractureChunks) {
        const engine::Vector3 delta=pose.position-s.start;
        const float distance=std::sqrt(delta.x*delta.x+delta.z*delta.z);
        const float delay=1-std::clamp(distance/48.0f,0.0f,1.0f);
        const bool supportsPlayer=std::abs(delta.x)<=5.1f && std::abs(delta.z)<=5.1f;
        const float begin=supportsPlayer?escapeTime+.004f:rageTime+.02f+delay*(escapeTime-rageTime-.01f);
        const float age=std::max(0.0f,(t-begin)*s.duration);
        pose.object->SetIsVisible(t>=rageTime && age<4.0f);
        pose.object->SetTranslate(pose.position+engine::Vector3{std::sin(age*2+pose.position.x)*age*.6f,-4.9f*age*age,0});
        pose.object->SetRotation(pose.rotation+engine::Vector3{age*.18f,age*.04f,age*.13f});
        pose.object->UpdateWorldMatrix(false);

        if (crossed(begin) && distance>10)
            VFXSequencer::PlayOneShot("crown_inheritance_floor_break_cue",pose.position+engine::Vector3{0,.8f,0});
    }

    // 親子を両方動かすと子だけ落下量が二重になるため、親が演出対象なら子の姿勢は親に追従させます。
    std::unordered_set<engine::Object3d*> animatedObjects;
    animatedObjects.reserve(s.originalObjects.size());
    for (const auto& pose : s.originalObjects)
        animatedObjects.insert(pose.object);

    size_t index=0;

    for (auto& pose:s.originalObjects) {
        if (auto* parent = pose.object->GetParent(); parent && animatedObjects.find(parent) != animatedObjects.end()) {
            if (t >= rageTime) {
                pose.object->SetCollisionAttribute(0);
                pose.object->SetCollisionMask(0);
            }
            continue;
        }
        const std::string& name=pose.object->GetName();
        if (name=="Stage3_HighCrown_VictoryCrown")
            continue;
        if (name=="Stage3_HighCrown_ArenaVisual") {
            pose.object->SetIsVisible(t<rageTime);
            continue;
        }
        const bool floor=name.find("ArenaFloor_")!=std::string::npos;
        const float begin=floor?escapeTime+.005f:rageTime+.02f+(index++%9)*.012f;
        const float age=std::max(0.0f,(t-begin)*s.duration);
        pose.object->SetTranslate(pose.position+engine::Vector3{0,-4.9f*age*age,0});
        pose.object->SetRotation(pose.rotation+engine::Vector3{age*.07f,0,age*.055f});

        if (age>0) {
            pose.object->SetCollisionAttribute(0);
            pose.object->SetCollisionMask(0);
        }
        pose.object->UpdateWorldMatrix(false);
    }

    // 対象の姿勢を確定してから、同じ時刻の装飾・VFX・カメラを評価する。
    crownFinalePlayer_.SetTime(s.time, dt > 0.0f);
    if (!crownFinalePlayer_.IsPlaying() && !crownFinalePlayer_.IsPreviewing()) {
        ResetCrownFinaleWorld();
        return;
    }

    // 通常Updateもカメラ変更前にモデルを更新していました。最後に全描画定数を揃えます。
    for (auto& object:GetObjects())
        if (object && !object->IsReplayRemoved())
            object->UpdateWorldMatrix(true);
    if (engine::Object3d* marker=player_->GetHookMarker())
        marker->UpdateWorldMatrix(true);
    if (t>=1 && !crownMoviePreview_.active)
        FinishCrownFinale(true);
}

void GamePlayScene::FinishCrownFinale(bool completed) {
    // 結果に応じて演出終了後のボス・Player状態を確定する。
    auto& s = crownFinale_;
    if (!s.active)
        return;
    crownFinalePlayer_.Stop(true);
    s.active = false;
    if (auto* params = PostEffect::GetInstance()->GetParams())
        params->cinemaBarHeight = s.bars;
    if (player_) {
        player_->EndCinematicLock(s.hadControl);
        // 中断時は元位置、完走時は最終戦の開始地点へ移し、重複した着地を防ぐ。
        if (!completed)
            player_->SetMoveYaw(s.playerYaw);
        player_->SetScale(s.playerScale);
        auto rotation = player_->GetRotation();
        rotation.x = rotation.z = 0.0f;
        player_->SetRotation(rotation);
        player_->SetTranslate(completed ? s.offset + engine::Vector3{0.0f, 0.82f, 12.0f} : s.start);
        player_->SetVelocity({});
        player_->SetGrounded(true);
        player_->UpdateWorldMatrix(true);

        if (completed) {
            player_->StartEvasionInvincibility(2.5f);
            if (!s.landed)
                VFXSequencer::PlayOneShot("crown_victory_land_cue", player_->GetWorldPosition());
        }
    }

    if (completed && s.boss) {
        s.boss->BeginFinalArena(s.offset + engine::Vector3{0.0f, 0.71f, -12.0f});

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
        // プレビュー中断では最終形態へ確定せず、開始前のモデルと衝突形状を戻す。
        const auto collider = s.boss->GetColliderConfig();
        s.boss->SetModel(s.bossModel);
        s.boss->SetColliderConfig(collider);
        s.boss->SetScale(s.bossScale);
        s.boss->SetRotation(s.bossRotation);
        s.boss->SetTranslate(s.bossStart);
        if (!crownMoviePreview_.active)
            s.boss->AbortCrownFinale();
    }

    if (player_) {
        player_->EndCinematicPullVisual();
        player_->ClearPortalClip();

        if (!completed) {
            player_->ClearCrownPower();
            player_->SetModel(s.playerModel);
            player_->SetMaterialType(s.playerMaterial);
            player_->SetColor(s.playerColor);
        }
    }

    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetFovY(s.fov);
        camera->SetFollowTarget(player_);
        camera->SetLockOnTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::kAimable);
        camera->SetRotation({0.24f, std::numbers::pi_v<float>, 0.0f});
        camera->SnapToThirdPerson(18.0f, 2.5f, 0.24f);
        camera->SetInputEnabled(true);
    }

    CameraManager::GetInstance()->SetActiveCamera(nullptr);
    // 装飾ObjectはStopで共通再生器が破棄する。借用参照だけを外す。
    s.fragment = nullptr;
}

void GamePlayScene::ResetCrownFinaleWorld() {
    // プレビューや中断後に生成した演出Objectを片付け、元の世界へ戻す。
    auto& s = crownFinale_;
    if (!s.prepared)
        return;
    FinishCrownFinale(false);

    if (s.boss && !s.bossModel.empty()) {
        const auto collider = s.boss->GetColliderConfig();
        s.boss->SetModel(s.bossModel);
        s.boss->SetColliderConfig(collider);
    }

    if (player_)
        player_->ClearCrownPower();
    for (auto& pose : s.originalObjects) {
        if (!pose.object)
            continue;
        if (auto* gate = dynamic_cast<GimmickBossGate*>(pose.object))
            gate->SetCinematicOverride(false);
        if (auto* receiver = dynamic_cast<GimmickEventReceiver*>(pose.object))
            receiver->RelocateRuntimeBase(pose.position);
        pose.object->SetTranslate(pose.position);
        pose.object->SetRotation(pose.rotation);
        pose.object->SetCollisionAttribute(pose.attribute);
        pose.object->SetCollisionMask(pose.mask);
        pose.object->SetIsVisible(pose.visible);
        pose.object->UpdateWorldMatrix(true);
    }

    for (auto* object : s.generatedObjects) {
        if (crownMoviePreview_.active) {
            object->SetIsVisible(false);
            object->SetReplayRemoved(true);
        }

        RequestRemoveObject(object);
    }

    s = CrownFinaleState{};
}

void GamePlayScene::DrawCrownFinaleWorld() {
    if (crownFinale_.active)
        crownFinalePlayer_.DrawPresentation(pointLightResource_.Get(), spotLightResource_.Get());
}

void GamePlayScene::DrawCrownFinaleUI() {
    if (!player_ || (!SceneManager::GetInstance()->IsPlaying() && !crownMoviePreview_.active) || isGoal_ ||
        (controlsGuideOverlay_ && controlsGuideOverlay_->IsActive()) ||
        (pauseMenuOverlay_ && pauseMenuOverlay_->IsActive()))
        return;
    auto* boss = crownMoviePreview_.active ? crownFinale_.boss : FindFalseKingBossForHUD();
    if (!boss || (!crownFinale_.active && !boss->IsEncounterHudActive()))
        return;
    int index = boss->GetBattlePhase() == 2 ? 0 : (boss->GetBattlePhase() == 3 ? 2 : -1);
    float alpha = 1.0f;

    if (arenaBossIntroActive_) {
        index = 3;
        alpha = Easing::SmoothStep01((arenaBossIntroTimer_ - arenaBossIntroMessageInTime_) / 0.35f) *
                (1.0f - Easing::SmoothStep01((arenaBossIntroTimer_ - arenaBossIntroMessageOutTime_) / 0.35f));
    } else if (crownFinale_.active) {
        const float t = crownFinale_.time / crownFinale_.duration;
        const auto tuning = GetCrownFinaleTiming();
        index = t < tuning.movieFragmentPullTime ? 4 : (t < tuning.movieFragmentAbsorbTime ? 1 : 5);
        const float begin = index == 4 ? 0.0f : (index == 1 ? tuning.movieFragmentPullTime : tuning.movieFragmentAbsorbTime);
        const float end = index == 4 ? tuning.movieFragmentPullTime : (index == 1 ? tuning.movieFragmentAbsorbTime : tuning.movieEscapeTime);
        alpha = Easing::SmoothStep01((t - begin) / 0.025f) * (1.0f - Easing::SmoothStep01((t - end + 0.025f) / 0.025f));
    }

    if (index < 0)
        return;
    if (alpha <= 0.001f)
        return;
    if (!crownFinaleMessages_[index]) {
        const char* paths[] = {"ui/crown_finale/seals.png",    "ui/crown_finale/inherit.png",
                               "ui/crown_finale/controls.png", "ui/crown_finale/boss_intro.png",
                               "ui/crown_finale/release.png",  "ui/crown_finale/awaken.png"};
        crownFinaleMessages_[index] = std::make_unique<engine::Sprite>();
        const uint32_t texture = engine::Sprite::LoadTexture(paths[index]);
        crownFinaleMessages_[index]->Initialize(spriteCommon_.get(), texture);
        crownFinaleMessages_[index]->SetTextureName(paths[index]);
        crownFinaleMessages_[index]->SetAnchorPoint({0.5f, 0.5f});
    }

    auto& message = crownFinaleMessages_[index];
    message->SetColor({1.0f, 1.0f, 1.0f, alpha});
    const bool movie = crownFinale_.active || arenaBossIntroActive_;

    // 透過文字の実寸比率を維持し、通常時は左下のHPと中央のプレイヤーを避けます。
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(message->GetTextureHandle());
    const float fit = std::min((movie ? 1020.0f : 820.0f) / std::max(1.0f, static_cast<float>(metadata.width)),
                               (movie ? 72.0f : 64.0f) / std::max(1.0f, static_cast<float>(metadata.height)));
    message->SetPosition(SpriteLayoutScaler::ScaleDesignPosition({movie ? 640.0f : 830.0f, movie ? 620.0f : 662.0f}, {1280.0f, 720.0f}));
    message->SetSize(
        SpriteLayoutScaler::ScaleDesignSize({metadata.width * fit, metadata.height * fit}, {1280.0f, 720.0f}));
    message->Update();
    message->Draw();
}
