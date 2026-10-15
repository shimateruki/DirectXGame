#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "game/system/CrownDefeatTuning.h"
#include "CameraManager.h"
#include "PostEffect.h"
#include "SceneManager.h"
#include "VFXSequencer.h"
#include "GPUParticleManager.h"
#include "GameDataManager.h"
#include "StageManager.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace {
constexpr float kPi = 3.14159265f;
// 王冠敗北演出の時間を滑らかな補間率へ変換します。
float Ease(float x) {
    // 時刻を範囲内へ収め、演出の切り替わりを滑らかにします。
    x=std::clamp(x,0.0f,1.0f);return x*x*(3-2*x);
}
engine::Vector3 Mix(const engine::Vector3& a,const engine::Vector3& b,float t) {
    // カメラと注目点を同じ補間係数で移動させます。
    return a+(b-a)*t;
}
engine::Vector3 RotateY(engine::Vector3 v,float angle) {
    // 破片のローカル位置を、ボスの向きに合わせたワールド方向へ回します。
    return {v.x*std::cos(angle)+v.z*std::sin(angle),v.y,-v.x*std::sin(angle)+v.z*std::cos(angle)};
}

engine::Vector3 Product(const engine::Vector3& a,const engine::Vector3& b) {
    // 破片の支点にボスの非等方スケールを適用します。
    return {a.x*b.x,a.y*b.y,a.z*b.z};
}
engine::Vector3 Look(const engine::Vector3& eye,const engine::Vector3& target) {
    // 注目点への方向から演出カメラのピッチとヨーを求めます。
    const auto d=target-eye;return {-std::atan2(d.y,std::sqrt(d.x*d.x+d.z*d.z)),std::atan2(d.x,d.z),0};
}
}

bool GamePlayScene::StartCrownDefeat(EnemyFalseKingSlime* boss) {
    // 演出資源の読み込みに成功してから入力と通常の撃破処理を止めます。
    if (!boss || !player_ || player_->isDead || crownDefeat_.active || crownDefeat_.completed || crownFinale_.active || isGoal_)
        return false;
    Camera* camera=CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return false;
    // 破片データを先に検証し、準備に失敗した状態で通常死亡処理を止めないようにします。
    std::ifstream file("Resources/3DModel/Characters/false_king_slime/false_king_defeat_parts.json");
    const auto parts=nlohmann::json::parse(file,nullptr,false);
    if (!parts.is_array() || parts.empty()) return false;
    auto& s=crownDefeat_;
    s.boss=boss;s.bossPosition=boss->GetTranslate();s.bossScale=boss->GetScale();s.bossRotation=boss->GetRotation();
    s.bossVisible=boss->GetIsVisible();
    s.playerPosition=player_->GetTranslate();s.playerScale=player_->GetScale();s.playerRotation=player_->GetRotation();
    s.playerYaw=player_->GetMoveYaw();s.hadControl=player_->IsControlActive();
    s.previousCamera=CameraManager::GetInstance()->GetActiveCameraOverride();
    s.camera=std::make_unique<Camera>();s.camera->Initialize();
    s.camera->SetClipRange(camera->GetNearClip(),camera->GetFarClip());s.camera->SetInputEnabled(false);
    s.eye=camera->GetEye();s.target=camera->GetTargetPoint();
    s.forward=s.playerPosition-s.bossPosition;s.forward.y=0;
    const float length=std::sqrt(s.forward.x*s.forward.x+s.forward.z*s.forward.z);
    s.forward=length>.001f?s.forward*(1.0f/length):engine::Vector3{0,0,1};
    s.right={s.forward.z,0,-s.forward.x};
    auto make=[&](const std::string& model,bool light=false) {
        auto object=std::make_unique<engine::Object3d>();object->Initialize(GetObject3dCommon());object->SetModel(model);
        object->SetEditorInternal(true);object->SetCollisionAttribute(0);object->SetCollisionMask(0);
        object->SetMaterialType(light?1:0);object->SetEnableLighting(!light);object->SetEnableEnvMap(!light);
        object->SetEnvIntensity(.55f);object->SetEmissive(light?2.0f:1.0f);
        if(light)object->SetBlendMode(BlendMode::kAdd);
        return object;
    };
    s.body=make("Characters/false_king_slime/false_king_defeat_body.obj");
    s.cracks=make("Characters/false_king_slime/false_king_defeat_cracks.obj",true);
    s.ring=make("Characters/false_king_slime/false_king_pressure_front.obj",true);
    for(const auto& p:parts) {
        if(!p.contains("model") || !p.contains("pivot") || p["pivot"].size()!=3) {ResetCrownDefeat();return false;}
        CrownDefeatPart part;part.visual=make(p["model"].get<std::string>());
        part.pivot={p["pivot"][0].get<float>(),p["pivot"][1].get<float>(),p["pivot"][2].get<float>()};
        part.gem=p.value("gem",false);s.parts.push_back(std::move(part));
    }
    for(int i=0;i<24;++i)s.rays.push_back(make("Characters/false_king_slime/false_king_defeat_ray.obj",true));
    if(auto* params=PostEffect::GetInstance()->GetParams())s.bars=params->cinemaBarHeight;
    player_->BeginCinematicLock(true);player_->SetVelocity({});
    // Idleへの遷移で通常スライム色へ戻るため、継承した王の色を演出中も保持します。
    if(player_->GetEnemyMorphType()==Player::EnemyMorphType::Crown) {
        player_->SetMaterialType(0);player_->SetColor({1,.77f,.25f,1});
    }
    boss->HoldFinalDefeat();boss->SetIsVisible(false);
    s.active=true;s.time=0;
    CameraManager::GetInstance()->SetActiveCamera(s.camera.get());
    UpdateCrownDefeat(0);
    return true;
}

