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
#include "DebrisEffectManager.h"
#include "SpriteLayoutScaler.h"
#include "TextureManager.h"
#include "game/system/CrownFinaleTuning.h"
#include "game/system/CrownBarrageCamera.h"
#include "game/actor/combat/CombatGroundQuery.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
float CrownEase(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vector3 CrownLerp(const Vector3& a, const Vector3& b, float t) { return a + (b - a) * t; }
Vector3 CrownLook(const Vector3& eye, const Vector3& target) {
    const Vector3 d = target - eye;
    return {-std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)), std::atan2(d.x, d.z), 0.0f};
}
} // namespace

// 王冠決着の専用進行です。通常戦闘を止めた後、演出用状態を更新し、終了時にワールドを復元します。

bool GamePlayScene::StartSealBarrageMovie(EnemyFalseKingSlime* boss, const Vector3& strike) {
    if (!SceneManager::GetInstance()->IsPlaying() || !boss || !player_ || player_->isDead ||
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
    const float finish = CrownEase((movie.time - duration + 0.42f) / 0.42f);
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
            CrownEase(movie.time / 0.22f) * (1.0f - CrownEase((movie.time - duration + 0.40f) / 0.40f));
}

void GamePlayScene::FinishSealBarrageMovie() {
    auto& movie = sealBarrageMovie_;
    if (!movie.active)
        return;
    movie.active = false;
    if (auto* params = PostEffect::GetInstance()->GetParams())
        params->cinemaBarHeight = movie.bars;
    if (player_ && !player_->isDead) {
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

bool GamePlayScene::StartCrownFinale(EnemyFalseKingSlime* boss) {
    if (crownFinale_.active || crownFinale_.prepared || !boss || !player_ || player_->isDead ||
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
    auto& s = crownFinale_;
    const auto& tuning = CrownFinaleTuning::Get();
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
    s.eye = camera->GetEye();
    s.target = camera->GetTargetPoint();
    s.fov = camera->GetFovY();
    if (auto* params = PostEffect::GetInstance()->GetParams())
        s.bars = params->cinemaBarHeight;
    player_->BeginCinematicLock();
    s.start = player_->GetTranslate();
    s.playerScale = player_->GetScale();
    s.playerModel = player_->GetModelName(); s.playerColor = player_->GetColor();
    s.playerMaterial = player_->GetMaterialType();
    std::vector<std::unique_ptr<Object3d>> previewArenaCopies;
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
                if ((x <= 1 || x >= 6) && (z == 0 || z == 7)) continue;
                auto chunk = object->Clone();
                if (!chunk) continue;
                char model[128]; std::snprintf(model, sizeof(model), "Stages/high_crown/high_crown_fracture_%02d.obj", i);
                chunk->SetModel(model); chunk->SetName("CrownFracture_" + std::to_string(i));
                chunk->ClearEditorHiddenReasons(); chunk->SetEditorInternal(crownMoviePreview_.active);
                chunk->SetEventID(-1); chunk->SetTargetID(-1); chunk->SetCollisionAttribute(0); chunk->SetCollisionMask(0);
                const Vector3 position = Math::Transform({-35.0f + x * 10.0f, 0.0f, 35.0f - z * 10.0f}, object->GetWorldMatrix());
                chunk->SetTranslate(position); chunk->SetIsVisible(false); chunk->UpdateWorldMatrix(true);
                s.fractureChunks.push_back({chunk.get(), position, chunk->GetRotation(), 0, 0, false});
                s.generatedObjects.push_back(chunk.get()); previewArenaCopies.push_back(std::move(chunk));
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
            if (crownMoviePreview_.active) previewArenaCopies.push_back(std::move(copy));
            else AddObject(std::move(copy));
        }
    }
    for (auto& copy : previewArenaCopies) AddObjectImmediate(std::move(copy));
    // 元モデルから分離した中央装飾を同じ位置へ配置し、破断前後の形状を連続させます。
    auto makeVisual = [&](const char* model) {
        auto visual = std::make_unique<Object3d>();
        visual->Initialize(GetObject3dCommon());
        visual->SetModel(model);
        visual->SetMaterialType(0);
        visual->SetBlendMode(BlendMode::kNone);
        visual->SetColliderType(ColliderType::kNone);
        visual->SetCollisionAttribute(0);
        visual->SetCollisionMask(0);
        visual->SetEmissive(1.0f);
        visual->SetIsVisible(false);
        return visual;
    };
    s.lastSealPosition = boss->GetLastBrokenCrownSealPosition();
    s.lastSeal = makeVisual("Effects/false_king_seal_core");
    s.lastSeal->SetColor({.86f,.32f,1,1});
    s.lastSeal->SetEmissive(1.5f);
    s.fragment = makeVisual("Characters/false_king_slime/false_king_crown_emblem.obj");
    s.fragmentCrack = makeVisual("Characters/false_king_slime/false_king_crown_emblem_crack.obj");
    s.fragmentCrack->SetEmissive(6.0f);
    for (int i = 0; i < 5; ++i)
        s.fragmentChips.push_back(makeVisual("Characters/false_king_slime/false_king_crown_emblem_chip.obj"));
    const auto bossCollider = boss->GetColliderConfig();
    boss->SetModel("Characters/false_king_slime/false_king_slime_without_emblem.obj");
    boss->SetColliderConfig(bossCollider);
    boss->UpdateWorldMatrix(true);
    s.fragmentStart = Math::Transform({0.0f, 3.44f, -1.76f}, boss->GetWorldMatrix());
    const Vector3 towardBoss = s.bossStart - s.start;
    const float planarLength = std::sqrt(towardBoss.x * towardBoss.x + towardBoss.z * towardBoss.z);
    const Vector3 direction = planarLength > 0.01f
                                  ? Vector3{towardBoss.x / planarLength, 0.0f, towardBoss.z / planarLength}
                                  : Vector3{std::sin(s.playerYaw), 0.0f, std::cos(s.playerYaw)};
    const Vector3 sample = s.start + direction * tuning.movieFragmentDistance;
    s.fragmentGround = CombatGroundQuery::FindGroundPoint(sample, player_, {8.0f, 24.0f}, s.start.y - 0.75f);
    s.fragmentGround.y += 0.51f * s.bossScale.y * tuning.movieFragmentScale;
    s.fragment->SetTranslate(s.fragmentStart);
    s.fragment->SetScale(s.bossScale);
    s.fragment->SetRotation(s.bossRotation);
    s.fragment->SetIsVisible(true);
    s.fragment->UpdateWorldMatrix(true);
    // 白い薄い圧力面を高速で広げ、金色の塊が拡大する見え方を避けます。
    s.shockwave = makeVisual("Characters/false_king_slime/false_king_pressure_front.obj");
    s.shockwave->SetBlendMode(BlendMode::kAdd);
    s.shockwave->SetEmissive(3.0f);
    s.wings = makeVisual("Characters/false_king_slime/crown_inheritance_wing_left.obj");
    s.rightWing = makeVisual("Characters/false_king_slime/crown_inheritance_wing_right.obj");
    s.innerRune = makeVisual("Characters/false_king_slime/crown_inheritance_inner_rune.obj");
    s.innerRune->SetEnableLighting(false); s.innerRune->SetBlendMode(BlendMode::kAdd);
    s.goldBody = makeVisual("Characters/slime_crown/slime_crown_body.gltf");
    s.goldBody->SetColor({1,.77f,.25f,1});
    s.regalia = makeVisual("Characters/slime_crown/slime_crown_regalia.gltf");
    s.regalia->SetColor({1,.68f,.12f,1});
    for (Object3d* wing : {s.wings.get(),s.rightWing.get()}) {
        // SetModelで取得した基準材質を保ち、白い羽と金の軸をそれぞれ描きます。
        wing->SetEnableLighting(false); wing->SetEmissive(1.1f);
    }
    s.groundFractures = makeVisual("Characters/false_king_slime/crown_inheritance_ground_fractures.obj");
    s.groundFractures->SetEnableLighting(false);
    for (const auto& pose : s.fractureChunks) {
        const std::string index = pose.object->GetName().substr(std::string("CrownFracture_").size());
        char path[160];std::snprintf(path,sizeof(path),"Characters/false_king_slime/crown_inheritance_cracks_%02d.obj",std::stoi(index));
        // 地割れが交差しない外周の三片は、床だけを落とします。
        if (index=="1" || index=="6" || index=="62") {s.floorCracks.push_back(nullptr);continue;}
        auto crack=makeVisual(path);crack->SetScale(pose.object->GetScale());
        crack->SetEnableLighting(false);s.floorCracks.push_back(std::move(crack));
    }
    camera->EndOverride(0.0f); camera->ClearPresentationLayers();
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetLockOnTarget(nullptr);
    camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    return true;
}

void GamePlayScene::UpdateCrownFinale(float dt) {
    auto& s = crownFinale_;
    if (!s.active) return;
    Camera* camera = crownMoviePreview_.active ? crownMoviePreview_.camera.get() : CameraManager::GetInstance()->GetMainCamera();
    if (!camera || !player_ || !s.boss || player_->isDead) { ResetCrownFinaleWorld(); return; }
    const auto& tuning = CrownFinaleTuning::Get();
    const float previousTime = s.time;
    s.time += std::max(0.0f, dt);
    const float t = std::clamp(s.time / s.duration, 0.0f, 1.0f);
    const float breakTime = tuning.movieFragmentBreakTime, landTime = tuning.movieFragmentLandTime;
    const float rageTime = tuning.movieRageTime, pullTime = tuning.movieFragmentPullTime;
    const float absorbTime = tuning.movieFragmentAbsorbTime, bodyTime = tuning.movieBodyTime;
    const float crownTime = tuning.movieCrownTime, wingsTime = tuning.movieWingsTime;
    const float escapeTime = tuning.movieEscapeTime, arriveTime = tuning.movieArriveTime;
    const float bodyEnd = crownTime - 0.012f, crownEnd = wingsTime - 0.012f;
    const Vector3 destination = s.offset + Vector3{0.0f, 0.82f, 12.0f};
    const float flightYaw = std::atan2(destination.x - s.start.x, destination.z - s.start.z);
    auto crossed = [&](float when) { return previousTime < when * s.duration && s.time >= when * s.duration; };
    auto periodic = [&](float rate) { return dt > 0.0f && static_cast<int>(s.time * rate) != static_cast<int>(previousTime * rate); };

    // 最後の封印の破壊を冒頭で見せ、次の王冠カットに因果関係を持たせます。
    const float sealDisappear=CrownEase((s.time-.08f)/.48f);
    s.lastSeal->SetTranslate(s.lastSealPosition);
    s.lastSeal->SetScale({1,1,1}); s.lastSeal->SetIsVisible(s.time<.58f);
    auto* sealMaterial=s.lastSeal->GetMaterialData();
    sealMaterial->portalClipEnabled=1; sealMaterial->portalClipProgress=0;
    sealMaterial->portalClipCenter=s.lastSealPosition+Vector3{0,3.0f-6.0f*sealDisappear-.52f,0};
    sealMaterial->portalClipNormal={0,1,0};sealMaterial->portalClipEdgeWidth=.10f;
    sealMaterial->portalClipDissolve=.18f;sealMaterial->portalClipColor={1,.85f,1,1};
    if (crossed(.005f)) {
        VFXSequencer::PlayOneShot("pull_catch_cue",s.lastSealPosition,{1.4f,1.4f,1.4f});
        DebrisEffectConfig debris;
        if (DebrisEffectManager::GetInstance()->LoadConfig("Resources/json/debris/false_king_seal_shatter.json",debris)) {
            debris.color={.85f,.30f,1,1};debris.groundY=s.lastSealPosition.y-2;
            DebrisEffectManager::GetInstance()->SpawnFromConfig(debris,s.lastSealPosition);
        }
    }
    // 破断の衝撃と暴走は別の動作です。欠片を地面で見せ終わるまで床は崩しません。
    auto bossPose = [&](float time) {
        const float crack = time < breakTime ? std::sin(time * 320.0f) * CrownEase(time / breakTime) * 0.025f : 0.0f;
        const float rage = CrownEase((time - rageTime + 0.018f) / 0.04f);
        const float throb = time < escapeTime ? std::sin(time * s.duration * 15.0f) * rage * 0.075f : 0.0f;
        return Vector3{s.bossScale.x * (1.0f + rage * 0.08f + throb),
                       s.bossScale.y * (1.0f - crack - rage * 0.09f - throb), s.bossScale.z * (1.0f + rage * 0.08f)};
    };
    s.boss->SetScale(bossPose(t)); s.boss->SetTranslate(s.bossStart); s.boss->UpdateWorldMatrix(false);
    const Vector3 release = Math::Transform({0.0f, 3.44f, -1.76f}, Math::MakeAffineMatrix(bossPose(breakTime), s.bossRotation, s.bossStart));
    const Vector3 towardFragment = s.fragmentGround - s.start;
    const float pullYaw = std::atan2(towardFragment.x, towardFragment.z);
    const Vector3 pullDirection{std::sin(pullYaw), 0.0f, std::cos(pullYaw)};
    const Vector3 mouth = s.start + pullDirection * 0.70f + Vector3{0.0f, 0.22f, 0.0f};
    Vector3 fragmentPosition = release, fragmentRotation = s.bossRotation, fragmentScale = s.bossScale;
    if (t < breakTime) {
        fragmentPosition = Math::Transform({0.0f, 3.44f, -1.76f}, s.boss->GetWorldMatrix()); fragmentScale = bossPose(t);
    } else if (t < landTime) {
        const float fall = (t - breakTime) / (landTime - breakTime);
        fragmentPosition = CrownLerp(release, s.fragmentGround, fall);
        fragmentPosition.y += std::sin(fall * 3.14159265f) * tuning.movieFragmentFallArc;
        fragmentRotation = s.bossRotation + Vector3{std::sin(fall * 3.14159265f) * 0.55f, fall * 6.2831853f, fall * 0.16f};
        fragmentScale = s.bossScale * (1.0f + (tuning.movieFragmentScale - 1.0f) * CrownEase(fall));
    } else if (t < pullTime) {
        const float settle = std::clamp((t - landTime) / 0.04f, 0.0f, 1.0f);
        fragmentPosition = s.fragmentGround + Vector3{0.0f, std::fabs(std::sin(settle * 6.2831853f)) * (1.0f - settle) * 0.25f, 0.0f};
        fragmentRotation = s.bossRotation + Vector3{0.0f, 6.2831853f, 0.16f}; fragmentScale = s.bossScale * tuning.movieFragmentScale;
    } else {
        const float receive = CrownEase((t - pullTime) / (absorbTime - pullTime));
        fragmentPosition = CrownLerp(s.fragmentGround, mouth, receive);
        fragmentPosition.y += std::sin(receive * 3.14159265f) * 0.22f;
        fragmentScale = s.bossScale * (tuning.movieFragmentScale * std::max(0.001f, 1.0f - CrownEase((receive - 0.65f) / 0.35f)));
        fragmentRotation = s.bossRotation + Vector3{0.0f, 6.2831853f + receive * 1.2f, 0.16f};
    }
    s.fragment->SetIsVisible(t < absorbTime); s.fragment->SetTranslate(fragmentPosition);
    s.fragment->SetScale(fragmentScale); s.fragment->SetRotation(fragmentRotation);
    s.fragment->SetEmissive(1.2f + (t >= landTime ? 0.4f + 0.15f * std::sin(s.time * 6.0f) : 0.0f));
    s.fragmentCrack->SetIsVisible(t >= 0.04f && t < breakTime);
    s.fragmentCrack->SetTranslate(fragmentPosition); s.fragmentCrack->SetScale(fragmentScale);
    s.fragmentCrack->SetRotation(fragmentRotation);
    if (crossed(0.04f)) VFXSequencer::PlayOneShot("pull_catch_cue", release, {0.7f,0.7f,0.7f});
    if (t >= breakTime && !s.fragmentBroken) {
        s.fragmentBroken = true; VFXSequencer::PlayOneShot("pull_catch_cue", release, {0.9f,0.9f,0.9f});
    }
    for (size_t i=0; i<s.fragmentChips.size(); ++i) {
        auto& chip=s.fragmentChips[i]; const float age=(t-breakTime)*s.duration, angle=i*2.3999632f;
        chip->SetIsVisible(age>=0.0f && age<1.3f);
        chip->SetTranslate(release + Vector3{std::cos(angle)*age*2.5f,age*(1.6f+i*.18f)-4.9f*age*age,std::sin(angle)*age*2.5f});
        chip->SetScale(s.bossScale);chip->SetRotation({age*3.0f,angle+age*4.0f,age*2.0f});
    }
    if (t>=landTime && !s.fragmentLanded) {
        s.fragmentLanded=true;VFXSequencer::PlayOneShot("pull_catch_cue",s.fragmentGround,{.5f,.5f,.5f});
    }
    if (t>=landTime && t<pullTime && periodic(5))
        GPUParticleManager::GetInstance()->EmitDirected("crown_goal_idle_sparkle",s.fragmentGround,{0,1,0},.4f);

    // 寄りで欠片を確認した後、ボスの圧力面・粉塵・地割れが外周へ走ります。
    const float shock = std::clamp((t-rageTime)/.07f,0.0f,1.0f);
    s.shockwave->SetIsVisible(t>=rageTime && t<rageTime+.07f);
    s.shockwave->SetTranslate(s.bossStart+Vector3{0,.18f,0});
    s.shockwave->SetScale({1+shock*47,.42f+std::sin(shock*3.14159265f)*.7f,1+shock*40});
    s.shockwave->SetColor({1,.91f,.76f,(1-shock)*.85f});
    if (t>=rageTime && !s.impacted) {
        s.impacted=true;VFXSequencer::PlayOneShot("crown_inheritance_rupture_cue",s.bossStart);
    }
    s.groundFractures->SetIsVisible(false);
    s.groundFractures->SetTranslate({0,.89f,0}); s.groundFractures->SetScale({1,1,1});
    s.groundFractures->SetEmissive(1.0f + 1.8f*CrownEase((t-rageTime)/.07f));

    Vector3 playerPosition=s.start, eye{},target{};
    const float notice=CrownEase((t-rageTime-.03f)/(pullTime-rageTime-.03f));
    const float turnDelta=std::atan2(std::sin(pullYaw-s.playerYaw),std::cos(pullYaw-s.playerYaw));
    if (t<pullTime) player_->SetMoveYaw(s.playerYaw+turnDelta*notice);
    if (t>=pullTime && t<absorbTime) {
        const float receive=std::clamp((t-pullTime)/(absorbTime-pullTime),0.0f,1.0f);
        player_->SetMoveYaw(pullYaw);
        player_->StepCinematicPullVisual(dt,towardFragment,receive);
        if (Object3d* marker=player_->GetHookMarker()) {
            const float shot=CrownEase((t-pullTime)/.012f);
            const Vector3 tip=CrownLerp(mouth,fragmentPosition,shot),diff=tip-mouth;
            marker->SetIsVisible(true);marker->SetColor({.10f,.82f,1,1});
            marker->SetTranslate(mouth+diff*.5f);
            marker->SetRotation({std::atan2(-diff.y,std::sqrt(diff.x*diff.x+diff.z*diff.z)),std::atan2(diff.x,diff.z),0});
            marker->SetScale({.10f,.10f,std::max(.01f,Math::Length(diff))}); marker->UpdateWorldMatrix(true);
        }
        if (!s.pullStarted) {s.pullStarted=true;VFXSequencer::PlayOneShot("pull_bind_cue",s.fragmentGround,{.65f,.65f,.65f});}
    }
    if (crossed(absorbTime)) {
        player_->EndCinematicPullVisual();player_->SetScale(s.playerScale);player_->SetMoveYaw(pullYaw);
        VFXSequencer::PlayOneShot("pull_catch_cue",mouth,{.8f,.8f,.8f});
    }

    // 体内の菱形は取り込んだ装飾と同じ形です。身体を透かして模様を読ませます。
    s.innerRune->SetIsVisible(t>=absorbTime && t<crownTime);
    s.innerRune->SetTranslate(s.start);s.innerRune->SetScale(s.playerScale);
    s.innerRune->SetRotation({0,pullYaw,0});s.innerRune->SetColor({1,.8f,.24f,1});
    s.innerRune->SetEmissive(2+2*std::sin(s.time*7)*std::sin(s.time*7));
    if (t>=absorbTime && t<bodyTime) {
        player_->SetMaterialType(1);player_->SetColor({.72f,.95f,1,.48f});
        player_->SetScale(s.playerScale*(1+.04f*std::sin(s.time*11)));
        if (periodic(8)) GPUParticleManager::GetInstance()->EmitDirected("goal_clear_gold_motes",s.start+Vector3{0,.8f,0},{0,1,0},.45f);
    }
    auto formFromBottom=[&](Object3d* object,float progress,float low,float high,bool reverse) {
        auto* material=object->GetMeshRenderer()->GetMaterialData();
        material->portalClipEnabled=progress<.999f?1.0f:0.0f;
        material->portalClipProgress=0;
        const float height=low+(high-low)*progress;
        material->portalClipCenter=s.start+Vector3{0,height+(reverse?.52f:-.52f),0};
        material->portalClipNormal={0,reverse?-1.0f:1.0f,0};
        material->portalClipEdgeWidth=.035f;material->portalClipDissolve=.055f;
        material->portalClipColor={1,.85f,.3f,1};
    };
    const float bodyProgress=CrownEase((t-bodyTime)/(bodyEnd-bodyTime));
    s.goldBody->SetIsVisible(t>=bodyTime && t<bodyEnd);
    s.goldBody->SetTranslate(s.start);s.goldBody->SetScale(s.playerScale*1.002f);s.goldBody->SetRotation(player_->GetRotation());
    formFromBottom(s.goldBody.get(),bodyProgress,-.1f,1.65f,false);
    if (t>=bodyTime && t<bodyEnd) {
        player_->SetScale(s.playerScale);player_->SetMaterialType(25);player_->SetColor(s.playerColor);
        formFromBottom(player_,bodyProgress,-.1f,1.65f,true);
    }
    if (t>=bodyEnd && !s.bodyFormed) {
        s.bodyFormed=true;player_->GetMeshRenderer()->GetMaterialData()->portalClipEnabled=0;
        player_->SetModel("Characters/slime_crown/slime_crown_body.gltf");player_->SetMaterialType(0);player_->SetColor({1,.77f,.25f,1});
    }
    if (crossed(bodyTime)) VFXSequencer::PlayOneShot("crown_get_cue",s.start,{.65f,.65f,.65f});
    const float crownProgress=CrownEase((t-crownTime)/(crownEnd-crownTime));
    s.regalia->SetIsVisible(t>=crownTime && t<crownEnd);
    s.regalia->SetTranslate(s.start);s.regalia->SetScale(s.playerScale);s.regalia->SetRotation(player_->GetRotation());
    formFromBottom(s.regalia.get(),crownProgress,0,2.2f,false);
    if (t>=crownTime && !s.crownStarted) {
        s.crownStarted=true;VFXSequencer::PlayOneShot("crown_get_cue",s.start+Vector3{0,1.5f,0},{.45f,.45f,.45f});
    }
    // 能力付与は見た目の形成完了と一致させ、初めから装備済みに見えないようにします。
    if (t>=crownEnd && !s.granted) {
        s.granted=player_->ApplyStoredCopy("Crown",true);
        if (!s.granted) {ResetCrownFinaleWorld();return;}
        player_->SetScale(s.playerScale);player_->SetMoveYaw(pullYaw);
    }
    if (t>=wingsTime && !s.wingsStarted) {
        s.wingsStarted=true;VFXSequencer::PlayOneShot("crown_get_cue",s.start+Vector3{0,1,0},{.45f,.45f,.45f});
    }
    const float wingOpen=CrownEase((t-wingsTime)/(escapeTime-wingsTime-.02f));
    const float wingFold=1-CrownEase((t-arriveTime)/std::max(.02f,1-arriveTime));
    if (t>=wingsTime && t<escapeTime) {
        const float anticipation=CrownEase((t-escapeTime+.025f)/.025f);
        const float turn=CrownEase((t-wingsTime)/(escapeTime-wingsTime));
        player_->SetMoveYaw(pullYaw+std::atan2(std::sin(flightYaw-pullYaw),std::cos(flightYaw-pullYaw))*turn);
        player_->SetScale({s.playerScale.x*(1+anticipation*.20f),s.playerScale.y*(1-anticipation*.30f),s.playerScale.z});
    }
    if (t>=escapeTime && t<arriveTime) {
        const float travel=(t-escapeTime)/(arriveTime-escapeTime);
        playerPosition=CrownLerp(s.start,destination,travel);
        playerPosition.y+=std::sin(travel*3.14159265f)*tuning.escapeHeight;
        const float stretch=std::exp(-travel*18);
        player_->SetScale({s.playerScale.x*(1-stretch*.14f),s.playerScale.y*(1+stretch*.24f),s.playerScale.z});
        player_->SetMoveYaw(flightYaw);player_->SetRotation({-.25f*(1-2*travel),flightYaw+player_->GetVisualYawOffset(),std::sin(travel*6.2831853f)*.12f});
        if (!s.launched) {s.launched=true;VFXSequencer::PlayOneShot("crown_victory_land_cue",s.start);}
        if (periodic(15)) GPUParticleManager::GetInstance()->EmitDirected("false_king_rush_wake",playerPosition,{-std::sin(flightYaw),0,-std::cos(flightYaw)},.75f);
    } else if (t>=arriveTime) {
        playerPosition=destination;
        const float settle=CrownEase((t-arriveTime)/.035f),squash=(1-settle)*std::cos(settle*4.712389f);
        player_->SetScale({s.playerScale.x*(1+squash*.23f),s.playerScale.y*(1-squash*.30f),s.playerScale.z*(1+squash*.18f)});
        player_->SetMoveYaw(3.14159265f);player_->SetRotation({0,3.14159265f+player_->GetVisualYawOffset(),0});
        if (!s.landed) {s.landed=true;VFXSequencer::PlayOneShot("crown_victory_land_cue",destination);}
        s.boss->SetTranslate(s.offset+Vector3{0,.82f+(1-CrownEase((t-arriveTime)/.065f))*8,-12});
    }
    player_->SetTranslate(playerPosition);player_->SetVelocity({});player_->UpdateWorldMatrix(false);
    const float flap=t>=escapeTime && t<arriveTime?std::sin((t-escapeTime)*s.duration*9):0;
    const float yaw=player_->GetRotation().y;
    for (Object3d* wing:{s.wings.get(),s.rightWing.get()}) {
        wing->SetIsVisible(t>=wingsTime && wingFold>.001f);
        wing->SetTranslate(playerPosition+Vector3{0,.35f,0});
        wing->SetScale(s.playerScale*(tuning.movieWingScale*wingFold));
        const float side=wing==s.wings.get()?-1.0f:1.0f;
        wing->SetRotation({0,yaw,side*((1-wingOpen)*1.15f+flap*.22f)});
        wing->SetEmissive(1.0f+(1-wingOpen)*1.6f);
        auto* mat=wing->GetMeshRenderer()->GetMaterialData();
        mat->portalClipEnabled=wingOpen<.999f?1.0f:0.0f;mat->portalClipProgress=0;
        mat->portalClipCenter=playerPosition+Vector3{0,.35f+(-.1f+2.9f*wingOpen)-.52f,0};
        mat->portalClipNormal={0,1,0};mat->portalClipEdgeWidth=.04f;mat->portalClipDissolve=.06f;mat->portalClipColor={1,.9f,.4f,1};
    }

    // 表示メッシュを実際に分割した床片です。外周から割れ、最後に足元が抜けます。
    size_t crackIndex=0;
    for (auto& pose:s.fractureChunks) {
        const Vector3 delta=pose.position-s.start;
        const float distance=std::sqrt(delta.x*delta.x+delta.z*delta.z);
        const float delay=1-std::clamp(distance/48.0f,0.0f,1.0f);
        const bool supportsPlayer=std::abs(delta.x)<=5.1f && std::abs(delta.z)<=5.1f;
        const float begin=supportsPlayer?escapeTime+.004f:rageTime+.02f+delay*(escapeTime-rageTime-.01f);
        const float age=std::max(0.0f,(t-begin)*s.duration);
        pose.object->SetIsVisible(t>=rageTime && age<4.0f);
        pose.object->SetTranslate(pose.position+Vector3{std::sin(age*2+pose.position.x)*age*.6f,-4.9f*age*age,0});
        pose.object->SetRotation(pose.rotation+Vector3{age*.18f,age*.04f,age*.13f});pose.object->UpdateWorldMatrix(false);
        // 床の姿勢を共有し、亀裂の発光だけを衝撃後に減衰させます。
        auto& crack=s.floorCracks[crackIndex++];
        if (crack) {crack->SetTranslate(pose.object->GetTranslate());crack->SetRotation(pose.object->GetRotation());
        crack->SetIsVisible(pose.object->GetIsVisible());
        crack->SetEmissive(1.0f+1.5f*std::exp(-(t-rageTime)*12));}
        if (crossed(begin) && distance>10) VFXSequencer::PlayOneShot("crown_inheritance_floor_break_cue",pose.position+Vector3{0,.8f,0});
    }
    size_t index=0;
    for (auto& pose:s.originalObjects) {
        const std::string& name=pose.object->GetName();
        if (name=="Stage3_HighCrown_VictoryCrown") continue;
        if (name=="Stage3_HighCrown_ArenaVisual") {pose.object->SetIsVisible(t<rageTime);continue;}
        const bool floor=name.find("ArenaFloor_")!=std::string::npos;
        const float begin=floor?escapeTime+.005f:rageTime+.02f+(index++%9)*.012f;
        const float age=std::max(0.0f,(t-begin)*s.duration);
        pose.object->SetTranslate(pose.position+Vector3{0,-4.9f*age*age,0});
        pose.object->SetRotation(pose.rotation+Vector3{age*.07f,0,age*.055f});
        if (age>0) {pose.object->SetCollisionAttribute(0);pose.object->SetCollisionMask(0);}
        pose.object->UpdateWorldMatrix(false);
    }

    // カットごとに主役を一つに絞る。欠片の着地を見せてから暴走の全景へ切り替えます。
    if (t<.035f) {target=s.lastSealPosition;eye=target+Vector3{3.2f,1.0f,5.5f};}
    else if (t<breakTime) {target=fragmentPosition;eye=target+Vector3{2.4f,.7f,4.8f}*tuning.movieFragmentCameraDistance;}
    else if (t<landTime) {
        const float follow=CrownEase((t-breakTime)/(landTime-breakTime));target=fragmentPosition;
        eye=target+CrownLerp({2.4f,.7f,4.8f},{2.6f,1.2f,4.2f},follow)*tuning.movieFragmentCameraDistance;
    } else if (t<rageTime) {target=s.fragmentGround;eye=target+Vector3{2.5f,1,4.0f}*tuning.movieFragmentCameraDistance;}
    else if (t<rageTime+.04f) {target=s.bossStart+Vector3{0,1.8f,0};eye=target+Vector3{13,7,24};}
    else if (t<pullTime) {target=CrownLerp(s.start+Vector3{0,.8f,0},s.fragmentGround,.45f);eye=target+Vector3{5,2.6f,7};}
    else if (t<absorbTime) {target=CrownLerp(mouth,s.fragmentGround,.38f);eye=target+Vector3{4.0f,1.8f,5.8f}*tuning.movieFragmentCameraDistance;}
    else if (t<wingsTime) {
        target=s.start+Vector3{0,1,0};const Vector3 front{std::sin(pullYaw),0,std::cos(pullYaw)};
        eye=target+front*5.5f+Vector3{1.6f,1.1f,0};
    } else if (t<escapeTime) {target=s.start+Vector3{0,1.3f,0};eye=target+Vector3{-7,3,9};}
    else if (t<arriveTime) {
        target=playerPosition+Vector3{0,.9f,0};
        const float travel=(t-escapeTime)/(arriveTime-escapeTime);
        eye=target+CrownLerp({-14,7,24},{-9,5,15},CrownEase(travel));
    } else {target=CrownLerp(destination+Vector3{0,1.2f,0},s.offset+Vector3{0,3,0},.5f*CrownEase((t-arriveTime)/(1-arriveTime)));eye=target+Vector3{8,10,32};}
    const float impact=t>=rageTime?.25f*std::exp(-(t-rageTime)*48):0;
    const float tremor=t>=rageTime && t<escapeTime?.055f:0;
    const float shake=(impact+tremor)*tuning.movieShake;
    const Vector3 jitter{std::sin(s.time*47)*shake,std::sin(s.time*61)*shake*.65f,0};
    eye=eye+jitter;target=target+jitter*.35f;
    camera->SetFollowTarget(nullptr);camera->SetLockOnTarget(nullptr);camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->SetEye(eye);camera->SetTarget(target);camera->SetRotation(CrownLook(eye,target));
    camera->ConfigFixedPoint(eye,CrownLook(eye,target));camera->SetFovY(.58f);camera->SetInputEnabled(false);
    // 編集カメラのOverride補間を経由せず、評価した注視点をそのまま行列へ反映します。
    camera->ApplyReplayView(eye,target,CrownLook(eye,target),.58f,camera->GetNearClip(),camera->GetFarClip());
    CameraManager::GetInstance()->SetActiveCamera(camera);
    // 通常Updateもカメラ変更前にモデルを更新していました。最後に全描画定数を揃えます。
    for (auto& object:GetObjects()) if (object && !object->IsReplayRemoved()) object->UpdateWorldMatrix(true);
    if (Object3d* marker=player_->GetHookMarker()) marker->UpdateWorldMatrix(true);
    if (auto* params=PostEffect::GetInstance()->GetParams())
        params->cinemaBarHeight=s.bars+.075f*CrownEase(t/.04f)*(1-CrownEase((t-.96f)/.04f));
    if (t>=1 && !crownMoviePreview_.active) FinishCrownFinale(true);
}


void GamePlayScene::FinishCrownFinale(bool completed) {
    auto& s = crownFinale_;
    if (!s.active)
        return;
    s.active = false;
    if (auto* params = PostEffect::GetInstance()->GetParams())
        params->cinemaBarHeight = s.bars;
    if (player_) {
        player_->EndCinematicLock(s.hadControl);
        if (!completed)
            player_->SetMoveYaw(s.playerYaw);
        player_->SetScale(s.playerScale);
        auto rotation = player_->GetRotation();
        rotation.x = rotation.z = 0.0f;
        player_->SetRotation(rotation);
        player_->SetTranslate(completed ? s.offset + Vector3{0.0f, 0.82f, 12.0f} : s.start);
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
        s.boss->BeginFinalArena(s.offset + Vector3{0.0f, 0.82f, -12.0f});
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
        const auto collider = s.boss->GetColliderConfig();
        s.boss->SetModel(s.bossModel);
        s.boss->SetColliderConfig(collider);
        s.boss->SetScale(s.bossScale);
        s.boss->SetRotation(s.bossRotation);
        s.boss->SetTranslate(s.bossStart);
        if (!crownMoviePreview_.active) s.boss->AbortCrownFinale();
    }
    if (player_) {
        player_->EndCinematicPullVisual();
        player_->GetMeshRenderer()->GetMaterialData()->portalClipEnabled = 0.0f;
        if (!completed) {
            player_->ClearCrownPower(); player_->SetModel(s.playerModel);
            player_->SetMaterialType(s.playerMaterial); player_->SetColor(s.playerColor);
        }
    }
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetFovY(s.fov);
        camera->SetFollowTarget(player_);
        camera->SetLockOnTarget(nullptr);
        camera->SetFollowMode(Camera::FollowMode::kAimable);
        camera->SetRotation({0.24f, 3.14159265f, 0.0f});
        camera->SnapToThirdPerson(18.0f, 2.5f, 0.24f);
        camera->SetInputEnabled(true);
    }
    CameraManager::GetInstance()->SetActiveCamera(nullptr);
    s.fragment.reset();
    s.fragmentCrack.reset(); s.lastSeal.reset();
    s.fragmentChips.clear();
    s.shockwave.reset();
    s.wings.reset(); s.rightWing.reset(); s.innerRune.reset(); s.goldBody.reset();
    s.regalia.reset(); s.groundFractures.reset(); s.floorCracks.clear();
}

void GamePlayScene::ResetCrownFinaleWorld() {
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
            object->SetIsVisible(false); object->SetReplayRemoved(true);
        }
        RequestRemoveObject(object);
    }
    s = CrownFinaleState{};
}

