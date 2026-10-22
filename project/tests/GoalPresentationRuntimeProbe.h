#pragma once
#ifdef USE_IMGUI
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/system/progress/StageManager.h"
#include "game/system/progress/GameDataManager.h"
#include "game/system/scene/SceneGameplayAccess.h"
#include "tests/GateEntryRuntimeProbe.h"
#include "MeshEffectManager.h"
#include "GPUParticleManager.h"
#include "SceneManager.h"
#include "WinApp.h"
#include "CollisionManager.h"
#include "CollisionConfig.h"
#include <fstream>
#include <stdexcept>

// 専用worktreeの明示起動で、実モデル・実カメラ・GPU描画とプレビュー復元を検証する。
namespace GoalPresentationRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_GOAL_PRESENTATION_TEST",nullptr,0)>0; }
struct Access {
    static void Begin(GamePlayScene* game) { game->StartGoalPresentationPreview(); }
    static void End(GamePlayScene* game) { game->StopGoalPresentationPreview(); }
    static Camera* CameraFor(GamePlayScene* game) { return game->goalPresentationCamera_.get(); }
    static float Time(GamePlayScene* game) { return game->goalPresentationTimer_; }
    static engine::Object3d* Crown(GamePlayScene* game) { return game->FindGoalCrownObject(); }
};
inline void Require(bool value,const char* error) { if (!value) throw std::runtime_error(error); }
inline void CheckFrame(engine::Object3d* object,Camera* camera,float top) {
    const auto lo=object->GetModel()->GetLocalAabbMin(),hi=object->GetModel()->GetLocalAabbMax();
    for(int i=0;i<8;++i) {
        const auto world=engine::Math::Transform({(i&1)?hi.x:lo.x,(i&2)?hi.y:lo.y,(i&4)?hi.z:lo.z},object->GetWorldMatrix());
        const auto view=engine::Math::Transform(world,camera->GetViewMatrix());
        Require(view.z>0.05f,"subject went behind camera");
        const auto ndc=engine::Math::Transform(world,camera->GetViewProjectionMatrix());
        Require(std::isfinite(ndc.x) && std::isfinite(ndc.y),"invalid projection");
        if(!(ndc.x>=-0.825f && ndc.x<=0.825f && ndc.y>=-0.825f && ndc.y<=top+0.025f))
            throw std::runtime_error(object->GetName()+" ndc="+std::to_string(ndc.x)+","+std::to_string(ndc.y)+" eye="+std::to_string(camera->GetEye().x)+","+std::to_string(camera->GetEye().y)+","+std::to_string(camera->GetEye().z));
    }
}
inline float FootHeight(Player* p) {
    const auto lo=p->GetModel()->GetLocalAabbMin(),hi=p->GetModel()->GetLocalAabbMax();
    return engine::Math::Transform({(lo.x+hi.x)*0.5f,lo.y,(lo.z+hi.z)*0.5f},p->GetWorldMatrix()).y;
}
inline void AfterDraw(SceneManager* manager,const std::string& path) {
    static std::ofstream log(path,std::ios::trunc);
    static ULONGLONG started=GetTickCount64();
    static int phase=0,stage=0,frames=0;
    static bool done=false,landing=false,apex=false,result=false;
    static int cleared=0;
    static engine::Vector3 crownPos{},crownScale{},crownRotation{},playerPos{},playerRotation{};
    static Camera* originalCamera=nullptr;
    static std::string idle1,idle2;
    static float settledFoot=0,baseRoughness=0,baseEnvIntensity=0;
    static bool baseEnvMap=false,footCaptured=false;
    if(done || !manager)return;
    auto finish=[&](const std::string& message){log<<message<<std::endl;done=true;WinApp::CloseNow();};
    try {
        Require(GetTickCount64()-started<360000,"goal presentation test timeout");
        if(manager->IsTransitioning())return;
        if(phase==0) {
            StageManager::GetInstance()->SetCurrentStageById(stage==0?"stage1":"stage2");
            manager->ChangeScene("GAMEPLAY");phase=1;frames=0;return;
        }
        auto* game=dynamic_cast<GamePlayScene*>(manager->GetCurrentScene());
        if(!game)return;
        auto* player=GetScenePlayer(game);auto* crown=Access::Crown(game);
        if(phase==1) {
            if(++frames<5 || !player || !player->IsControlActive() || player->IsCinematicLocked())return;
            Require(player && crown && player->GetModel() && crown->GetModel(),"missing goal scene models");
            crownPos=crown->GetTranslate();crownScale=crown->GetScale();crownRotation=crown->GetRotation();
            idle1=crown->GetMeshEffect1Name();idle2=crown->GetMeshEffect2Name();
            baseRoughness=crown->GetRoughness();baseEnvIntensity=crown->GetEnvIntensity();baseEnvMap=crown->GetEnableEnvMap();
            originalCamera=CameraManager::GetInstance()->GetActiveCameraOverride();
            // 空中取得を再現し、配置サイズ1と2.8の両方を実アセットで通す。
            auto toward=player->GetWorldPosition()-crown->GetWorldPosition();toward.y=0;
            toward=engine::Math::Length(toward)>0.01f?engine::Math::Normalize(toward):engine::Vector3{-1,0,0};
            auto pickupPosition=crown->GetWorldPosition()+toward*2.0f;
            PhysicsQueryFilter filter;filter.mask=CollisionCategories::Get("ground");filter.ignoredObject=player;
            const auto floor=CollisionManager::GetInstance()->Raycast(pickupPosition+engine::Vector3{0,12,0},{0,-1,0},60.0f,filter);
            Require(floor.isHit,"no floor below crown approach");
            pickupPosition.y=floor.hitPoint.y-player->GetModel()->GetLocalAabbMin().y*player->GetScale().y+2.5f;
            player->SetTranslate(pickupPosition);player->SetVelocity({0,-5,0});player->UpdateWorldMatrix(true);
            playerPos=player->GetTranslate();playerRotation=player->GetRotation();
            cleared=GameDataManager::GetInstance()->GetClearedStageCount();
            Access::Begin(game);Require(game->IsGoal(),"preview did not start");
            log<<"START stage="<<stage+1<<" authoredCrownScale="<<crownScale.x<<std::endl;
            phase=2;frames=0;landing=apex=result=false;return;
        }
        if(phase==2) {
            const float t=Access::Time(game);
            auto* camera=CameraManager::GetInstance()->GetActiveCamera();
            Require(camera && player && crown,"presentation lost actors or camera");
            Require(camera==Access::CameraFor(game),"active camera was replaced by editor");

            if(t>0.05f) {
                CheckFrame(player,camera,t>=3.1f?0.24f:0.8f);
                CheckFrame(crown,camera,t>=3.1f?0.24f:0.8f);
            }
            if(t>1.0f) Require(crown->GetMeshEffect1Name().empty() && crown->GetMeshEffect2Name().empty(),"idle crown effects were left running");
            if(t>=1.1f && !footCaptured) { settledFoot=FootHeight(player);footCaptured=true; }
            if(t>=2.35f && !landing) {
                GateEntryRuntimeProbe::Capture(path+".stage"+std::to_string(stage+1)+".wear.png");landing=true;
                log<<"PASS crown moved and idle effects stopped"<<std::endl;
            }
            if(t>=3.08f && !apex) {
                GateEntryRuntimeProbe::Capture(path+".stage"+std::to_string(stage+1)+".jump.png");apex=true;
            }
            if(t>=4.3f && !result) {
                Require(std::abs(FootHeight(player)-settledFoot)<0.10f,"result pose did not settle on the ground");
                const auto bodySize=player->GetModel()->GetLocalAabbMax()-player->GetModel()->GetLocalAabbMin();
                const auto crownSize=crown->GetModel()->GetLocalAabbMax()-crown->GetModel()->GetLocalAabbMin();
                const float ratio=crownSize.x*std::abs(crown->GetScale().x)/(bodySize.x*std::abs(player->GetScale().x));
                Require(ratio>0.65f && ratio<0.98f,"worn crown size did not match player");
                GateEntryRuntimeProbe::Capture(path+".stage"+std::to_string(stage+1)+".result.png");result=true;
                log<<"PASS wearRatio="<<ratio<<" foot="<<FootHeight(player)<<" screen bounds fit with UI"<<std::endl;
            }
            if(t>=5.6f) {
                Require(landing && apex && result,"key poses were not rendered");
                Access::End(game);
                Require(!game->IsGoal() && !player->IsCinematicLocked(),"preview retained cinematic state");
                Require(engine::Math::Length(crown->GetTranslate()-crownPos)<0.001f && engine::Math::Length(crown->GetScale()-crownScale)<0.001f,"crown pose was not restored");
                Require(engine::Math::Length(player->GetTranslate()-playerPos)<0.001f,"player position was not restored");
                Require(crown->GetMeshEffect1Name()==idle1 && crown->GetMeshEffect2Name()==idle2,"idle effects were not restored");
                Require(crown->GetEnableEnvMap()==baseEnvMap && std::abs(crown->GetRoughness()-baseRoughness)<0.001f && std::abs(crown->GetEnvIntensity()-baseEnvIntensity)<0.001f,"crown material was not restored");
                Require(CameraManager::GetInstance()->GetActiveCameraOverride()==originalCamera,"camera was not restored");
                Require(GameDataManager::GetInstance()->GetClearedStageCount()==cleared,"preview changed progress");
                log<<"PASS restore and progress stage="<<stage+1<<std::endl;
                if(++stage==2){finish("PASS: goal clear crown fit, airborne pickup, safe camera, VFX lifecycle, stable result and preview restoration");return;}
                settledFoot=0;footCaptured=false;phase=0;return;
            }
            // 本編と同じ実更新・描画で次のフレームを待つ。
        }
    } catch(const std::exception& error) { GateEntryRuntimeProbe::Capture(path+".failed.png"); finish(std::string("FAIL: ")+error.what()); }
}
}
#endif
