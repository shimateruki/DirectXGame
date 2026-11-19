#define NOMINMAX
#include "engine/animation/cinematic/CinematicPlayer.h"
#include "BaseScene.h"
#include "SceneManager.h"
#include "CameraManager.h"
#include "PostEffect.h"
#include "GpuParticleManager.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace engine {


CinematicPlayer::SpringState CinematicPlayer::GetSpringState(const std::string& name) const {
    const auto found = springStates_.find(name);
    return found == springStates_.end() ? SpringState{} : found->second;
}

void CinematicPlayer::RestoreSpringState(const std::string& name, const SpringState& state) {
    springStates_[name] = state;
}

CinematicTransformKey CinematicPlayer::UpdateSpringAttachment(const std::string& name, const SpringInput& input, float deltaTime) {
    const CinematicSpringSettings defaults;
    // 素材に部品名がなければ内蔵の係数を使います。積分済みの位置と速度は係数とは別に名前ごとに保持します。
    const auto* settings = &defaults;
    if (sequence_) {
        const auto found = sequence_->presentation.springs.find(name);
        if (found != sequence_->presentation.springs.end()) settings = &found->second;
    }
    const auto& physics = *settings;
    auto& state = springStates_[name];
    const auto& targetPosition = input.targetPosition;
    const float motionEnergy = std::clamp(input.motionEnergy, 0.0f, 1.0f);
    auto NormalizeYaw = [](float yaw) {
        constexpr float pi = std::numbers::pi_v<float>;
        while (yaw > pi) yaw -= 2.0f * pi;
        while (yaw < -pi) yaw += 2.0f * pi;
        return yaw;
    };
    CinematicTransformKey result;
    if (!state.initialized) {
        state.position = targetPosition + physics.initialPositionOffset;
        state.velocity = input.forward * physics.initialForwardVelocity + engine::Vector3{0.0f, physics.initialVerticalVelocity, 0.0f};
        state.rotation = input.baseRotation + physics.initialRotationOffset;
        state.angularVelocity = {0.0f, 0.0f, physics.initialRollVelocity};
        state.initialized = true;
    }

    // 長いフレームを制限し、必要なら小刻みに積分して追従部品の揺れを安定させる。
    const float frameDeltaTime = std::clamp(deltaTime, 0.0f, 1.0f / 30.0f);
    // 1フレームで扱う時間は最大1/30秒とし、分割後の積分幅が1/120秒を越えない分割数を選びます。
    const int substepCount = std::max(1, static_cast<int>(std::ceil(frameDeltaTime / (1.0f / 120.0f))));
    const float substep = frameDeltaTime / static_cast<float>(substepCount);

    for (int step = 0; step < substepCount; ++step) {
        const engine::Vector3 springDelta = targetPosition - state.position;
        // 目標までの距離にばねの係数を掛け、速度に比例する減衰を引きます。速度を先に更新してから位置を進めます。
        const engine::Vector3 acceleration = springDelta * (physics.positionStiffness + motionEnergy * 8.0f) -
                                     state.velocity * physics.positionDamping;
        state.velocity = state.velocity + acceleration * substep;
        state.position = state.position + state.velocity * substep;
    }

    // バネが跳ね過ぎても追従先から大きく外れないよう、水平と上下の偏差を別々に制限する。
    engine::Vector3 offset = state.position - targetPosition;
    const float maxPlanarOffset = physics.maxPlanarOffset + motionEnergy * 0.05f;
    const float planarLength = std::sqrt(offset.x * offset.x + offset.z * offset.z);

    if (planarLength > maxPlanarOffset && planarLength > 0.0001f) {
        // 上限と現在距離の比をXZ両軸に掛け、向きを保ったまま円の境界へ戻します。
        const float rate = maxPlanarOffset / planarLength;
        offset.x *= rate;
        offset.z *= rate;
        state.velocity.x *= 0.55f;
        state.velocity.z *= 0.55f;
    }

    const float unclampedY = offset.y;
    offset.y = std::clamp(offset.y, -physics.maxSink, physics.maxLift + motionEnergy * 0.04f);

    // 上下限で位置を切り詰めたときだけ鉛直速度を減衰させ、次の更新で同じ向きに飛び出しにくくします。
    if (offset.y != unclampedY) {
        state.velocity.y *= 0.32f;
    }

    result.position = targetPosition + offset;
    state.position = result.position;

    // 追従部品の位置ずれと速度から前後・左右の傾きを求め、基準対象の傾きも加える。
    const float forwardOffset = engine::Math::Dot(offset, input.forward);
    const float rightOffset = engine::Math::Dot(offset, input.right);
    // 位置差はワールド距離、速度はワールド距離毎秒です。それぞれ前後・左右の軸へ射影して傾きへ換算します。
    const float forwardSpeed = engine::Math::Dot(state.velocity, input.forward);
    const float rightSpeed = engine::Math::Dot(state.velocity, input.right);
    engine::Vector3 targetRotation = input.baseRotation;
    targetRotation.x += input.sourceRotationDelta.x * physics.sourceTiltFollow -
                        forwardOffset * physics.tiltFromOffset - forwardSpeed * physics.tiltFromVelocity;
    targetRotation.z += input.sourceRotationDelta.z * physics.sourceTiltFollow +
                        rightOffset * physics.tiltFromOffset + rightSpeed * physics.tiltFromVelocity;
    targetRotation.x =
        std::clamp(targetRotation.x, input.baseRotation.x - 0.52f, input.baseRotation.x + 0.52f);
    targetRotation.z =
        std::clamp(targetRotation.z, input.baseRotation.z - 0.52f, input.baseRotation.z + 0.52f);
    // ラジアンのヨー差を±πの範囲へ畳み、角度の境界で一周近く戻らないようにします。
    const float targetYaw = input.baseRotation.y + NormalizeYaw(input.sourceRotationDelta.y);
    targetRotation.y = state.rotation.y + NormalizeYaw(targetYaw - state.rotation.y);

    // Yaw差を最短角へ畳んでから回転バネを積分する。
    for (int step = 0; step < substepCount; ++step) {
        engine::Vector3 rotationDelta = targetRotation - state.rotation;
        rotationDelta.y = NormalizeYaw(rotationDelta.y);
        const engine::Vector3 acceleration =
            rotationDelta * physics.angularStiffness - state.angularVelocity * physics.angularDamping;
        state.angularVelocity = state.angularVelocity + acceleration * substep;
        state.rotation = state.rotation + state.angularVelocity * substep;
    }

    result.rotation = state.rotation;

    // 下向きの偏差と速度が大きいときだけ、追従部品を少し潰して着地感を出す。
    const float compression =
        std::clamp(-offset.y * 2.2f + std::max(0.0f, -state.velocity.y) * 0.045f, 0.0f, 0.35f);
    result.scale.x *= 1.0f + compression * physics.compressionScale;
    result.scale.y *= 1.0f - compression * physics.compressionScale * 0.72f;
    result.scale.z *= 1.0f + compression * physics.compressionScale;
    return result;
}

