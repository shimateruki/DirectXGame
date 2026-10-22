#define NOMINMAX
#include "engine/animation/cinematic/CinematicPlayer.h"
#include "BaseScene.h"
#include "SceneManager.h"
#include "CameraManager.h"
#include "PostEffect.h"
#include "GPUParticleManager.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace engine {

std::function<void(const std::string&, float, bool)> CinematicPlayer::playAudio_;
std::function<void(const std::string&)> CinematicPlayer::stopAudio_;
std::function<std::vector<CinematicPlayer::AudioEntry>()> CinematicPlayer::audioEntries_;

namespace {
engine::Vector3 Product(const engine::Vector3& a, const engine::Vector3& b) { return {a.x*b.x, a.y*b.y, a.z*b.z}; }
engine::Vector3 Rotate(const engine::Vector3& v, float yaw) { return CinematicAnchorPosition(v, {}, yaw); }
CinematicVisualKey VisualAt(const CinematicObjectClip& clip, float time) {
    return SampleCinematicKeys(clip.keys, time, [](const auto& a, const auto& b, float t) {
        auto k = a;
        static_cast<CinematicTransformKey&>(k) = InterpolateCinematicTransform(a,b,t,true);
        t = std::clamp((t-a.time)/std::max(0.000001f,b.time-a.time),0.0f,1.0f);
        k.color=engine::Math::Lerp(a.color,b.color,t);
        k.emissive=engine::Math::Lerp(a.emissive,b.emissive,t); k.clipHeight=engine::Math::Lerp(a.clipHeight,b.clipHeight,t);
        k.bounce=engine::Math::Lerp(a.bounce,b.bounce,t);
        return k;
    });
}
CinematicViewKey ViewAt(const std::vector<CinematicViewKey>& keys, float time) {
    return SampleCinematicKeys(keys, time, [](const auto& a, const auto& b, float t) {
        auto k=a;
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
    if (clip.positionSpace=="model") offset=Rotate(Product(offset,s.scale),s.rotation.y);
    if (clip.positionSpace=="radial") offset=s.radial*k.position.x+engine::Vector3{0,k.position.y,0};
    auto position=s.origin+pivot+offset;
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

std::unordered_set<engine::Object3d*> CinematicPlayer::GetLiveTargets() const {
    std::unordered_set<engine::Object3d*> live;
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
        if (clip.spawn && s.source && boundScene_->GetObject3dCommon()) {
            s.owned=std::make_unique<engine::Object3d>();
            s.owned->Initialize(boundScene_->GetObject3dCommon());
            if (!clip.model.empty()) s.owned->SetModel(clip.model);
            s.target=s.owned.get();
            s.target->SetName(clip.name); s.target->SetEditorInternal(true);
            s.target->SetCollisionAttribute(0); s.target->SetCollisionMask(0);
            s.target->SetMaterialType(clip.additive?1:0); s.target->SetEnableLighting(clip.lit);
            s.target->SetEnableEnvMap(clip.lit); s.target->SetEnvIntensity(0.55f);
            if (clip.additive) s.target->SetBlendMode(BlendMode::kAdd);
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
        if (clip.keys.empty()) issues.push_back(clip.name+": 見た目キーがありません。");
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
    const std::unordered_set<engine::Object3d*>& live) const {
    if (positionResolver_)
        if (const auto position=positionResolver_({name,-1})) return position;
    auto* target=ResolveTarget({name,-1});
    return live.contains(target) ? std::optional(target->GetTranslate()) : std::nullopt;
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
        s.radial=length>0.01f ? engine::Vector3{pivot.x/length,0,pivot.z/length} : engine::Vector3{1,0,0};
        s.before.position=s.target->GetTranslate(); s.before.rotation=s.target->GetRotation();
        s.before.scale=s.target->GetScale(); s.before.visible=s.target->GetIsVisible();
        if (auto* material=s.target->GetMaterialData()) s.material=*material;
    }
    for (const auto& name:p.hideObjects)
        if (auto* object=ResolveTarget({name,-1})) {
            presentationHidden_.push_back({object,object->GetIsVisible()}); object->SetIsVisible(false);
        }
    if (p.view.empty()) return;
    auto* reference=ResolveTarget({p.reference,-1}); auto* toward=ResolveTarget({p.lookToward,-1});
    if (!reference || !toward) return;
    presentationOrigin_=reference->GetTranslate(); presentationScale_=reference->GetScale();
    presentationLookAt_=toward->GetTranslate();
    const auto direction=presentationLookAt_-presentationOrigin_;
    presentationYaw_=std::atan2(direction.x,direction.z);
    presentationPreviousCamera_=CameraManager::GetInstance()->GetActiveCameraOverride();
    presentationCamera_=std::make_unique<Camera>(); presentationCamera_->Initialize();
    if (auto* camera=CameraManager::GetInstance()->GetActiveCamera())
        presentationCamera_->SetClipRange(camera->GetNearClip(),camera->GetFarClip());
    presentationCamera_->SetInputEnabled(false);
    if (auto* params=PostEffect::GetInstance()->GetParams()) presentationBars_=params->cinemaBarHeight;
}

void CinematicPlayer::ReleasePresentation(bool restore) {
    bindingsReady_=false;
    if (restore && presentationCaptured_) {
        const auto live=GetLiveTargets();
        for (auto& s:presentationObjects_)
            if (!s.owned && live.contains(s.target)) {
                s.target->SetTranslate(s.before.position); s.target->SetRotation(s.before.rotation);
                s.target->SetScale(s.before.scale); s.target->SetIsVisible(s.before.visible);
                if (auto* material=s.target->GetMaterialData()) *material=s.material;
                s.target->UpdateWorldMatrix(true);
            }
        for (const auto& hidden:presentationHidden_)
            if (live.contains(hidden.first)) hidden.first->SetIsVisible(hidden.second);
        if (presentationCamera_ && sceneManager_ && sceneManager_->GetCurrentScene()==boundScene_)
            if (auto* params=PostEffect::GetInstance()->GetParams()) params->cinemaBarHeight=presentationBars_;
    }
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
        if (!clip.enabled || clip.keys.empty()) continue;
        if (!live.contains(s.source) || !live.contains(s.target)) {
            isPlaying_=false; isPreviewing_=false; finished_=false;
            ReleasePresentation(true);
            return;
        }
        if (!clip.animate) continue;
        // 退避した姿勢は復元用に保ち、評価時だけ指定成分を現在の対象から借りる。
        struct { engine::Vector3 origin,rotation,scale,radial; } pose{s.origin,s.rotation,s.scale,s.radial};
        if (clip.followPosition) pose.origin=s.source->GetTranslate();
        if (clip.followRotation) pose.rotation=s.source->GetRotation();
        if (clip.followScale) pose.scale=s.source->GetScale();
        const auto k=VisualAt(clip,time);
        const auto position=PositionAt(pose,clip,k);
        s.target->SetTranslate(position);
        s.target->SetRotation(clip.inheritRotation?Product(pose.rotation,clip.rotationMask)+k.rotation:k.rotation);
        s.target->SetScale(clip.inheritScale?Product(pose.scale,k.scale):k.scale);
        s.target->SetColor(k.color); s.target->SetEmissive(k.emissive);
        s.target->SetIsVisible(k.visible && (!clip.inheritVisibility || s.source->GetIsVisible()));
        if (auto* material=s.target->GetMaterialData()) {
            material->portalClipEnabled=k.clipEnabled?1.0f:0.0f; material->portalClipProgress=0;
            material->portalClipCenter=position+engine::Vector3{0,k.clipHeight*(clip.clipScaleHeight?pose.scale.y:1.0f)+clip.clipCenterOffset,0};
            material->portalClipNormal={0,1,0}; material->portalClipEdgeWidth=clip.clipEdgeWidth;
            material->portalClipDissolve=clip.clipDissolve; material->portalClipColor=clip.clipColor;
        }
        s.target->UpdateWorldMatrix(true);
    }
    // 遅延フレームでも区間内の粒子を順に出す。停止・巻戻しのシークでは発火しない。
    if (dispatch && isPlaying_ && time>=previous)
        for (const auto& clip:p.particles) {
            size_t i=0;
            while (i<p.objects.size() && p.objects[i].name!=clip.target) ++i;
            if (i>=presentationObjects_.size() || p.objects[i].keys.empty()) continue;
            const auto& s=presentationObjects_[i];
            const int first=std::max(0,static_cast<int>(std::floor((previous-clip.startTime)/clip.interval))+1);
            const int last=static_cast<int>(std::floor((std::min(time,clip.endTime)-clip.startTime)/clip.interval));
            for (int tick=first;tick<=last;++tick) {
                const auto position=PositionAt(s,p.objects[i],VisualAt(p.objects[i],clip.startTime+tick*clip.interval));
                auto direction=clip.direction;
                if (clip.directionMode=="radialIn") {
                    const auto d=s.origin-position; const float length=std::sqrt(d.x*d.x+d.z*d.z);
                    if (length>0.001f) direction={d.x/length,clip.direction.y,d.z/length};
                }
                GPUParticleManager::GetInstance()->EmitDirected(clip.preset,position,direction,clip.scale);
            }
        }
    if (presentationCamera_ && !p.view.empty()) {
        const auto k=ViewAt(p.view,time);
        const auto from=k.reference.empty()?std::optional(presentationOrigin_):PresentationPosition(k.reference,live);
        const auto to=k.lookToward.empty()?std::optional(presentationLookAt_):PresentationPosition(k.lookToward,live);
        const auto eyeAnchor=k.eyeReference.empty()?std::optional(engine::Vector3{}):PresentationPosition(k.eyeReference,live);
        if (!from || !to || !eyeAnchor) {
            isPlaying_=false; isPreviewing_=false; finished_=false; ReleasePresentation(true); return;
        }
        const float yaw=p.viewWorldSpace?0.0f:presentationYaw_;
        const auto target=engine::Math::Lerp(*from,*to,k.focusMix)+
            Product(k.scaledFocusOffset,presentationScale_)+Rotate(k.focusOffset,yaw);
        const auto eye=(k.eyeReference.empty()?target:*eyeAnchor)+Rotate(k.eyeOffset,yaw)+k.jitter;
        const auto d=target-eye;
        const engine::Vector3 rotation{-std::atan2(d.y,std::sqrt(d.x*d.x+d.z*d.z)),std::atan2(d.x,d.z),0};
        presentationCamera_->ConfigFixedPoint(eye,rotation);
        presentationCamera_->ApplyReplayView(eye,target,rotation,k.fov,presentationCamera_->GetNearClip(),presentationCamera_->GetFarClip());
        CameraManager::GetInstance()->SetActiveCamera(presentationCamera_.get());
        if (auto* params=PostEffect::GetInstance()->GetParams()) params->cinemaBarHeight=k.bars;
    }
}

void CinematicPlayer::DrawPresentation(ID3D12Resource* pointLight, ID3D12Resource* spotLight) {
    if (!presentationCaptured_ || !sequence_) return;
    for (int transparent=0;transparent<2;++transparent)
        for (size_t i=0;i<presentationObjects_.size() && i<sequence_->presentation.objects.size();++i) {
            auto& s=presentationObjects_[i]; const auto& clip=sequence_->presentation.objects[i];
            if (s.owned && !clip.model.empty() && clip.enabled && clip.additive==(transparent!=0) && s.target->GetIsVisible()) {
                s.target->UpdateWorldMatrix(true); s.target->Draw(pointLight,spotLight);
            }
        }
}

} // namespace engine
