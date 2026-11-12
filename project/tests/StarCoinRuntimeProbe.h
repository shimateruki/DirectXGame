#pragma once
#ifdef USE_IMGUI
#include "game/scene/gameplay/GamePlayScene.h"
#include <numbers>
#include "game/system/progress/GameDataManager.h"
#include "game/system/progress/StageManager.h"
#include "game/system/progress/StarCoinPresentation.h"
#include "tests/GateEntryRuntimeProbe.h"
#include "GpuParticleManager.h"
#include "game/system/rules/EventManager.h"
#include "CollisionConfig.h"
#include "json.hpp"
#include <fstream>
#include <stdexcept>

// 明示起動した専用プロセスで、保存済みと未取得のモデル・粒子・再接触を確認する。
namespace StarCoinRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_SILVER_STAR_TEST",nullptr,0)>0; }
inline void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
inline engine::Object3D* Find(BaseScene* scene,int index) {
    for(auto& object:scene->GetObjects())
        if(object && object->GetEventType()==EventType::StarCoin && object->GetTargetId()==index)return object.get();
    return nullptr;
}
inline bool Near(float a,float b) { return std::abs(a-b)<0.001f; }
inline void AfterDraw(SceneManager* manager,const std::string& path) {
    static std::ofstream log(path,std::ios::trunc);
    static ULONGLONG start=GetTickCount64();
    static int phase=0,stage=0,frames=0,savedCount=0;
    static bool done=false;
    static std::unique_ptr<Camera> camera;
    static Camera* previousCamera=nullptr;
    static engine::Object3D* silver=nullptr;
    static engine::Object3D* gold=nullptr;
    auto finish=[&](const std::string& message){log<<message<<std::endl;done=true;CameraManager::GetInstance()->SetActiveCamera(previousCamera);WinApp::CloseNow();};
    if(done || !manager)return;
    try {
        Require(GetTickCount64()-start<360000,"silver star test timeout");
        if(manager->IsTransitioning())return;
        auto* data=GameDataManager::GetInstance();
        if(phase==0) {
            const char* id=stage==0?"stage1":"stage2";
            StageManager::GetInstance()->SetCurrentStageById(id);
            const int stageIndex=StageManager::GetInstance()->GetCurrentStageIndex();
            for(int i=0;i<3;++i)data->SetStarCoinCollected(stageIndex,i,i!=1);
            savedCount=data->GetCollectedStarCoinCount();
            manager->ChangeScene("GAMEPLAY");phase=1;frames=0;return;
        }
        auto* scene=dynamic_cast<GamePlayScene*>(manager->GetCurrentScene());
        if(!scene)return;
        auto* player=GetScenePlayer(scene);
        if(phase==1) {
            if(++frames<15 || !player || !player->IsControlActive())return;
            nlohmann::json authored;std::ifstream file("Resources/json/3Dobject/"+std::string(stage==0?"stage1":"stage2")+"_object.json");file>>authored;
            for(int i=0;i<3;++i) {
                auto* object=Find(scene,i);Require(object,"missing stage star ID");
                Require(StarCoinPresentation::IsSilver(*object)==(i!=1),"save flag did not select model");
                if(i!=1) {
                    Require(object->GetGpuParticleName()==StarCoinPresentation::kSilverIdle,"silver idle preset missing");
                    const auto c=object->GetColor();Require(c.z>=c.x && c.x>0.4f,"silver material still gold or too dark");
                    for(const auto& entry:authored.at("objects"))if(entry.value("name","")==object->GetName()) {
                        Require(object->GetCollisionAttribute()==entry.at("collisionAttribute").get<uint32_t>(),"authored collision attribute changed");
                        Require(object->GetCollisionMask()==entry.at("collisionMask").get<uint32_t>(),"authored collision mask changed");
                        const auto size=entry.at("collider").at("size");const auto actual=object->GetColliderConfig().size;
                        Require(Near(actual.x,size[0]) && Near(actual.y,size[1]) && Near(actual.z,size[2]),"model auto-fit changed pickup size");
                    }
                }
            }
            const auto& presets=GpuParticleManager::GetInstance()->GetPresets();
            Require(presets.count(StarCoinPresentation::kSilverIdle) && presets.count(StarCoinPresentation::kSilverGet),"silver GPU presets not loaded");
            silver=Find(scene,0);gold=Find(scene,1);
            // 銀の状態で保存した配置でも、別セーブの未取得状態へ復元できる。
            const auto goldCollider=gold->GetColliderConfig();
            StarCoinPresentation::ApplyPreviouslyCollected(*gold);
            StarCoinPresentation::RestoreUncollectedIfSilver(*gold);
            Require(!StarCoinPresentation::IsSilver(*gold) && gold->GetModelName()==StarCoinPresentation::kGoldModel,"silver authoring state leaked into fresh save");
            const auto restoredCollider=gold->GetColliderConfig();
            Require(Near(restoredCollider.size.x,goldCollider.size.x) && Near(restoredCollider.size.y,goldCollider.size.y) && Near(restoredCollider.size.z,goldCollider.size.z),"gold restore changed collider");
            const auto center=player->GetWorldPosition()+engine::Vector3{0,3,8};
            for(auto* object:{silver,gold}) {
                object->SetParent(nullptr,true);object->SetIsVisible(true);
                object->SetTranslate(center+engine::Vector3{object==silver?-2.6f:2.6f,0,0});
                object->SetRotation({(std::numbers::pi_v<float> * 0.5f),0,0});object->SetScale({0.05f,0.05f,0.05f});object->UpdateWorldMatrix(true);
            }
            previousCamera=CameraManager::GetInstance()->GetActiveCameraOverride();
            camera=std::make_unique<Camera>();camera->Initialize();camera->SetFovY(0.65f);
            Camera::CameraOverrideParams view;view.duration=0;view.fixedEyePos=center+engine::Vector3{0,3,-17};
            view.fixedTargetPos=center;view.trackTargetX=view.trackTargetY=view.trackTargetZ=false;
            camera->StartOverride(view);camera->Update();CameraManager::GetInstance()->SetActiveCamera(camera.get());
            log<<"PASS stage="<<stage+1<<" saved models, silver presets, fresh-save restore and authored collision preserved"<<std::endl;
            phase=2;frames=0;return;
        }
        if(phase==2) {
            if(++frames<45)return;
            GateEntryRuntimeProbe::Capture(path+".stage"+std::to_string(stage+1)+".idle.png");
            // 実際のPlayer接触入口からGameRuleを通す。途中の再接触で寿命が戻らないことも確認する。
            for(auto* object:{silver,gold}) {
                // 遭遇で出現するスターも、この取得テストの間だけ出現済みの状態へ揃える。
                object->SetCollisionAttribute(CollisionCategories::Get("trigger"));
                object->SetCollisionMask(CollisionCategories::Get("player"));
                player->SetTranslate(object->GetWorldPosition());player->SetVelocity({0,0,0});player->UpdateWorldMatrix(true);
                Require(player->OnCollision(object),"player contact rejected");
                Require(object->IsCollecting(),"contact did not start collection");
                Require(object->GetCollisionAttribute()==0 && object->GetCollisionMask()==0,"collection collision stayed enabled");
                Require(object->GetGpuParticleName().empty(),"idle particles remained during collection");
                object->Update(0.30f);
                PlayerHitEvent repeated;repeated.me=player;repeated.hitObject=object;EventManager::GetInstance()->Dispatch(repeated);
                object->Update(0.21f);
                Require(object->IsDead() && !object->GetIsVisible(),"repeated contact restarted collection");
            }
            Require(data->GetCollectedStarCoinCount()==savedCount,"recollect changed saved star count");
            log<<"PASS stage="<<stage+1<<" gold/silver pickup, duplicate contact ignored, saved count unchanged"<<std::endl;
            silver=gold=nullptr;phase=3;frames=0;return;
        }
        if(phase==3) {
            if(++frames<2)return;
            GateEntryRuntimeProbe::Capture(path+".stage"+std::to_string(stage+1)+".pickup.png");
            CameraManager::GetInstance()->SetActiveCamera(previousCamera);camera.reset();previousCamera=nullptr;
            if(++stage<2){phase=0;return;}
            finish("PASS: silver star models, idle/pickup effects, preserved collision, duplicate pickup and save count in Stage1/2");
        }
    }catch(const std::exception& error){GateEntryRuntimeProbe::Capture(path+".failed.png");finish(std::string("FAIL: ")+error.what());}
}
} // namespace StarCoinRuntimeProbe
#endif