// 絶対時刻で姿勢を評価します。巻き戻し・フレーム送りでも同じ破片軌道とカメラになります。
void GamePlayScene::UpdateCrownDefeat(float deltaTime) {
    auto& s=crownDefeat_;if(!s.active || !player_ || !s.boss)return;
    const auto& t=CrownDefeatTuning::Get();
    const float previous=s.time;s.time=std::min(t.duration,s.time+std::max(0.0f,deltaTime));
    const float now=s.time/t.duration, before=previous/t.duration;
    const auto crossed=[&](float mark){return before<mark && now>=mark;};
    const float burst=t.rupture*t.duration;
    const float age=std::max(0.0f,s.time-burst);
    const float charge=Ease((now-.07f)/(t.rupture-.07f));
    const float shrink=Ease((now-t.shrink)/(t.settle-t.shrink));
    const float finish=Ease((now-t.victory)/.09f);
    const float recoil=std::sin(std::clamp((s.time-.12f)/.65f,0.0f,1.0f)*kPi)*.14f;
    const float panic=now<t.rupture?std::sin(s.time*24)*.015f*charge:0;
    const engine::Vector3 center=s.bossPosition+engine::Vector3{0,s.bossScale.y*3.05f,0};
    const float bodySize=1.0f-.83f*shrink;
    engine::Vector3 bodyScale=s.bossScale*bodySize;
    const float flop=std::sin(std::clamp((now-t.settle)/.06f,0.0f,1.0f)*kPi);
    bodyScale.x*=1+recoil+flop*.35f;bodyScale.y*=1-recoil-flop*.28f;
    s.body->SetScale(bodyScale);s.body->SetTranslate(s.bossPosition);
    s.body->SetRotation(s.bossRotation+engine::Vector3{panic,0,recoil*.35f});
    s.body->SetColor({.78f+.22f*shrink,.65f+.35f*shrink,1,1});
    s.body->SetEmissive(1.0f);
    s.body->UpdateWorldMatrix(true);
    const float pulse=.7f+.3f*std::sin(s.time*(8+charge*13));
    s.cracks->SetIsVisible(now>.065f && now<t.rupture);
    s.cracks->SetTranslate(s.bossPosition);s.cracks->SetScale(s.bossScale);
    s.cracks->SetRotation(s.bossRotation);
    s.cracks->SetColor({1,.35f+.55f*charge,1,charge*pulse});
    s.cracks->SetEmissive(1+charge*4*t.energy);
    auto* crackMaterial=s.cracks->GetMeshRenderer()->GetMaterialData();
    crackMaterial->portalClipEnabled=1;crackMaterial->portalClipProgress=0;
    crackMaterial->portalClipCenter=s.bossPosition+engine::Vector3{0,(3.05f+1.55f*charge)*s.bossScale.y-.52f,0};
    crackMaterial->portalClipNormal={0,1,0};crackMaterial->portalClipEdgeWidth=.045f;
    crackMaterial->portalClipDissolve=.07f;crackMaterial->portalClipColor={1,.7f,1,1};
    for(size_t i=0;i<s.parts.size();++i) {
        auto& part=s.parts[i];auto* model=part.visual.get();
        const auto pivot=RotateY(Product(part.pivot,s.bossScale),s.bossRotation.y);
        auto position=s.bossPosition+pivot;auto rotation=s.bossRotation;
        const float radial=std::sqrt(pivot.x*pivot.x+pivot.z*pivot.z);
        const engine::Vector3 direction=radial>.01f?engine::Vector3{pivot.x/radial,0,pivot.z/radial}:s.right;
        if(now>=t.rupture) {
            const float speed=(part.gem?5.8f:4.1f)*t.spread;
            // 初速・重力から求めた放物線。接地後は短い反発だけを残し、床を貫通させません。
            const float travel=std::min(age,1.65f);
            position=position+direction*(speed*travel);
            const float height=2.4f*age-5.4f*age*age;
            const float floor=s.bossPosition.y+.18f;
            position.y=std::max(floor,position.y+height);
            if(position.y<=floor+.001f)position.y+=.16f*std::abs(std::sin(age*13))*std::exp(-age*1.1f);
            const float spin=std::min(age,1.6f);
            rotation=rotation+engine::Vector3{spin*(1.2f+i*.07f),spin*(i%2?1.5f:-1.2f),spin*.7f};
        } else {
            position=position+direction*(.04f*charge*std::sin(s.time*30+i));
        }
        model->SetTranslate(position);model->SetRotation(rotation);model->SetScale(s.bossScale);
        model->SetEmissive(1+charge*.15f*t.energy*(now<t.rupture?1.0f:0.0f));
        const float dissolve=Ease((age-2.0f)/1.7f);
        model->SetIsVisible(dissolve<.999f);
        auto* material=model->GetMeshRenderer()->GetMaterialData();
        material->portalClipEnabled=dissolve>0?1.0f:0.0f;material->portalClipProgress=0;
        material->portalClipCenter=position+engine::Vector3{0,5-10*dissolve-.52f,0};
        material->portalClipNormal={0,1,0};material->portalClipDissolve=.20f;
        material->portalClipEdgeWidth=.075f;material->portalClipColor={.9f,.5f,1,1};
        model->UpdateWorldMatrix(true);
    }
    s.ring->SetIsVisible(now>=t.rupture && age<.85f);
    s.ring->SetTranslate(center);const float radius=1.0f+age*23;
    s.ring->SetScale({radius,.9f,radius});s.ring->SetColor({.9f,.55f,1,(1-Ease(age/.85f))*.8f});
    for(size_t i=0;i<s.rays.size();++i) {
        auto& ray=s.rays[i];const float a=static_cast<float>(i)*2.399963f;
        const float localAge=age-static_cast<float>(i%3)*.025f;
        const float life=.35f+static_cast<float>(i%4)*.045f;
        ray->SetIsVisible(now>=t.rupture && localAge>=0 && localAge<life);
        ray->SetTranslate(center);ray->SetRotation({.5f+static_cast<float>(i%5)*.43f,a,0});
        const float fade=1-Ease(localAge/life);
        ray->SetScale({fade, (2+static_cast<float>(i%7))*(.4f+.6f*Ease(localAge/.06f)),fade});
        ray->SetColor({.85f,.62f,1,fade});ray->SetEmissive(2.5f*t.energy);
    }
    // 持続粒子の発生回数も時刻の境界で決め、停止中や毎描画フレームの重複発生を防ぎます。
    const bool tick=static_cast<int>(s.time*9)>static_cast<int>(previous*9);
    if(tick && now>.09f && now<t.rupture) {
        const float a=s.time*5;const float r=(1-charge*.45f)*4;
        GPUParticleManager::GetInstance()->EmitDirected("crown_defeat_charge",center+engine::Vector3{std::cos(a)*r,1,std::sin(a)*r},{-std::cos(a),-.4f,-std::sin(a)},.16f*t.energy);
    }
    if(crossed(t.rupture))VFXSequencer::PlayOneShot("crown_defeat_rupture_cue",center,{t.energy,t.energy,t.energy});
    if(tick && now>=t.shrink && now<t.settle) {
        GPUParticleManager::GetInstance()->EmitDirected("crown_defeat_charge",s.bossPosition+engine::Vector3{0,bodyScale.y*1.3f,0},{0,1,0},.2f*t.energy);
    }
    // 王冠能力は保持し、追加の変身をせずに小さな跳躍で勝利を見せます。
    engine::Vector3 playerPosition=s.playerPosition;
    const float hop=std::sin(std::clamp((now-t.victory)/.11f,0.0f,1.0f)*kPi);
    playerPosition.y+=hop*1.1f;
    player_->SetTranslate(playerPosition);player_->SetScale(s.playerScale);
    if(now>=t.victory)player_->SetMoveYaw(std::atan2(s.forward.x,s.forward.z));
    else player_->SetMoveYaw(s.playerYaw);
    if(crossed(t.victory+.11f))VFXSequencer::PlayOneShot("crown_victory_land_cue",s.playerPosition,{.7f,.7f,.7f});
    if(tick && now>t.victory)GPUParticleManager::GetInstance()->EmitDirected("goal_clear_gold_motes",playerPosition+engine::Vector3{0,1,0},{0,1,0},.35f);
    player_->UpdateWorldMatrix(true);
    // 亀裂の寄り → 破砕の引き → 小スライム → 王の勝利。注目点を切り替えて因果を見せます。
    engine::Vector3 focus=s.bossPosition+engine::Vector3{0,3.0f*s.bossScale.y,0};
    engine::Vector3 offset=s.forward*13+s.right*5+engine::Vector3{0,4,0};
    if(now<t.rupture) {
        const float dolly=Ease((now-.03f)/(t.rupture-.03f));
        offset=Mix(s.forward*19+s.right*7+engine::Vector3{0,3,0},offset,dolly);
    } else if(now<t.shrink) {
        offset=s.forward*20+s.right*9+engine::Vector3{0,7,0};
    } else {
        focus=Mix(focus,s.bossPosition+engine::Vector3{0,.65f,0},shrink);
        offset=Mix(s.forward*20+s.right*9+engine::Vector3{0,7,0},s.forward*7+s.right*3+engine::Vector3{0,2.3f,0},shrink);
    }
    if(now>=t.victory) {
        focus=Mix(focus,playerPosition+engine::Vector3{0,1.0f,0},finish);
        offset=Mix(offset,s.forward*7+s.right*1.8f+engine::Vector3{0,1.5f,0},finish);
    }
    engine::Vector3 eye=focus+offset*t.cameraDistance;
    const float shake=(now>=t.rupture?std::exp(-age*6)*.22f:0)*t.shake;
    eye.x+=std::sin(s.time*93)*shake;eye.y+=std::sin(s.time*117)*shake;
    auto* camera=s.camera.get();camera->SetFollowTarget(nullptr);camera->SetFollowMode(Camera::FollowMode::kFixedPoint);
    camera->SetEye(eye);camera->SetTarget(focus);camera->SetRotation(Look(eye,focus));
    camera->ConfigFixedPoint(eye,Look(eye,focus));camera->SetFovY(.62f);camera->Update();
    CameraManager::GetInstance()->SetActiveCamera(camera);
    if(auto* params=PostEffect::GetInstance()->GetParams())params->cinemaBarHeight=.10f*Ease(s.time/.20f);
    if(s.time>=t.duration && !crownMoviePreview_.active)FinishCrownDefeat();
}