void GamePlayScene::DrawCrownFinaleWorld() {
    if (!crownFinale_.active)
        return;
    for (auto* visual : {crownFinale_.fragment.get(), crownFinale_.fragmentCrack.get(), crownFinale_.lastSeal.get(),
                         crownFinale_.shockwave.get(), crownFinale_.wings.get(), crownFinale_.rightWing.get(),
                         crownFinale_.innerRune.get(), crownFinale_.goldBody.get(), crownFinale_.regalia.get(),
                         crownFinale_.groundFractures.get()}) {
        if (!visual || !visual->GetIsVisible())
            continue;
        visual->UpdateWorldMatrix(true);
        visual->Draw(pointLightResource_.Get(), spotLightResource_.Get());
    }
    for (const auto& crack : crownFinale_.floorCracks) {
        if (!crack || !crack->GetIsVisible()) continue;
        crack->UpdateWorldMatrix(true);crack->Draw(pointLightResource_.Get(),spotLightResource_.Get());
    }
    for (const auto& chip : crownFinale_.fragmentChips) {
        if (chip && chip->GetIsVisible()) {
            chip->UpdateWorldMatrix(true);
            chip->Draw(pointLightResource_.Get(), spotLightResource_.Get());
        }
    }
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
        alpha = CrownEase((arenaBossIntroTimer_ - 2.7f) / 0.35f) *
                (1.0f - CrownEase((arenaBossIntroTimer_ - 5.1f) / 0.35f));
    } else if (crownFinale_.active) {
        const float t = crownFinale_.time / crownFinale_.duration;
        const auto& tuning = CrownFinaleTuning::Get();
        index = t < tuning.movieFragmentPullTime ? 4 : (t < tuning.movieFragmentAbsorbTime ? 1 : 5);
        const float begin = index == 4 ? 0.0f : (index == 1 ? tuning.movieFragmentPullTime : tuning.movieFragmentAbsorbTime);
        const float end = index == 4 ? tuning.movieFragmentPullTime : (index == 1 ? tuning.movieFragmentAbsorbTime : tuning.movieEscapeTime);
        alpha = CrownEase((t - begin) / 0.025f) * (1.0f - CrownEase((t - end + 0.025f) / 0.025f));
    }
    if (index < 0)
        return;
    if (alpha <= 0.001f)
        return;
    if (!crownFinaleMessages_[index]) {
        const char* paths[] = {"ui/crown_finale/seals.png",    "ui/crown_finale/inherit.png",
                               "ui/crown_finale/controls.png", "ui/crown_finale/boss_intro.png",
                               "ui/crown_finale/release.png",  "ui/crown_finale/awaken.png"};
        crownFinaleMessages_[index] = std::make_unique<Sprite>();
        const uint32_t texture = Sprite::LoadTexture(paths[index]);
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
