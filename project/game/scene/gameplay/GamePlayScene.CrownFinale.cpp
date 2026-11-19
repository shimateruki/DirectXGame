#include "game/actor/enemy/boss/false_king/BossIntroSettings.h"
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
#include "VfxSequencer.h"
#include "GpuParticleManager.h"
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
#include <utility>
#include "game/system/scene/ObjectManager.h"

namespace {
constexpr const char* kCrownFinalePath = "Resources/json/scenario/stage3/crown_finale.json";
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
    // 操作や視点を別の演出が所有している場合は、封印失敗の固定視点を重ねません。
    if (!SceneManager::GetInstance()->IsPlaying() || !boss || !player_ || player_->IsDead() ||
        player_->IsCinematicLocked() || crownFinale_.active || sealBarrageMovie_.active ||
        isGoal_)
        return false;
    engine::Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (!camera)
        return false;

    // 被弾後も同じ画角と追従先へ戻せるよう、操作を止める前に状態を保存します。
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
    camera->SetFollowMode(engine::Camera::FollowMode::FixedPoint);
    return true;
}

void GamePlayScene::UpdateSealBarrageMovie(float deltaTime) {
    // 封印失敗時の専用カメラを進め、通常の戦闘カメラと切り分ける。
    auto& movie = sealBarrageMovie_;
    if (!movie.active)
        return;
    engine::Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    // 必要な対象を失った場合や続く継承演出が始まった場合は、固定視点を解除します。
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
    // 槍雨のカメラ姿勢を時刻で評価し、終端直前の0.42秒で元の構図へ寄せます。
    auto pose = EvaluateCrownBarrageCamera(movie.time, movie.boss, movie.strike, CrownFinaleTuning::Get());
    const float finish = Easing::SmoothStep01((movie.time - duration + 0.42f) / 0.42f);
    pose.eye = CrownLerp(pose.eye, movie.eye, finish);
    pose.target = CrownLerp(pose.target, movie.target, finish);
    pose.fov += (movie.fov - pose.fov) * finish;
    camera->SetInputEnabled(false);
    camera->SetFollowTarget(nullptr);
    camera->SetFollowMode(engine::Camera::FollowMode::FixedPoint);
    camera->SetEye(pose.eye);
    camera->SetTarget(pose.target);
    camera->SetRotation(CrownLook(pose.eye, pose.target));
    camera->ConfigFixedPoint(pose.eye, CrownLook(pose.eye, pose.target));
    camera->SetFovY(pose.fov);
    camera->Update();
    // 中盤だけ黒帯を広げ、元の設定高を基準に導入と終端で補間します。
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
    // 撃破されたプレイヤーへ操作を返さず、生存時にだけ保存した制御状態を戻します。
    if (player_ && !player_->IsDead()) {
        player_->SetVelocity({});
        player_->SetIsControlActive(movie.hadControl);
    }

    // 演出中の固定視点を解除し、以前の画角・追従先・姿勢をまとめて戻します。
    if (engine::Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
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
    // ゲーム用の調整値を基準とし、有効なタイムラインのマーカー秒だけを全尺に対する割合に直します。
    auto tuning = CrownFinaleTuning::Get();
    const auto* sequence = crownFinalePlayer_.GetSequence();
    if (!sequence || sequence->duration <= 0) return tuning;
    tuning.movieDuration = sequence->GetAuthoredDuration();
    for (const auto& [name, member] : kFinaleMarkers)
        tuning.*member = sequence->GetSignalTime(name, tuning.*member * tuning.movieDuration) / tuning.movieDuration;
    return tuning;
}

void GamePlayScene::RequestCrownFinale(EnemyFalseKingSlime* boss) {
    // 同じボスからの要求を積み増さず、更新末尾の安全な開始処理に預けます。
    if (!pendingCrownFinaleBoss_ && !crownFinale_.prepared)
        pendingCrownFinaleBoss_ = boss;
}

void GamePlayScene::StartPendingCrownFinale() {
    auto* boss = std::exchange(pendingCrownFinaleBoss_, nullptr);
    if (!boss)
        return;

    // 更新の末尾で削除済みのObjectには触れない。
    const auto& objects = GetObjects();
    const auto owned = std::find_if(objects.begin(), objects.end(), [boss](const auto& object) {
        return object.get() == boss && !object->IsReplayRemoved();
    });
    if (owned == objects.end() || boss->IsDead())
        return;

    if (StartCrownFinale(boss))
        boss->OnCrownFinaleStarted();

    // 最終闘技場の通常追加と、開始失敗時の削除予約も同フレームで反映する。
    objectManager_->ApplyPendingChanges();
}

bool GamePlayScene::StartCrownFinale(EnemyFalseKingSlime* boss) {
    if (crownFinale_.active || crownFinale_.prepared || !boss || !player_ || player_->IsDead() ||
        player_->IsCinematicLocked() || isGoal_)
        return false;
    engine::Camera* camera = crownMoviePreview_.active ? crownMoviePreview_.camera.get() : CameraManager::GetInstance()->GetMainCamera();
    if (!camera)
        return false;

    // 必須の足場を確認してから生成。攻撃プレビュー等では脱出を開始しません。
    bool hasFloor = false, hasArena = false;

    for (const auto& object : GetObjects()) {
        hasFloor |= object->GetName().find("Stage3_HighCrown_ArenaFloor_") != std::string::npos;
        hasArena |= object->GetName() == "Stage3_HighCrown_ArenaVisual";
    }

    // 必要な場と演出素材を確認し、床や役者を変える前に不足を検出します。
    if (!hasFloor || !hasArena)
        return false;
    if (!crownFinalePlayer_.Load(kCrownFinalePath, SceneManager::GetInstance()))
        return false;
    const auto* sequence = crownFinalePlayer_.GetSequence();
    const auto& clips = sequence->presentation.objects;
    const auto fragmentClip = std::find_if(clips.begin(), clips.end(), [](const auto& clip) { return clip.name == "fragment"; });
    if (fragmentClip == clips.end() || (fragmentClip->keys.empty() && fragmentClip->motions.empty())) return false;
    if (std::none_of(clips.begin(),clips.end(),[](const auto& clip) {
        return clip.name=="player_flight" && clip.enabled && !clip.motions.empty();
    })) return false;
    auto& s = crownFinale_;
    const auto tuning = GetCrownFinaleTiming();
    // 編集した節目が欠けたり逆転した場合は、世界を変更する前に停止する。
    float previous = 0;
    for (const auto& [name, member] : kFinaleMarkers) {
        const float time = sequence->GetSignalTime(name, -1) / tuning.movieDuration;
        const float gap = member == &CrownFinaleTuning::movieFragmentPullTime ? .03f :
            (member == &CrownFinaleTuning::movieCrownTime || member == &CrownFinaleTuning::movieWingsTime ? .012f : 0);
        if (!(time > previous + gap && time < 1)) {
            LOG_ERROR("Crown inheritance: invalid required marker {}", name);
            return false;
        }
        previous = time;
    }
    // ここから先は中断時に元へ戻せるよう、操作・姿勢・材質・画角を保存してからロックします。
    s.active = s.prepared = true;
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
    std::vector<std::unique_ptr<engine::Object3D>> previewArenaCopies;

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

        // 床崩壊に使う分割モデルを、元の闘技場のワールド座標へ並べて非表示で用意します。
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
                chunk->SetEventId(-1);
                chunk->SetTargetId(-1);
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

        // 飛行先に移動した後も床に立てるよう、必要な闘技場部品を移動先へ複製します。
        if (floor || arena || foundation) {
            auto copy = object->Clone();
            if (!copy)
                continue;
            copy->SetName("CrownFinalArena_" + name);

            if (crownMoviePreview_.active) {
                copy->SetEditorInternal(true);
                copy->SetIsLocked(true);
            }

            copy->SetEventId(-1);
            copy->SetTargetId(-1);
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

    // 最後の封印とボス胸元を基準に、欠片の出発点と接地位置を先に計算します。
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
    // 接地時の大きさも見た目キーから取り、床に埋まらない高さへ合わせる。
    const auto resting = engine::SampleCinematicVisual(*fragmentClip,
        tuning.movieFragmentLandTime*s.duration,sequence);
    s.fragmentGround.y += 0.51f * s.bossScale.y * resting.scale.y;
    // 装飾・追従・カメラは同じDirector素材から再生し、ゲーム固有の位置だけ接続する。
    crownFinalePlayer_.SetBindingResolver([this](const CinematicObjectBinding& binding) -> engine::Object3D* {
        if (binding.targetName == "boss" || binding.targetName == "$Actor" || binding.targetName == "boss_landing") return crownFinale_.boss;
        if (binding.targetName == "player" || binding.targetName == "$Gate" || binding.targetName == "player_flight" || binding.targetName == "player_absorb" || binding.targetName == "player_anticipation") return player_;
        return CinematicPlayer::ResolveSceneTarget(this, binding);
    });
    crownFinalePlayer_.SetPositionResolver([this](const CinematicObjectBinding& binding)
        -> std::optional<engine::Vector3> {
        const auto& state = crownFinale_;
        if (binding.targetName == "fragment_ground") return state.fragmentGround;
        if (binding.targetName == "fragment_release") return state.fragmentStart;
        if (binding.targetName == "fragment_attached" && state.boss)
            return engine::Math::Transform({0.0f,3.44f,-1.76f},state.boss->GetWorldMatrix());
        if (binding.targetName == "seal_position") return state.lastSealPosition;
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
    // 再生用オブジェクトの初期化に失敗したら、すでに準備した配置と操作状態を復元します。
    if (!crownFinalePlayer_.Prepare()) {
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
    crownFinalePlayer_.Play(false, &s.time);
    if (!crownFinalePlayer_.IsPlaying()) {
        ResetCrownFinaleWorld();
        return false;
    }
    UpdateCrownFinale(0);
    return true;
}

void GamePlayScene::UpdateCrownFinale(float frameDeltaTime, std::optional<float> seekSeconds) {
    auto& s = crownFinale_;
    if (!s.active)
        return;
    // 編集確認では専用カメラを使う。通常プレイはシーンのメインカメラで続行する。
    engine::Camera* camera = crownMoviePreview_.active ? crownMoviePreview_.camera.get() : CameraManager::GetInstance()->GetMainCamera();

    // 必要な対象を失った場合は、再生を進める前にカメラやシーンの状態を戻す。
    if (!camera || !player_ || !s.boss || player_->IsDead()) {
        ResetCrownFinaleWorld();
        return;
    }
    // 時刻は再生器だけが進める。準備処理が失敗した場合はVFXも評価しない。
    bool prepared = false;
    const auto prepare = [&](float previous, float current) {
        prepared = PrepareCrownFinaleFrame(seekSeconds ? current : previous, current, frameDeltaTime);
        return prepared;
    };
    // 時刻指定はそのフレームの姿勢を確認する経路で、通常再生では経過時間の分だけ進める。
    if (seekSeconds)
        crownFinalePlayer_.SetTime(*seekSeconds, false, prepare);
    else
        crownFinalePlayer_.Step(frameDeltaTime, prepare);
    if (!prepared) return;

    // 再生器の時刻から進行率を求め、王冠の破片を本体に合わせる区間と終了時刻の判定に使う。
    const float t = std::clamp(s.time.Elapsed() / s.duration, 0.0f, 1.0f);
    const float breakTime = GetCrownFinaleTiming().movieFragmentBreakTime;
    if (!crownFinalePlayer_.IsPlaying() && !crownFinalePlayer_.IsPreviewing()) {
        ResetCrownFinaleWorld();
        return;
    }

    // 外れる前だけは本体の変形を継承する。外れた後の拡縮は見た目キーが担当する。
    if (t < breakTime) {
        s.fragment->SetScale(s.boss->GetScale());
        s.fragment->UpdateWorldMatrix(true);
    }

    // 通常Updateもカメラ変更前にモデルを更新していました。最後に全描画定数を揃えます。
    for (auto& object:GetObjects())
        if (object && !object->IsReplayRemoved())
            object->UpdateWorldMatrix(true);
    if (engine::Object3D* marker=player_->GetHookMarker())
        marker->UpdateWorldMatrix(true);
    // 本編だけ終了を確定する。プレビューは最後のフレームを確認できるように残す。
    if (t>=1 && !crownMoviePreview_.active)
        FinishCrownFinale(true);
}

bool GamePlayScene::PrepareCrownFinaleFrame(float previousTime, float timeSeconds, float frameDeltaTime) {
    auto& s = crownFinale_;
    const auto tuning = GetCrownFinaleTiming();
    // 節目の設定は全尺に対する0〜1の比率。Directorへ姿勢を問い合わせるときは秒を渡す。
    const float t = std::clamp(timeSeconds / s.duration, 0.0f, 1.0f);
    const float rageTime = tuning.movieRageTime, pullTime = tuning.movieFragmentPullTime;
    const float absorbTime = tuning.movieFragmentAbsorbTime, bodyTime = tuning.movieBodyTime;
    const float crownTime = tuning.movieCrownTime, wingsTime = tuning.movieWingsTime;
    const float escapeTime = tuning.movieEscapeTime, arriveTime = tuning.movieArriveTime;
    const float bodyEnd = crownTime - 0.012f, crownEnd = wingsTime - 0.012f;
    const engine::Vector3 destination = s.offset + engine::Vector3{0.0f, 0.82f, 12.0f};
    const float flightYaw = std::atan2(destination.x - s.start.x, destination.z - s.start.z);
    // 前回と今回の秒数の間に節目が入ったかを調べ、フレームが飛んでも一度だけ起こす。
    auto crossed = [&](float when) { return previousTime < when * s.duration && timeSeconds >= when * s.duration; };

    // 本体の震え・膨張・下降は、Directorの区間設定から同じ時刻で評価する。
    if (!crownFinalePlayer_.ApplyObjectTransform("boss_landing", timeSeconds)) {
        ResetCrownFinaleWorld();
        return false;
    }
    s.boss->UpdateWorldMatrix(false);
    const engine::Vector3 towardFragment = s.fragmentGround - s.start;
    const float pullYaw = std::atan2(towardFragment.x, towardFragment.z);
    const engine::Vector3 pullDirection{std::sin(pullYaw), 0.0f, std::cos(pullYaw)};
    const engine::Vector3 mouth = s.start + pullDirection * 0.70f + engine::Vector3{0.0f, 0.22f, 0.0f};
    // 腕の先端と描画で同じ時刻の位置を使い、評価順による一フレームの遅れを防ぐ。
    const auto fragmentPosition = crownFinalePlayer_.SampleObjectPosition("fragment", timeSeconds);
    if (!fragmentPosition) {
        ResetCrownFinaleWorld();
        return false;
    }

    // プレイヤーの向きを先に欠片へ寄せ、引き寄せ区間でだけ操作用のフック表示を使います。
    engine::Vector3 playerPosition=s.start;
    const float notice=Easing::SmoothStep01((t-rageTime-.03f)/(pullTime-rageTime-.03f));
    // sinとcosから角度差を±πへ折り返し、欠片を向くまでの最短回転を求める。
    const float turnDelta=std::atan2(std::sin(pullYaw-s.playerYaw),std::cos(pullYaw-s.playerYaw));
    if (t<pullTime)
        player_->SetMoveYaw(s.playerYaw+turnDelta*notice);
    if (t>=pullTime && t<absorbTime) {
        const float receive=std::clamp((t-pullTime)/(absorbTime-pullTime),0.0f,1.0f);
        player_->SetMoveYaw(pullYaw);
        player_->StepCinematicPullVisual(frameDeltaTime,towardFragment,receive);

        if (engine::Object3D* marker=player_->GetHookMarker()) {
            const float shot=Easing::SmoothStep01((t-pullTime)/.012f);
            const engine::Vector3 tip=CrownLerp(mouth,*fragmentPosition,shot),diff=tip-mouth;
            marker->SetIsVisible(true);
            marker->SetColor({.10f,.82f,1,1});
            // 中心原点のマーカーを口と先端の中点に置き、Z方向の長さをその距離に合わせる。
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
    }

    if (t>=absorbTime && t<bodyTime) {
        player_->SetMaterialType(1);
        player_->SetColor({.72f,.95f,1,.48f});
        if (!crownFinalePlayer_.ApplyObjectTransform("player_absorb", timeSeconds)) {
            ResetCrownFinaleWorld();
            return false;
        }


    }

    // 実Playerの置換面だけを設定する。金色の仮ボディは共通再生器が逆側から形成する。
    if (t>=bodyTime && t<bodyEnd) {
        const float progress = Easing::SmoothStep01((t-bodyTime)/(bodyEnd-bodyTime));
        player_->SetScale(s.playerScale);
        player_->SetMaterialType(25);
        player_->SetColor(s.playerColor);
        engine::Object3D::PortalClipSettings clipSettings;
        clipSettings.enabled = progress < .999f;
        clipSettings.progress = 0;
        // 開始位置を基準に切断面を1.75ワールド単位上げ、元の体を下から順に隠す。
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
            return false;
        }
        player_->SetScale(s.playerScale);
        player_->SetMoveYaw(pullYaw);
    }

    // 羽の出現と飛び出しは別の時刻にし、発進前に向きとつぶれを作ります。

    if (t>=wingsTime && t<escapeTime) {
        const float turn=Easing::SmoothStep01((t-wingsTime)/(escapeTime-wingsTime));
        player_->SetMoveYaw(pullYaw+std::atan2(std::sin(flightYaw-pullYaw),std::cos(flightYaw-pullYaw))*turn);
        if (!crownFinalePlayer_.ApplyObjectTransform("player_anticipation", timeSeconds)) {
            ResetCrownFinaleWorld();
            return false;
        }
    }

    // 操作用の向きだけを決め、飛行・着地の姿勢はDirectorの区間設定へ任せる。
    if (t>=escapeTime) {
        player_->SetMoveYaw(t<arriveTime ? flightYaw : std::numbers::pi_v<float>);
        if (!crownFinalePlayer_.ApplyObjectTransform("player_flight",timeSeconds)) {
            ResetCrownFinaleWorld();
            return false;
        }
        playerPosition=player_->GetTranslate();

    }
    if (t>=arriveTime) {
        // 着地の移動とVFXはDirectorへ任せ、完走時の二重発火を防ぐ状態だけ保持する。
        s.landed=true;

    }

    player_->SetTranslate(playerPosition);
    player_->SetVelocity({});
    player_->UpdateWorldMatrix(false);
    // 表示メッシュを実際に分割した床片です。外周から割れ、最後に足元が抜けます。

    for (auto& pose:s.fractureChunks) {
        const engine::Vector3 delta=pose.position-s.start;
        const float distance=std::sqrt(delta.x*delta.x+delta.z*delta.z);
        // 外周ほど遅延率が0に近づく。プレイヤーの足元の床だけは発進時刻の後まで支える。
        const float delay=1-std::clamp(distance/48.0f,0.0f,1.0f);
        const bool supportsPlayer=std::abs(delta.x)<=5.1f && std::abs(delta.z)<=5.1f;
        const float begin=supportsPlayer?escapeTime+.004f:rageTime+.02f+delay*(escapeTime-rageTime-.01f);
        // 崩壊の開始からの実秒数で落下させる。-4.9は初速0の重力落下における加速度の半分。
        const float age=std::max(0.0f,(t-begin)*s.duration);
        pose.object->SetIsVisible(t>=rageTime && age<4.0f);
        pose.object->SetTranslate(pose.position+engine::Vector3{std::sin(age*2+pose.position.x)*age*.6f,-4.9f*age*age,0});
        pose.object->SetRotation(pose.rotation+engine::Vector3{age*.18f,age*.04f,age*.13f});
        pose.object->UpdateWorldMatrix(false);

        if (crossed(begin) && distance>10)
            VfxSequencer::PlayOneShot("enemy/false_king/crown_inheritance_floor_break_cue",pose.position+engine::Vector3{0,.8f,0});
    }

    // 親子を両方動かすと子だけ落下量が二重になるため、親が演出対象なら子の姿勢は親に追従させます。
    std::unordered_set<engine::Object3D*> animatedObjects;
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

        // 床や壁が動き出したフレームで衝突を外し、落ちる描画だけが残るようにする。
        if (age>0) {
            pose.object->SetCollisionAttribute(0);
            pose.object->SetCollisionMask(0);
        }
        pose.object->UpdateWorldMatrix(false);
    }

    return true;
}

void GamePlayScene::FinishCrownFinale(bool completed) {
    // 結果に応じて演出終了後のボス・Player状態を確定する。
    auto& s = crownFinale_;
    if (!s.active)
        return;
    // トラックの変更を元に戻してから、完走・中断それぞれの位置と操作状態を上書きする。
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

        // 完走後は2.5秒の無敵を付け、Directorが着地を処理済みなら着地の粒子を再生済みとして扱う。
        if (completed) {
            player_->StartEvasionInvincibility(2.5f);
            if (!s.landed)
                VfxSequencer::PlayOneShot("shared/crown_victory_land_cue", player_->GetWorldPosition());
        }
    }

    // 完走時にだけボスと勝利報酬を最終闘技場へ移し、戦闘を再開できるようにします。
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

        // 中断時は付与された王冠能力を解除してから、吸収前の見た目で元の体へ戻す。
        if (!completed) {
            player_->ClearCrownPower();
            player_->SetModel(s.playerModel);
            player_->SetMaterialType(s.playerMaterial);
            player_->SetColor(s.playerColor);
        }
    }

    // 演出用画角と固定視点から、着地後の通常追従へ戻します。
    if (engine::Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        camera->SetFovY(s.fov);
        camera->SetFollowTarget(player_);
        camera->SetLockOnTarget(nullptr);
        camera->SetFollowMode(engine::Camera::FollowMode::Aimable);
        camera->SetRotation({0.24f, std::numbers::pi_v<float>, 0.0f});
        camera->SnapToThirdPerson(18.0f, 2.5f, 0.24f);
        camera->SetInputEnabled(true);
    }

    CameraManager::GetInstance()->SetActiveCamera(nullptr);
    // 装飾ObjectはStopで共通再生器が破棄する。借用参照だけを外す。
    s.fragment = nullptr;
}

void GamePlayScene::ResetCrownFinaleWorld() {
    pendingCrownFinaleBoss_ = nullptr;
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
    // 最初に記録した配置へ戻します。イベント受信側の基準位置も戻し、次の更新で復元が打ち消されないようにします。
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

    // 元の配置を復元した後に、演出で追加した床片と闘技場を削除対象へ移します。
    for (auto* object : s.generatedObjects) {
        if (crownMoviePreview_.active) {
            object->SetIsVisible(false);
            object->SetReplayRemoved(true);
        }

        RequestRemoveObject(object);
    }

    s = CrownFinaleState{};
}

void GamePlayScene::DrawCrownFinaleWorld(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // Drawで取得した共有ライトを渡し、直前の描画が残したGPU状態に依存させない。
    if (crownFinale_.active)
        crownFinalePlayer_.DrawPresentation(pointLightResource, spotLightResource);
}

void GamePlayScene::DrawCrownFinaleUi() {
    // ゲーム操作と演出の案内が不要な場面、またはポーズ中は描画を省きます。
    if (!player_ || (!SceneManager::GetInstance()->IsPlaying() && !crownMoviePreview_.active) || isGoal_ ||
        (controlsGuideOverlay_ && controlsGuideOverlay_->IsActive()) ||
        (pauseMenuOverlay_ && pauseMenuOverlay_->IsActive()))
        return;
    auto* boss = crownMoviePreview_.active ? crownFinale_.boss : FindFalseKingBossForHUD();
    if (!boss || (!crownFinale_.active && !boss->IsEncounterHudActive()))
        return;
    int index = boss->GetBattlePhase() == 2 ? 0 : (boss->GetBattlePhase() == 3 ? 2 : -1);
    float alpha = 1.0f;

    // 登場では名前、継承では時刻ごとの指示、それ以外は現在の形態に必要な案内を選びます。
    if (arenaBossIntroActive_) {
        index = 3;
        alpha = Easing::SmoothStep01((arenaBossIntroTimer_ - arenaBossIntroMessageInTime_) / 0.35f) *
                (1.0f - Easing::SmoothStep01((arenaBossIntroTimer_ - arenaBossIntroMessageOutTime_) / 0.35f));
    } else if (crownFinale_.active) {
        const float t = crownFinale_.time.Elapsed() / crownFinale_.duration;
        const auto tuning = GetCrownFinaleTiming();
        index = t < tuning.movieFragmentPullTime ? 4 : (t < tuning.movieFragmentAbsorbTime ? 1 : 5);
        // 案内の開始と終了は全尺に対する割合。前後の境目を同じ幅でフェードさせる。
        const float begin = index == 4 ? 0.0f : (index == 1 ? tuning.movieFragmentPullTime : tuning.movieFragmentAbsorbTime);
        const float end = index == 4 ? tuning.movieFragmentPullTime : (index == 1 ? tuning.movieFragmentAbsorbTime : tuning.movieEscapeTime);
        alpha = Easing::SmoothStep01((t - begin) / 0.025f) * (1.0f - Easing::SmoothStep01((t - end + 0.025f) / 0.025f));
    }

    if (index < 0)
        return;
    if (alpha <= 0.001f)
        return;
    // 表示対象が決まった時点で画像を用意し、次のフレームから同じSpriteを使います。
    if (!crownFinaleMessages_[index]) {
        const char* paths[] = {"ui/crown_finale/seals.png",    "ui/crown_finale/inherit.png",
                               "ui/control_guide/book/crown_gate/controls.png", "ui/crown_finale/boss_intro_title.png",
                               "ui/crown_finale/release.png",  "ui/crown_finale/awaken.png"};
        crownFinaleMessages_[index] = std::make_unique<engine::Sprite>();
        const uint32_t texture = engine::Sprite::LoadTexture(paths[index]);
        crownFinaleMessages_[index]->Initialize(GetSpriteCommon(), texture);
        crownFinaleMessages_[index]->SetTextureName(paths[index]);
        crownFinaleMessages_[index]->SetAnchorPoint({0.5f, 0.5f});
    }

    auto& message = crownFinaleMessages_[index];
    message->SetColor({1.0f, 1.0f, 1.0f, alpha});
    const bool movie = crownFinale_.active || arenaBossIntroActive_;

    // 透過文字の実寸比率を維持し、通常時は左下のHPと中央のプレイヤーを避けます。
    const auto& metadata = TextureManager::GetInstance()->GetMetadata(message->GetTextureHandle());
    const auto& introStyle = BossIntroSettings::Get();
    const float titlePop = arenaBossIntroActive_ ? std::lerp(1.12f,1.0f,Easing::SmoothStep01((arenaBossIntroTimer_-arenaBossIntroMessageInTime_)/.18f)) : 1.0f;
    // 幅と高さそれぞれの最大倍率の小さい方を使い、文字画像を縦横比を保ったまま表示範囲に収める。
    const float fit = titlePop * std::min((arenaBossIntroActive_ ? introStyle.titleWidth : movie ? 1020.0f : 820.0f) / std::max(1.0f, static_cast<float>(metadata.width)),
                               (arenaBossIntroActive_ ? 190.0f : movie ? 72.0f : 64.0f) / std::max(1.0f, static_cast<float>(metadata.height)));
    message->SetPosition(SpriteLayoutScaler::ScaleDesignPosition({movie ? 640.0f : 830.0f, arenaBossIntroActive_ ? introStyle.titleY : movie ? 620.0f : 662.0f}, {1280.0f, 720.0f}));
    message->SetSize(
        SpriteLayoutScaler::ScaleDesignSize({metadata.width * fit, metadata.height * fit}, {1280.0f, 720.0f}));
    message->Update();
    message->Draw();
}