void GamePlayScene::FinishCrownDefeat() {
    // ボスの撃破を確定し、通常のゴール演出へ制御を引き継ぎます。
    auto& s=crownDefeat_;if(!s.active || crownMoviePreview_.active)return;
    s.active=false;s.completed=true;
    // 完了フラグを立ててからイベントを通知し、報酬側の旧カメラを重ねて再生しません。
    s.boss->CompleteFinalDefeat();
    for(const auto& object:GetObjects())if(object->GetName()=="Stage3_HighCrown_VictoryCrown")object->SetIsVisible(false);
    goalWasStageCleared_=GameDataManager::GetInstance()->IsStageCleared(StageManager::GetInstance()->GetCurrentStageIndex());
    SetIsGoal(true);goalPresentationState_=GoalPresentationState::Celebrating;
    goalPresentationTimer_=std::max(0.0f,goalClearPlayerAnimator_.GetTuning().readyTime-2.5f);
    goalPlayerSnapshotValid_=false;goalCrownSnapshotValid_=false;goalCameraSnapshotValid_=false;
    goalCinematicPlayer_.Stop(false);
    UpdateGoalPresentationOverlay();
}

void GamePlayScene::ResetCrownDefeat() {
    // 演出で変更したボス・Player・カメラの状態を控えた値へ戻す。
    auto& s=crownDefeat_;
    if(!s.boss && !s.camera)return;
    if(s.boss) {s.boss->CancelFinalDefeat();s.boss->SetIsVisible(s.bossVisible);}
    if(player_) {
        player_->SetTranslate(s.playerPosition);player_->SetScale(s.playerScale);player_->SetRotation(s.playerRotation);
        player_->SetMoveYaw(s.playerYaw);player_->EndCinematicLock(s.hadControl);
    }
    if(CameraManager::GetInstance()->GetActiveCameraOverride()==s.camera.get())CameraManager::GetInstance()->SetActiveCamera(s.previousCamera);
    if(auto* params=PostEffect::GetInstance()->GetParams())params->cinemaBarHeight=s.bars;
    s=CrownDefeatState{};
}

void GamePlayScene::DrawCrownDefeatWorld() {
    // 本体と破片を先に描き、半透明の光は最後に重ねます。
    auto& s=crownDefeat_;if(!s.active && !s.completed)return;
    auto draw=[&](engine::Object3d* object) {
        if(object && object->GetIsVisible()) {object->UpdateWorldMatrix(true);object->Draw(pointLightResource_.Get(),spotLightResource_.Get());}
    };
    draw(s.body.get());
    for(auto& part:s.parts)draw(part.visual.get());
    // 半透明の亀裂と光は、不透明な王冠を描いた後に重ねます。
    draw(s.cracks.get());draw(s.ring.get());
    for(auto& ray:s.rays)draw(ray.get());
}