void CinematicPlayer::FitCameraToPoints(std::span<const engine::Vector3> points, float fov, float aspect,
    float margin, float topMargin, const engine::Vector3& centerOffset,
    const engine::Vector3& fallbackForward, engine::Vector3& eye, engine::Vector3& target) {
    if (points.empty() || !std::isfinite(fov) || !std::isfinite(aspect) || fov <= 0 || aspect <= 0) return;
    margin = std::clamp(margin, 0.0f, 0.45f);
    topMargin = std::clamp(topMargin, margin, 0.95f - margin);
    // 全点のワールド座標の最小・最大から、構図の基準にする包囲箱の中心を求めます。
    auto low=points.front(),high=points.front();
    for (const auto& p:points) {
        low={(std::min)(low.x,p.x),(std::min)(low.y,p.y),(std::min)(low.z,p.z)};
        high={(std::max)(high.x,p.x),(std::max)(high.y,p.y),(std::max)(high.z,p.z)};
    }
    auto direction=target-eye;
    if (engine::Math::Length(direction)<0.001f) direction=fallbackForward*-1.0f+engine::Vector3{0,-0.25f,0};
    auto forward=engine::Math::Normalize(direction);
    if (std::abs(forward.y)>0.95f) forward=engine::Math::Normalize(fallbackForward*-0.5f+engine::Vector3{0,forward.y,0});
    // ほぼ真上・真下を向くと外積が小さくなるため、直前で向きを補正してカメラの右・上軸を作ります。
    const auto right=engine::Math::Normalize(engine::Math::Cross({0,1,0},forward));
    const auto up=engine::Math::Cross(forward,right);
    auto center=(low+high)*0.5f;
    // 垂直画角はラジアンです。半画角の正接は奥行き1あたりの縦の半幅です。横の半幅はアスペクト比を掛けて求めます。
    const float tanY=std::tan(fov*0.5f);
    const float tanX=tanY*aspect;
    center=center+centerOffset;
    // 画面に対する余白の割合を、中心0、上下端±1の正規化座標に置き換えます。
    const float top=1.0f-2.0f*topMargin;
    const float bottom=-1.0f+2.0f*margin;
    const float middle=(top+bottom)*0.5f;
    const float horizontal=1.0f-2.0f*margin;
    float distance=engine::Math::Length(target-eye);
    for (const auto& p:points) {
        const auto delta=p-center;
        const float x=engine::Math::Dot(delta,right),y=engine::Math::Dot(delta,up),z=engine::Math::Dot(delta,forward);
        // 各点について最小の前方距離と左右・上下の画角条件を逆算し、全条件を満たす最大値を採用します。
        distance=(std::max)({distance,0.25f-z,
            std::abs(x)/(tanX*horizontal)-z,
            (y-top*tanY*z)/(tanY*(top-middle)),
            (-y+bottom*tanY*z)/(tanY*(middle-bottom))});
    }
    // 開始時の向きを保持しつつ対象の中心へ寄せ、結果UIのための上側余白を確保する。
    target=center-up*(middle*tanY*distance);
    eye=target-forward*distance;
}

