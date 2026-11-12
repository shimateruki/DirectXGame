#pragma once
#ifdef USE_IMGUI
#include "DebugEditor.h"
#include "EditorManager.h"
#include "SceneManager.h"
#include "WinApp.h"
#include "Player.h"
#include "game/editor/CopyAbilityPreviewRegistry.h"
#include "game/editor/EnemyAttackPreviewRegistry.h"
#include "game/editor/RegisterGameActorDebug.h"
#include "tests/EnemyProjectileRuntimeProbe.h"
#include <fstream>
#include <stdexcept>

// 明示した専用実行だけで、登録前の入力契約と実Actorへの接続を検証します。
struct PreviewCompositionRuntimeProbe {
    struct Case { std::string type, action; };
    static bool Enabled() { return GetEnvironmentVariableA("CG2_PREVIEW_COMPOSITION_TEST",nullptr,0)>0; }
    static void Require(bool value,const char* text) { if(!value)throw std::runtime_error(text); }
    static PlayerCopyAbilityInput LegacyInput(int action,float time,float deltaTime,bool trigger) {
        PlayerCopyAbilityInput input{};
        const bool held=time>=0.5f && time<2.0f;
        input.primaryTriggered=trigger && (action==1 || action==2);
        if(action==7) input.primaryTriggered=trigger || (time>=0.90f && time-deltaTime<0.90f) || (time>=1.55f && time-deltaTime<1.55f);
        input.primaryHeld=(action==2 && held) || (action==1 && trigger);
        input.secondaryTriggered=trigger && action==3;
        input.secondaryHeld=held && action==3;
        input.specialTriggered=trigger && action==4;
        return input;
    }
    // 旧入力表の期待値と登録された入力合成を比較し、押下・保持・解除の対応を確認する。
    static void CheckInputs(std::vector<Case>& cases,std::ofstream& log) {
        const std::vector<std::string> types={"Slime","FireSlime","ThunderSlime","WindSlime","Bomber","Crown"};
        const std::vector<std::string> actions={"acquire","primary","primary_hold","secondary","special","release","idle","pink_combo"};
        const auto& entries=CopyAbilityPreviewRegistry::GetEntries();
        Require(entries.size()==6,"copy form count");
        for(size_t form=0;form<types.size();++form) {
            const auto& entry=entries[form];
            Require(entry.type==types[form],"copy form ID/order");
            Require(entry.profileType==(form==5?"FalseKingSlime":entry.type),"legacy profile alias changed");
            Require(entry.actions.size()==(form==0?8u:7u),"copy action count");
            for(size_t a=0;a<entry.actions.size();++a) {
                const auto& action=entry.actions[a];
                Require(action.id==actions[a] && action.startTime==0.5f,"legacy action ID/time");
                Require(action.startsUncopied==(a==0),"copy start policy");
                Require(static_cast<bool>(action.onTrigger)==(a==0 || a==5),"acquire/release callback");
                bool triggered=false;int events=0;
                for(int f=0;f<=180;++f) {
                    const float time=static_cast<float>(f)/60.0f,deltaTime=1.0f/60.0f;
                    const bool trigger=!triggered && time>=0.5f;
                    CopyAbilityPreviewFrame frame{time,deltaTime,1.5f,action.startTime,trigger};
                    const auto actual=action.buildInput?action.buildInput(frame):PlayerCopyAbilityInput{};
                    const auto expected=LegacyInput(static_cast<int>(a),time,deltaTime,trigger);
                    Require(actual.primaryTriggered==expected.primaryTriggered && actual.primaryHeld==expected.primaryHeld &&
                        actual.secondaryTriggered==expected.secondaryTriggered && actual.secondaryHeld==expected.secondaryHeld &&
                        actual.specialTriggered==expected.specialTriggered,"registered input differs from legacy timing");
                    events+=actual.primaryTriggered?1:0;
                    if(trigger)triggered=true;
                }
                if(a==7)Require(events==3,"pink combo count");
                cases.push_back({entry.type,action.id});
            }
            for(const auto& bookmark:entry.bookmarks)Require(entry.FindAction(bookmark.actionId)!=nullptr,"bookmark action");
        }
        auto extra=entries.front();extra.type="__test";
        auto broken=extra;broken.type="__broken";broken.bookmarks.front().actionId="missing";
        Require(!CopyAbilityPreviewRegistry::Register({extra,broken}) && CopyAbilityPreviewRegistry::GetEntries().size()==6,
            "copy registry partial update");
        Require(!CopyAbilityPreviewRegistry::Register({entries.front()}),"copy duplicate accepted");
        log<<"OK 6 forms / 43 actions: legacy fixed-step inputs, hold/release, combo, profile aliases, atomic registration"<<std::endl;
    }
    static void CheckSpawn(ActorDebugPanel& preview,std::ofstream& log) {
        preview.StopPreview();preview.SetPlayerCopyMode(false);
        preview.enemyType_="PrismSlime";preview.LoadSelectedProfile();preview.prismChantOnly_=false;
        for(int a=0;a<static_cast<int>(preview.editableProfile_.attacks.size());++a)
            if(preview.editableProfile_.attacks[a].id=="slime_summon")preview.profileAttackIndex_=a;
        preview.ApplyRecommendedSettings();preview.loopDuration_=8.0f;preview.enabled_=true;preview.CreatePreviewObjects();
        preview.playing_=false;
        for(int f=0;f<480 && preview.spawnedPreviewObjects_.empty();++f)preview.StepSimulation(1.0f/60.0f);
        Require(!preview.spawnedPreviewObjects_.empty(),"real prism summon missing");
        for(auto* object:preview.spawnedPreviewObjects_)
            Require(object && object->IsEditorInternal() && object->GetClassName()=="EditorOnly" &&
                object->GetCollisionAttribute()==0 && object->GetCollisionMask()==0,"spawn isolation");
        log<<"OK actual prism attack spawned isolated actors count="<<preview.spawnedPreviewObjects_.size()<<std::endl;

        // 召喚されたボマーにも同じ追加先が再帰的に接続されることを、実投擲で確認します。
        auto nested=EnemyFactory::GetInstance()->CreateEnemy("Bomber",preview.previewScene_->GetObject3DCommon());
        auto* bomber=dynamic_cast<EnemyBomber*>(nested.get());Require(bomber!=nullptr,"nested bomber create");
        nested->SetTranslate(preview.previewEnemy_->GetTranslate());
        preview.AddPreviewSpawn(std::move(nested));
        const auto count=preview.spawnedPreviewObjects_.size();
        EnemyProjectileRuntimeProbe::Access::Release(bomber);
        Require(preview.spawnedPreviewObjects_.size()==count+1,"nested bomber callback missing");
        auto* bomb=preview.spawnedPreviewObjects_.back();
        Require(bomb && bomb->IsEditorInternal() && bomb->GetCollisionAttribute()==0 && bomb->GetCollisionMask()==0,
            "nested bomb isolation");
        log<<"OK nested bomber actual throw uses common isolation/ownership path"<<std::endl;
        auto stale=preview.MakePreviewSpawnCallback();
        preview.StopPreview();
        stale(std::make_unique<BaseEnemy>());
        Require(preview.spawnedPreviewObjects_.empty(),"stopped session accepted spawn");
        preview.enabled_=true;preview.CreatePreviewObjects();preview.playing_=false;
        const auto objects=preview.previewScene_->GetObjects().size();
        stale(std::make_unique<BaseEnemy>());
        Require(preview.previewScene_->GetObjects().size()==objects,"previous generation accepted spawn");
        auto current=preview.MakePreviewSpawnCallback();auto* scene=preview.previewScene_;
        preview.previewScene_=nullptr;current(std::make_unique<BaseEnemy>());preview.previewScene_=scene;
        Require(scene->GetObjects().size()==objects,"different scene accepted spawn");
        preview.StopPreview();
        std::function<void(std::unique_ptr<BaseEnemy>)> expired;
        {
            ActorDebugPanel temporary;
            temporary.spawnSession_=std::make_shared<ActorDebugPanel::SpawnSession>();
            expired=temporary.MakePreviewSpawnCallback();
        }
        expired(std::make_unique<BaseEnemy>());
        log<<"OK stop/restart/scene mismatch/window destruction reject expired spawn callbacks"<<std::endl;
    }
    static void AfterDraw(SceneManager* manager,DebugEditor* editor,const std::string& path) {
        static std::ofstream log(path,std::ios::trunc);static int phase=0,wait=0;
        static size_t index=0;static std::vector<Case> cases;static bool done=false;
        if(done || !manager || !editor || manager->IsTransitioning())return;
        auto finish=[&](const std::string& text){log<<text<<std::endl;done=true;WinApp::CloseNow();};
        try {
            if(phase==0){manager->SetIsPlaying(false);manager->ChangeScene("GAMEPLAY");phase=1;return;}
            if(manager->GetCurrentSceneName()!="GAMEPLAY" || ++wait<3)return;
            auto& preview=*GetGameActorDebugPanel();
            if(phase==1){
                CheckInputs(cases,log);CheckSpawn(preview,log);
                preview.SetPlayerCopyMode(true);preview.loop_=false;preview.showTarget_=false;
                EditorManager::GetInstance()->SetSelectedObject(&preview);phase=2;
            }
            if(phase==2){
                if(index==cases.size()){
                    preview.StopPreview();
                    finish("PASS: registered preview spawns and copy abilities, legacy input contract, real playback/rewind/cleanup");return;
                }
                preview.copyType_=cases[index].type;preview.LoadSelectedProfile();preview.copyActionId_=cases[index].action;
                preview.loopDuration_=6.0f;preview.enabled_=true;preview.CreatePreviewObjects();preview.playing_=false;
                Require(preview.previewPlayer_ && preview.GetCopyAction(),"copy preview creation");
                if(cases[index].action=="acquire")Require(!preview.previewPlayer_->IsEnemyMorphed(),"acquire started copied");
                else Require(preview.previewPlayer_->IsEnemyMorphed(),"copy form not applied");
                wait=0;phase=3;return;
            }
            if(phase==3){
                const auto& test=cases[index];
                const float time=test.action=="primary_hold"?2.2f:(test.action=="pink_combo"?2.3f:0.8f);
                preview.SeekPreview(time);
                Require(std::abs(preview.elapsedTime_-time)<0.001f && preview.copyActionTriggered_,"copy playback clock/trigger");
                if(test.action=="acquire")Require(preview.previewPlayer_->IsEnemyMorphed(),"acquire callback not executed");
                if(test.action=="release") {
                    // 王冠の任意解除は本番でも拒否されます。プレビューで強制解除して仕様を変えません。
                    if(test.type=="Crown") Require(preview.previewPlayer_->IsEnemyMorphed() &&
                        !preview.previewPlayer_->IsEnemyMorphReleasing(),"crown manual-release restriction changed");
                    else Require(preview.previewPlayer_->IsEnemyMorphReleasing() || !preview.previewPlayer_->IsEnemyMorphed(),
                        "release callback not executed");
                }
                const std::vector<Player::EnemyMorphType> morphs={Player::EnemyMorphType::Slime,Player::EnemyMorphType::FireSlime,
                    Player::EnemyMorphType::ThunderSlime,Player::EnemyMorphType::WindSlime,Player::EnemyMorphType::Bomber,Player::EnemyMorphType::Crown};
                if(test.action!="release"){
                    const auto& entries=CopyAbilityPreviewRegistry::GetEntries();
                    for(size_t f=0;f<entries.size();++f)if(entries[f].type==test.type)
                        Require(preview.previewPlayer_->GetEnemyMorphType()==morphs[f],"legacy morph identifier changed");
                }
                preview.SeekPreview(0.25f);
                Require(!preview.copyActionTriggered_ && std::abs(preview.elapsedTime_-0.25f)<0.001f,"copy rewind did not reset trigger");
                preview.StopPreview();Require(!preview.spawnSession_ && !preview.previewPlayer_,"copy cleanup references");
                log<<"OK actual copy playback/rewind "<<test.type<<" / "<<test.action<<std::endl;
                ++index;phase=2;wait=0;
            }
        }catch(const std::exception& error){finish(std::string("FAIL: ")+error.what());}
    }
};
#endif