std::function<void(const std::string&, float, bool)> CinematicPlayer::playAudio_;
std::function<void(const std::string&)> CinematicPlayer::stopAudio_;
std::function<std::vector<CinematicPlayer::AudioEntry>()> CinematicPlayer::audioEntries_;

namespace {
engine::Vector3 Product(const engine::Vector3& a, const engine::Vector3& b) { return {a.x*b.x, a.y*b.y, a.z*b.z}; }
engine::Vector3 Rotate(const engine::Vector3& v, float yaw) { return CinematicAnchorPosition(v, {}, yaw); }
CinematicViewKey ViewAt(const std::vector<CinematicViewKey>& keys, float time) {
    return SampleCinematicKeys(keys, time, [](const auto& a, const auto& b, float t) {
        auto k=a;
        // 演出時刻の秒を前後のキー間の割合へ変えます。数値項目は滑らかに補間し、参照名は先頭キーの設定を保持します。
        t = std::clamp((t-a.time)/std::max(0.000001f,b.time-a.time),0.0f,1.0f);
        k.focusMix=engine::Math::Lerp(a.focusMix,b.focusMix,t); k.focusOffset=engine::Math::Lerp(a.focusOffset,b.focusOffset,t);
        k.scaledFocusOffset=engine::Math::Lerp(a.scaledFocusOffset,b.scaledFocusOffset,t);
        k.eyeOffset=engine::Math::Lerp(a.eyeOffset,b.eyeOffset,t); k.jitter=engine::Math::Lerp(a.jitter,b.jitter,t);
        k.fov=engine::Math::Lerp(a.fov,b.fov,t); k.bars=engine::Math::Lerp(a.bars,b.bars,t);
        return k;
    });
}
template<class State> engine::Vector3 PositionAt(const State& s, const CinematicObjectClip& clip, const CinematicVisualKey& k) {
    const auto pivot=Rotate(Product(clip.pivot,s.scale),s.rotation.y);
    auto offset=k.position;
    // モデル基準の距離は対象倍率とヨーを反映します。放射方向の指定では X を水平方向への距離、Y を高さとして扱います。
    if (clip.positionSpace=="model") offset=Rotate(Product(offset,s.scale),s.rotation.y);
    if (clip.positionSpace=="radial") offset=s.radial*k.position.x+engine::Vector3{0,k.position.y,0};
    auto position=s.origin+pivot+offset;
    // 基準点からの床の高さに達した場合は、その位置にキーの跳ね返り量を足して接地後の高さを決めます。
    if (position.y <= s.origin.y+clip.floorHeight+0.001f)
        position.y=s.origin.y+clip.floorHeight+k.bounce;
    return position;
}
}

CinematicPlayer::~CinematicPlayer() {
    // Scene自身の破棄中は既にObject配列が失われ得る。復元は所有側のStop(true)で済ませる。
    ReleasePresentation(false);
}

void CinematicPlayer::SetAudioServices(std::function<void(const std::string&,float,bool)> play,
    std::function<void(const std::string&)> stop, std::function<std::vector<AudioEntry>()> entries) {
    playAudio_=std::move(play); stopAudio_=std::move(stop); audioEntries_=std::move(entries);
}
std::vector<CinematicPlayer::AudioEntry> CinematicPlayer::GetAudioEntries() {
    return audioEntries_ ? audioEntries_() : std::vector<AudioEntry>{};
}

std::unordered_set<engine::Object3D*> CinematicPlayer::GetLiveTargets() const {
    std::unordered_set<engine::Object3D*> live;
    if (sceneManager_ && sceneManager_->GetCurrentScene()==boundScene_ && boundScene_)
        for (const auto& object:boundScene_->GetObjects())
            if (object && !object->IsReplayRemoved()) live.insert(object.get());
    for (const auto& state:presentationObjects_)
        if (state.owned) live.insert(state.owned.get());
    return live;
}

void CinematicPlayer::PreparePresentation() {
    if (!sequence_ || !boundScene_) return;
    // 名前は素材内で完結し、生成Objectをシーンの保存用配列へ混ぜない。
    for (const auto& clip:sequence_->presentation.objects) {
        PresentationObject s;
        s.source=ResolveTarget({clip.source,-1});
        // 生成指定なら演出専用の見た目を所有し、配置済みの対象を使う設定ではシーンから借ります。
        if (clip.spawn && s.source && boundScene_->GetObject3DCommon()) {
            s.owned=std::make_unique<engine::Object3D>();
            s.owned->Initialize(boundScene_->GetObject3DCommon());
            if (!clip.model.empty()) s.owned->SetModel(clip.model);
            s.target=s.owned.get();
            s.target->SetName(clip.name); s.target->SetEditorInternal(true);
            s.target->SetCollisionAttribute(0); s.target->SetCollisionMask(0);
            s.target->SetMaterialType(clip.additive?1:0); s.target->SetEnableLighting(clip.lit);
            s.target->SetEnableEnvMap(clip.lit); s.target->SetEnvIntensity(0.55f);
            if (clip.additive) s.target->SetBlendMode(BlendMode::Add);
        } else if (!clip.spawn) s.target=ResolveTarget({clip.name,-1});
        presentationObjects_.push_back(std::move(s));
    }
}

void CinematicPlayer::AddPresentationIssues(std::vector<std::string>& issues) const {
    if (!sequence_) return;
    const auto& p=sequence_->presentation;
    const auto live=GetLiveTargets();
    // 視点キーが実カメラを所有する間は、既存Shotとの二重制御を許可しない。
    const bool hasCameraShot=std::any_of(sequence_->cameraShots.begin(),sequence_->cameraShots.end(),
        [](const auto& shot) { return shot.enabled && !shot.muted; });
    const bool hasLegacyCamera=sequence_->cameraShots.empty() && std::any_of(sequence_->transformTracks.begin(),sequence_->transformTracks.end(),
        [&](const auto& track) {
            auto* target=ResolveTarget(track.binding);
            return track.enabled && !track.muted && live.contains(target) && target->IsCameraObject();
        });
    if (!p.view.empty() && (hasCameraShot || hasLegacyCamera))
        issues.push_back("同じ演出で視点キーとCamera Shotを同時に有効化できません。");
    std::unordered_set<std::string> names;
    for (size_t i=0;i<p.objects.size();++i) {
        const auto& clip=p.objects[i];
        if (!clip.enabled) continue;
        if (clip.name.empty() || !names.insert(clip.name).second) issues.push_back("演出Object名が空、または重複しています: "+clip.name);
        if (i>=presentationObjects_.size() || !live.contains(presentationObjects_[i].target) || !live.contains(presentationObjects_[i].source))
            issues.push_back(clip.name+": 演出Objectまたは基準対象が見つかりません。");
        for (const auto& key : clip.keys)
            for (const auto& name : {key.positionReference, key.positionTarget})
                if (!name.empty() && !PresentationPosition(name, live))
                    issues.push_back(clip.name + ": 位置の基準点が見つかりません: " + name);
        float previousEnd = -1.0f;
        for (const auto& motion : clip.motions) {
            const float begin=motion.startSignal.empty()?motion.startTime:sequence_->GetSignalTime(motion.startSignal,-1) + motion.startSignalOffset;
            const float end=motion.endSignal.empty()?motion.endTime:sequence_->GetSignalTime(motion.endSignal,-1) + motion.endSignalOffset;
            // 区間はシグナルの時刻と秒のオフセットを解決した後で、順序と重なりを判定します。
            const bool missingSignal = (!motion.startSignal.empty() && sequence_->GetSignalTime(motion.startSignal,-1)<0) ||
                (!motion.endSignal.empty() && sequence_->GetSignalTime(motion.endSignal,-1)<0);
            if (missingSignal || begin<0 || end<=begin || begin<previousEnd)
                issues.push_back(clip.name+": 区間のSignal不足、時刻の逆転または重複があります。");
            previousEnd=end;
            for (const auto& point : {motion.from.positionReference,motion.to.positionReference})
                if (!point.empty() && !PresentationPosition(point,live))
                    issues.push_back(clip.name+": 区間の基準点が見つかりません: "+point);
        }
        if ((clip.keys.empty() && clip.motions.empty())) issues.push_back(clip.name+": 見た目キーがありません。");
    }
    if (!p.view.empty() && (!live.contains(ResolveTarget({p.reference,-1})) || !live.contains(ResolveTarget({p.lookToward,-1}))))
        issues.push_back("演出カメラの基準対象が見つかりません。");
    std::unordered_set<std::string> viewTargets;
    for (const auto& key:p.view)
        for (const auto& name:{key.reference,key.lookToward,key.eyeReference})
            if (!name.empty() && viewTargets.insert(name).second && !PresentationPosition(name,live))
                issues.push_back("注視対象が見つかりません: "+name);
    for (const auto& name:p.hideObjects)
        if (!ResolveTarget({name,-1})) issues.push_back("非表示対象が見つかりません: "+name);
    for (const auto& clip:p.particles)
        if (!ResolveTarget({clip.target,-1})) issues.push_back("粒子の発生点が見つかりません: "+clip.target);
}

std::optional<engine::Vector3> CinematicPlayer::PresentationPosition(const std::string& name,
    const std::unordered_set<engine::Object3D*>& live) const {
    if (positionResolver_)
        if (const auto position=positionResolver_({name,-1})) return position;
    auto* target=ResolveTarget({name,-1});
    return live.contains(target) ? std::optional(target->GetTranslate()) : std::nullopt;
}

std::optional<engine::Vector3> CinematicPlayer::SampleObjectPosition(const std::string& name, float time) const {
    if (!sequence_ || !presentationCaptured_ || !std::isfinite(time)) return std::nullopt;
    const auto& clips = sequence_->presentation.objects;
    const auto it = std::find_if(clips.begin(), clips.end(), [&](const auto& clip) { return clip.name == name; });
    if (it == clips.end()) return std::nullopt;
    return SampleObjectPosition(static_cast<size_t>(it - clips.begin()), time, GetLiveTargets());
}

// 実キャラクターはゲーム側が操作区間を決め、姿勢の数式は通常再生と同じ入口へ任せる。
bool CinematicPlayer::ApplyObjectTransform(const std::string& name, float time) {
    if (!sequence_ || !presentationCaptured_ || !std::isfinite(time)) return false;
    const auto& clips = sequence_->presentation.objects;
    const auto it = std::find_if(clips.begin(), clips.end(), [&](const auto& clip) { return clip.name == name; });
    if (it == clips.end()) return false;
    const size_t index=static_cast<size_t>(it-clips.begin());
    if (!ApplyPresentationTransform(index,time,GetLiveTargets())) return false;
    presentationObjects_[index].target->UpdateWorldMatrix(false);
    return true;
}

bool CinematicPlayer::ApplyPresentationTransform(size_t index, float time,
    const std::unordered_set<engine::Object3D*>& live) {
    const auto position = SampleObjectPosition(index, time, live);
    if (!position) return false;
    const auto& clip = sequence_->presentation.objects[index];
    auto& state = presentationObjects_[index];
    const auto key = SampleCinematicVisual(clip, time, sequence_);
    // 追従指定の成分は対象の現在値を使い、それ以外は再生開始前の採取値を使います。
    const auto rotation = clip.followRotation ? state.source->GetRotation() : state.rotation;
    const auto scale = clip.followScale ? state.source->GetScale() : state.scale;
    state.target->SetTranslate(*position);
    state.target->SetRotation(clip.inheritRotation ? Product(rotation,clip.rotationMask)+key.rotation : key.rotation);
    state.target->SetScale(clip.inheritScale ? Product(scale,key.scale) : key.scale);
    return true;
}

std::optional<engine::Vector3> CinematicPlayer::SampleObjectPosition(size_t index, float time,
    const std::unordered_set<engine::Object3D*>& live) const {
    if (!sequence_ || index >= presentationObjects_.size() || index >= sequence_->presentation.objects.size())
        return std::nullopt;
    const auto& clip = sequence_->presentation.objects[index];
    const auto& s = presentationObjects_[index];
    if (!clip.enabled || (clip.keys.empty() && clip.motions.empty()) || !live.contains(s.source) || !live.contains(s.target)) return std::nullopt;
    struct { engine::Vector3 origin, rotation, scale, radial; } pose{s.origin, s.rotation, s.scale, s.radial};
    if (clip.followPosition) pose.origin = s.source->GetTranslate();
    if (clip.followRotation) pose.rotation = s.source->GetRotation();
    if (clip.followScale) pose.scale = s.source->GetScale();
    bool valid = true;
    auto position = [&](const CinematicVisualKey& key) {
        if (key.positionReference.empty()) return PositionAt(pose, clip, key);
        const auto from = PresentationPosition(key.positionReference, live);
        const auto to = key.positionTarget.empty() ? from : PresentationPosition(key.positionTarget, live);
        if (!from || !to) { valid = false; return engine::Vector3{}; }
        return engine::Math::Lerp(*from, *to, key.positionMix) + key.position;
    };
    // 名前の異なるキーも、それぞれの基準点を解決してからワールド位置を補間する。
    if (!clip.motions.empty()) {
        const auto result = position(SampleCinematicVisual(clip,time,sequence_));
        return valid ? std::optional(result) : std::nullopt;
    }
    auto upper = std::upper_bound(clip.keys.begin(), clip.keys.end(), time,
        [](float t, const auto& key) { return t < key.time; });
    engine::Vector3 result;
    if (upper == clip.keys.begin()) result = position(clip.keys.front());
    else if (upper == clip.keys.end()) result = position(clip.keys.back());
    else {
        const auto& a = *(upper - 1); const auto& b = *upper;
        // 従来のキーは既存のイージングと接地処理を維持する。
        if (a.positionReference.empty() && b.positionReference.empty()) return PositionAt(pose, clip, SampleCinematicVisual(clip, time, sequence_));
        const float ratio = std::clamp((time - a.time) / std::max(0.000001f, b.time - a.time), 0.0f, 1.0f);
        result = engine::Math::Lerp(position(a), position(b), ratio);
    }
    return valid ? std::optional(result) : std::nullopt;
}

void CinematicPlayer::CapturePresentation() {
    if (!sequence_ || presentationCaptured_) return;
    if (presentationObjects_.empty()) PreparePresentation();
    const auto& p=sequence_->presentation;
    if (p.objects.empty() && p.view.empty()) return;
    presentationCaptured_=true;
    // 再生直前の姿勢を採る。準備済み部品の再生成やVFXの再読込は行わない。
    for (size_t i=0;i<presentationObjects_.size();++i) {
        auto& s=presentationObjects_[i];
        if (!s.source || !s.target) continue;
        const auto& clip=p.objects[i];
        s.origin=s.source->GetTranslate(); s.rotation=s.source->GetRotation(); s.scale=s.source->GetScale();
        const auto pivot=Rotate(Product(clip.pivot,s.scale),s.rotation.y);
        const float length=std::sqrt(pivot.x*pivot.x+pivot.z*pivot.z);
        // 基準からの水平距離で割って放射方向を単位化します。ピボットがほぼ真上なら +X を既定方向にします。
        s.radial=length>0.01f ? engine::Vector3{pivot.x/length,0,pivot.z/length} : engine::Vector3{1,0,0};
        s.before.position=s.target->GetTranslate(); s.before.rotation=s.target->GetRotation();
        s.before.scale=s.target->GetScale(); s.before.visible=s.target->GetIsVisible();
        s.material=s.target->CaptureMaterialState();
    }
    for (const auto& name:p.hideObjects)
        if (auto* object=ResolveTarget({name,-1})) {
            // 一時的に隠す対象は、終了時に元の表示状態へ戻せるよう、変更する前に可視フラグを保存します。
            presentationHidden_.push_back({object,object->GetIsVisible()}); object->SetIsVisible(false);
        }
    if (p.view.empty()) return;
    auto* reference=ResolveTarget({p.reference,-1}); auto* toward=ResolveTarget({p.lookToward,-1});
    if (!reference || !toward) return;
    presentationOrigin_=reference->GetTranslate(); presentationScale_=reference->GetScale();
    presentationLookAt_=toward->GetTranslate();
    const auto direction=presentationLookAt_-presentationOrigin_;
    // ワールドの +Z を正面として、注視先へ向く水平角をラジアンで求めます。高さの差はここで使いません。
    presentationYaw_=std::atan2(direction.x,direction.z);
    // 上書き先を確保する前に旧カメラを保持して、終了時に元の参照へ戻せるようにします。
    presentationPreviousCamera_=CameraManager::GetInstance()->GetActiveCameraOverride();
    presentationCamera_=std::make_unique<engine::Camera>(); presentationCamera_->Initialize();
    if (auto* camera=CameraManager::GetInstance()->GetActiveCamera())
        presentationCamera_->SetClipRange(camera->GetNearClip(),camera->GetFarClip());
    presentationCamera_->SetInputEnabled(false);
    if (auto* params=PostEffect::GetInstance()->GetParams()) presentationBars_=params->cinemaBarHeight;
}

void CinematicPlayer::ReleasePresentation(bool restore) {
    bindingsReady_=false;
    if (restore && presentationCaptured_) {
        const auto live=GetLiveTargets();
        // 借用した実体は生存を確かめて元の姿勢と材質へ戻します。専用に生成した実体は末尾の所有リスト破棄へ任せます。
        for (auto& s:presentationObjects_)
            if (!s.owned && live.contains(s.target)) {
                s.target->SetTranslate(s.before.position); s.target->SetRotation(s.before.rotation);
                s.target->SetScale(s.before.scale); s.target->SetIsVisible(s.before.visible);
                s.target->RestoreMaterialState(s.material);
                s.target->UpdateWorldMatrix(true);
            }
        for (const auto& hidden:presentationHidden_)
            if (live.contains(hidden.first)) hidden.first->SetIsVisible(hidden.second);
        if (presentationCamera_ && sceneManager_ && sceneManager_->GetCurrentScene()==boundScene_)
            if (auto* params=PostEffect::GetInstance()->GetParams()) params->cinemaBarHeight=presentationBars_;
    }
    // 自分が設定したカメラがまだ使用中の場合だけ差し替え、参照を外してから専用カメラを破棄します。
    if (presentationCamera_ && CameraManager::GetInstance()->GetActiveCameraOverride()==presentationCamera_.get())
        CameraManager::GetInstance()->SetActiveCamera(restore && sceneManager_ && sceneManager_->GetCurrentScene()==boundScene_ ? presentationPreviousCamera_ : nullptr);
    presentationCamera_.reset(); presentationObjects_.clear(); presentationHidden_.clear(); presentationCaptured_=false;
}

void CinematicPlayer::EvaluatePresentation(float time, float previous, bool dispatch) {
    if (!sequence_ || !presentationCaptured_) return;
    const auto& p=sequence_->presentation;
    const auto live=GetLiveTargets();
    for (size_t i=0;i<p.objects.size() && i<presentationObjects_.size();++i) {
        const auto& clip=p.objects[i]; auto& s=presentationObjects_[i];
        if (!clip.enabled || (clip.keys.empty() && clip.motions.empty())) continue;
        // 対象や基準がシーンから消えた時点で評価を中断します。残った対象とカメラの復元は ReleasePresentation に任せます。
        if (!live.contains(s.source) || !live.contains(s.target)) {
            playbackTimer_.Pause(); isPreviewing_=false; finished_=false;
            ReleasePresentation(true);
            return;
        }
        if (!clip.animate) continue;
        const auto k=SampleCinematicVisual(clip,time,sequence_);
        const auto scale=clip.followScale?s.source->GetScale():s.scale;
        if (!ApplyPresentationTransform(i,time,live)) {
            playbackTimer_.Pause(); isPreviewing_=false; finished_=false; ReleasePresentation(true); return;
        }
        const auto position=s.target->GetTranslate();
        s.target->SetColor(k.color); s.target->SetEmissive(k.emissive);
        s.target->SetIsVisible(k.visible && (!clip.inheritVisibility || s.source->GetIsVisible()));
        {
            engine::Object3D::PortalClipSettings clipSettings;
            clipSettings.enabled=k.clipEnabled; clipSettings.progress=0;
            // 切断面の高さを必要に応じてモデルの縦倍率で調整し、対象のワールド位置からのオフセットに変換します。
            clipSettings.center=position+engine::Vector3{0,k.clipHeight*(clip.clipScaleHeight?scale.y:1.0f)+clip.clipCenterOffset,0};
            clipSettings.normal={0,1,0}; clipSettings.edgeWidth=clip.clipEdgeWidth;
            clipSettings.dissolve=clip.clipDissolve; clipSettings.color=clip.clipColor;
            s.target->SetPortalClip(clipSettings);
        }
        s.target->UpdateWorldMatrix(true);
    }
    // 遅延フレームでも区間内の粒子を順に出す。停止・巻戻しのシークでは発火しない。
    if (dispatch && playbackTimer_.IsRunning() && time>=previous)
        for (const auto& clip:p.particles) {
            size_t i=0;
            while (i<p.objects.size() && p.objects[i].name!=clip.target) ++i;
            if (i>=presentationObjects_.size() || (p.objects[i].keys.empty() && p.objects[i].motions.empty())) continue;
            const auto& s=presentationObjects_[i];
            // 発生間隔は秒です。前回時刻の発生番号は済んでいるので次から始め、今回はクリップの終了時刻までに制限します。
            const int first=std::max(0,static_cast<int>(std::floor((previous-clip.startTime)/clip.interval))+1);
            const int last=static_cast<int>(std::floor((std::min(time,clip.endTime)-clip.startTime)/clip.interval));
            for (int tick=first;tick<=last;++tick) {
                // 遅延して複数回発生する場合も、現在時刻の位置へまとめず、それぞれの発生時刻の位置を再評価します。
                const float tickTime = clip.startTime + tick * clip.interval;
                const auto& objectClip = p.objects[i];
                const bool hasReferences = !objectClip.motions.empty() || std::any_of(objectClip.keys.begin(), objectClip.keys.end(),
                    [](const auto& key) { return !key.positionReference.empty(); });
                // 従来素材の粒子は開始姿勢基準の挙動を保ち、基準点キーだけ新しい評価を使う。
                const auto sampled = hasReferences ? SampleObjectPosition(i, tickTime, live)
                    : std::optional(PositionAt(s, objectClip, SampleCinematicVisual(objectClip, tickTime, sequence_)));
                if (!sampled) continue;
                const auto position = *sampled;
                auto direction=clip.direction;
                if (clip.directionMode=="radialIn") {
                    // 中心へ向かう水平ベクトルを単位化し、保存された鉛直方向の成分を残します。
                    const auto d=s.origin-position; const float length=std::sqrt(d.x*d.x+d.z*d.z);
                    if (length>0.001f) direction={d.x/length,clip.direction.y,d.z/length};
                }
                GpuParticleManager::GetInstance()->EmitDirected(clip.preset,position,direction,clip.scale);
            }
        }
    if (presentationCamera_ && !p.view.empty()) {
        const auto k=ViewAt(p.view,time);
        const auto from=k.reference.empty()?std::optional(presentationOrigin_):PresentationPosition(k.reference,live);
        const auto to=k.lookToward.empty()?std::optional(presentationLookAt_):PresentationPosition(k.lookToward,live);
        const auto eyeAnchor=k.eyeReference.empty()?std::optional(engine::Vector3{}):PresentationPosition(k.eyeReference,live);
        if (!from || !to || !eyeAnchor) {
            playbackTimer_.Pause(); isPreviewing_=false; finished_=false; ReleasePresentation(true); return;
        }
        const float yaw=p.viewWorldSpace?0.0f:presentationYaw_;
        const auto target=engine::Math::Lerp(*from,*to,k.focusMix)+
            Product(k.scaledFocusOffset,presentationScale_)+Rotate(k.focusOffset,yaw);
        const auto eye=(k.eyeReference.empty()?target:*eyeAnchor)+Rotate(k.eyeOffset,yaw)+k.jitter;
        const auto d=target-eye;
        // 注視点との差からピッチとヨーをラジアンで求め、ロールは0とします。
        const engine::Vector3 rotation{-std::atan2(d.y,std::sqrt(d.x*d.x+d.z*d.z)),std::atan2(d.x,d.z),0};
        presentationCamera_->ConfigFixedPoint(eye,rotation);
        presentationCamera_->ApplyReplayView(eye,target,rotation,k.fov,presentationCamera_->GetNearClip(),presentationCamera_->GetFarClip());
        CameraManager::GetInstance()->SetActiveCamera(presentationCamera_.get());
        if (auto* params=PostEffect::GetInstance()->GetParams()) params->cinemaBarHeight=k.bars;
    }
}

void CinematicPlayer::DrawPresentation(ID3D12Resource* pointLight, ID3D12Resource* spotLight) {
    if (!presentationCaptured_ || !sequence_) return;
    // 演出が所有する表示モデルを通常材質、加算材質の順で描きます。シーン所有の対象は通常の描画経路へ任せます。
    for (int transparent=0;transparent<2;++transparent)
        for (size_t i=0;i<presentationObjects_.size() && i<sequence_->presentation.objects.size();++i) {
            auto& s=presentationObjects_[i]; const auto& clip=sequence_->presentation.objects[i];
            if (s.owned && !clip.model.empty() && clip.enabled && clip.additive==(transparent!=0) && s.target->GetIsVisible()) {
                s.target->UpdateWorldMatrix(true); s.target->Draw(pointLight,spotLight);
            }
        }
}

} // namespace engine
