#pragma once
#ifdef USE_IMGUI
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/actor/enemy/ranged/EnemyBomber.h"
#include "game/actor/enemy/ranged/EnemyBomb.h"
#include "game/actor/gimmick/block/GimmickBreakableBlock.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "game/actor/combat/AttackSequence.h"
#include "game/system/combat/BulletManager.h"
#include "tests/GateEntryRuntimeProbe.h"
#include "tests/CannonDamageRuntimeProbe.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

// 明示起動した専用プロセスで、地形接触と個別所有モデルのGPU描画を確認する。
namespace EnemyProjectileRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_ENEMY_PROJECTILE_TEST",nullptr,0)>0; }
inline void Require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
struct Access {
    static void Hold(EnemyBomber* actor) {
        actor->throwState_=EnemyBomber::ThrowState::Windup;
        actor->windupTimer_=0.55f;
        actor->throwBombReleased_=false;
        actor->UpdateHeldBombVisual(0);
    }
    static engine::Object3d* Held(EnemyBomber* actor) { return actor->heldBombVisual_.get(); }
    static engine::Vector3 Expected(EnemyBomber* actor) { return actor->ComputeHeldBombPosition(actor->GetThrowProgress()); }
    static void Release(EnemyBomber* actor) {
        actor->ThrowBomb();actor->throwBombReleased_=true;
        actor->throwState_=EnemyBomber::ThrowState::Landing;actor->throwLandingTimer_=10;
    }
};
inline bool Same(const engine::Matrix4x4& a,const engine::Matrix4x4& b,float tolerance=0.01f) {
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)if(std::abs(a.m[i][j]-b.m[i][j])>tolerance)return false;
    return true;
}
inline void FireCases(BaseScene* scene,std::ostream& log) {
    auto* bullets=BulletManager::GetInstance();auto* collisions=CollisionManager::GetInstance();bullets->Clear();
    auto floor=std::make_unique<engine::Object3d>();floor->Initialize(scene->GetObject3dCommon());
    floor->SetTranslate({0,1000,10});floor->SetScale({1,1,1});
    engine::Object3d::ColliderConfig box;box.type=ColliderType::kAABB;box.size={8,0.1f,16};box.center={0,0,0};floor->SetColliderConfig(box);
    floor->SetCollisionAttribute(CollisionCategories::Get("ground"));floor->SetCollisionMask(~0u);floor->UpdateWorldMatrix(true);collisions->AddObject(floor.get());
    EnemyAttackProfile profile;std::string error;Require(profile.LoadForEnemy("FireSlime",&error),"fire profile load failed");
    const auto* fire=profile.FindAttack("fireball");Require(fire && fire->sequence.enabled,"missing fire sequence");
    for(float step:{1.0f/60.0f,0.25f}) {
        AttackSequence sequence;AttackSequenceContext context;context.position={0,1002,0};context.forward={0,0,1};
        context.hasTarget=true;context.targetPosition={0,999,12};context.targetDistance=12;
        Require(sequence.Start(*fire,context),"fire sequence failed");sequence.Update(0.44f,context);
        Require(bullets->GetBullets().size()==1,"sequence did not emit one fireball");
        Require(bullets->GetBullets().front()->GetVelocity().y<0,"fireball did not aim at ground");
        float elapsed=0;
        while(!bullets->GetBullets().empty() && elapsed<1.5f) { bullets->Update(step);elapsed+=step; }
        Require(bullets->GetBullets().empty(),"fireball remained alive at floor");
        log<<"PASS authored enemy fireball expires on floor step="<<step<<" elapsed="<<elapsed<<std::endl;
    }
    // 無害な比較表示と大砲の地形非衝突は、今回の火球指定に巻き込まない。
    BulletVisualConfig preview;preview.previewOnly=true;preview.destroyOnSolidCastHit=true;
    bullets->Fire({0,1002,0},{0,-10,0},CollisionCategories::Get("enemy_attack"),CollisionCategories::Get("ground"),"Primitives/sphere",0.3f,3,preview);
    bullets->Update(0.4f);Require(bullets->GetBullets().size()==1,"preview collided with ground");bullets->Clear();
    auto disabled=*fire;
    for(auto& event:disabled.sequence.events)if(event.operation=="projectile")event.projectile.collisionEnabled=false;
    AttackSequence disabledSequence;AttackSequenceContext context;context.position={0,1002,0};context.forward={0,0,1};context.hasTarget=true;context.targetPosition={0,999,12};context.targetDistance=12;
    Require(disabledSequence.Start(disabled,context),"disabled fire sequence failed");disabledSequence.Update(0.44f,context);bullets->Update(1.0f);
    Require(bullets->GetBullets().size()==1,"disabled collision fireball vanished on ground");bullets->Clear();
    BulletVisualConfig cannon;
    bullets->Fire({0,1002,0},{0,-10,0},CollisionCategories::Get("enemy_attack"),CollisionCategories::Get("player"),"Primitives/sphere",0.3f,3,cannon);
    bullets->Update(0.4f);Require(bullets->GetBullets().size()==1 && bullets->GetBullets().front()->GetTranslate().y<1000,"cannon ground pass changed");bullets->Clear();
    collisions->RemoveObject(floor.get());
    log<<"PASS preview/disabled collision and cannon ground-pass unchanged"<<std::endl;
}
inline void BombPhysicsCases(BaseScene* scene,std::ostream& log) {
    auto* collisions=CollisionManager::GetInstance();
    auto floor=std::make_unique<engine::Object3d>();floor->Initialize(scene->GetObject3dCommon());floor->SetTranslate({0,1000,0});
    engine::Object3d::ColliderConfig box;box.type=ColliderType::kAABB;box.size={100,0.1f,100};box.center={0,0,0};floor->SetColliderConfig(box);floor->SetCollisionAttribute(CollisionCategories::Get("ground"));floor->SetCollisionMask(~0u);floor->UpdateWorldMatrix(true);collisions->AddObject(floor.get());
    float shortest=10000,longest=0;
    for(bool playerOwned:{false,true})for(bool placed:{false,true})for(float dt:{1.0f/30.0f,1.0f/60.0f,1.0f/144.0f,0.1f}) {
        auto actor=EnemyFactory::GetInstance()->CreateEnemy("Bomb",scene->GetObject3dCommon());auto* bomb=dynamic_cast<EnemyBomb*>(actor.get());Require(bomb,"missing bomb factory");
        bomb->SetTarget(nullptr);bomb->SetPlayerOwned(playerOwned);bomb->Ignite(8);bomb->SetTranslate({0,placed?1000.52f:1002.0f,0});
        bomb->BeginThrown(placed?engine::Vector3{0,1.8f,0}:engine::Vector3{0,8,24});bomb->UpdateWorldMatrix(true);
        const float radius=bomb->GetCollider()->GetRadius();bool landed=false;float landTime=0,landZ=0,landY=0;float elapsed=0;
        for(int i=0;elapsed<3.0f;++i) {
            bomb->Update(dt);bomb->OnCollision(floor.get());elapsed+=dt;
            Require(std::abs(bomb->GetCollider()->GetRadius()-radius)<0.0001f,"bomb pulse changed collider radius");
            Require(!bomb->IsThrownPhysics(),"bomb entered generic slime bounce physics");
            Require(bomb->GetTranslate().y>=1000.1f+radius-0.035f,"bomb penetrated floor");
            if(!landed && bomb->IsGrounded()) {landed=true;landTime=elapsed;landZ=bomb->GetTranslate().z;landY=bomb->GetTranslate().y;}
            if(landed) {
                Require(bomb->GetTranslate().y<=landY+0.035f && bomb->GetVelocity().y<=0.05f,"bomb bounced after settling");
                if(elapsed-landTime>0.85f)Require(std::abs(bomb->GetVelocity().z)<0.01f,"bomb kept sliding after landing");
            }
        }
        Require(landed,"bomb never landed");Require(std::abs(bomb->GetTranslate().z-landZ)<3.2f,"bomb slid too far after landing");
        if(placed)Require(std::abs(bomb->GetTranslate().z)<0.01f,"placed bomb drifted horizontally");
        else {shortest=std::min(shortest,bomb->GetTranslate().z);longest=std::max(longest,bomb->GetTranslate().z);}
        log<<"PASS bomb physics owner="<<playerOwned<<" placed="<<placed<<" dt="<<dt<<" land="<<landTime<<" slide="<<bomb->GetTranslate().z-landZ<<" finalZ="<<bomb->GetTranslate().z<<std::endl;
    }
    Require(longest-shortest<0.35f,"bomb throw range depended on frame rate");
    auto wall=std::make_unique<engine::Object3d>();wall->Initialize(scene->GetObject3dCommon());box.size={8,3,0.10f};wall->SetColliderConfig(box);wall->SetTranslate({0,1003,6});wall->SetCollisionAttribute(CollisionCategories::Get("ground"));wall->SetCollisionMask(~0u);wall->UpdateWorldMatrix(true);collisions->AddObject(wall.get());
    for(bool playerOwned:{false,true}) {
        auto actor=EnemyFactory::GetInstance()->CreateEnemy("Bomb",scene->GetObject3dCommon());auto* bomb=dynamic_cast<EnemyBomb*>(actor.get());bomb->SetTarget(nullptr);bomb->SetPlayerOwned(playerOwned);bomb->Ignite(8);bomb->SetTranslate({0,1002,0});bomb->BeginThrown({0,8,24});bomb->UpdateWorldMatrix(true);
        bomb->Update(0.1f);Require(std::abs(bomb->GetVelocity().y-2.0f)<0.01f && std::abs(bomb->GetTranslate().z-2.4f)<0.01f,"gravity doubled or air velocity changed");
        for(int i=0;i<120;++i){bomb->Update(1.0f/60.0f);bomb->OnCollision(floor.get());bomb->OnCollision(wall.get());}
        Require(bomb->GetTranslate().z<5.7f && bomb->GetTranslate().z>0 && bomb->IsGrounded(),"wall contact tunneled or launched bomb");
    }
    collisions->RemoveObject(wall.get());collisions->RemoveObject(floor.get());
    log<<"PASS bomb wall contact, single gravity, stable placed/thrown physics and frame-rate range"<<std::endl;
}
// 爆弾モデルの縮小率・起爆経路が異なっても、同じワールド範囲へ一度だけ命中する。
inline void BombExplosionCases(BaseScene* scene, std::ostream& log) {
    auto* collisions = CollisionManager::GetInstance();
    const engine::Vector3 origin{2000.0f, 1000.0f, 2000.0f};
    for (float scale : {0.16f, 0.30f, 2.0f}) for (bool direct : {false, true}) {
        std::vector<std::unique_ptr<engine::Object3d>> objects;
        struct Registration {
            CollisionManager* manager;
            std::vector<engine::Object3d*> values;
            ~Registration() { for (auto* object : values) manager->RemoveObject(object); }
        } registration{collisions, {}};
        auto addEnemy = [&](const engine::Vector3& offset) {
            auto actor = EnemyFactory::GetInstance()->CreateEnemy("Slime", scene->GetObject3dCommon());
            Require(actor && actor->param_, "bomb regression target missing");
            auto* enemy = actor.get();
            enemy->SetTarget(nullptr);
            enemy->param_->hp = enemy->param_->maxHp = 100.0f;
            enemy->SetScale({1, 1, 1});
            enemy->SetTranslate(origin + offset);
            engine::Object3d::ColliderConfig shape;
            shape.type = ColliderType::kSphere; shape.size = {0.25f, 0.25f, 0.25f};
            enemy->SetColliderConfig(shape);
            enemy->SetCollisionAttribute(CollisionCategories::Get("enemy"));
            enemy->SetCollisionMask(CollisionCategories::Get("player_attack"));
            enemy->UpdateWorldMatrix(true);
            collisions->AddObject(enemy); registration.values.push_back(enemy);
            objects.push_back(std::move(actor));
            return enemy;
        };
        BaseEnemy* nearby = addEnemy({4.4f, 0, 0});
        BaseEnemy* outside = addEnemy({6.2f, 0, 0});
        BaseEnemy* contact = direct ? addEnemy({0, 0, 0.2f}) : nullptr;
        auto wall = std::make_unique<GimmickBreakableBlock>();
        wall->Initialize(scene->GetObject3dCommon(), "Stages/bomb_break_block");
        wall->SetScale({1, 1, 1}); wall->SetTranslate(origin + engine::Vector3{-4.4f, 0, 0});
        engine::Object3d::ColliderConfig box;
        box.type = ColliderType::kAABB; box.size = {0.25f, 0.25f, 0.25f};
        wall->SetColliderConfig(box); wall->UpdateWorldMatrix(true);
        auto* breakable = wall.get();
        collisions->AddObject(breakable); registration.values.push_back(breakable);
        objects.push_back(std::move(wall));
        auto actor = EnemyFactory::GetInstance()->CreateEnemy("Bomb", scene->GetObject3dCommon());
        auto* bomb = dynamic_cast<EnemyBomb*>(actor.get());
        Require(bomb && bomb->param_, "bomb regression actor missing");
        bomb->SetTarget(nullptr); bomb->SetPlayerOwned(true);
        bomb->param_->attackPower = 1.0f;
        bomb->ApplyManagedScale({scale, scale, scale});
        bomb->SetTranslate(origin); bomb->SetRotation({0.4f, 0.6f, 0.2f});
        bomb->Ignite(direct ? 8.0f : 0.01f); bomb->UpdateWorldMatrix(true);
        if (direct) bomb->OnCollision(contact); else bomb->Update(0.02f);
        Require(bomb->IsExploded(), "player bomb failed to explode");
        Require(std::abs(bomb->GetCollider()->GetRadius() - 5.1f) < 0.01f, "bomb model scale shrank world blast");
        Require(std::abs(nearby->param_->hp - 90.0f) < 0.01f, "near-miss bomb did not damage nearby enemy once");
        Require(std::abs(outside->param_->hp - 100.0f) < 0.01f, "bomb damaged enemy outside blast");
        Require(breakable->isDead && breakable->GetCollisionAttribute() == 0, "fuse/impact blast did not break nearby wall");
        if (contact) Require(std::abs(contact->param_->hp - 90.0f) < 0.01f, "direct blast damage duplicated");
        // 次の衝突・更新に再通知しても同じ爆風を残さない。
        if (contact) bomb->OnCollision(contact);
        nearby->OnCollision(bomb); bomb->Update(0.02f);
        Require(std::abs(nearby->param_->hp - 90.0f) < 0.01f && bomb->GetCollisionAttribute() == 0,
                "expired bomb applied a second blast");
        // 爆発した後に近づいた敵へ、表示用の膨張エフェクトが追加ダメージを出さない。
        BaseEnemy* late = addEnemy({0, 0, 0});
        auto* meshes = MeshEffectManager::GetInstance();
        meshes->UpdateEditorPreviewStep(0.25f);
        for (const auto& visual : meshes->GetActiveEffects()) late->OnCollision(visual.get());
        Require(std::abs(late->param_->hp - 100.0f) < 0.01f, "visual blast applied delayed extra damage");
        log << "PASS player bomb blast scale=" << scale << " direct=" << direct
            << " radius=" << bomb->GetCollider()->GetRadius() << " nearHP=" << nearby->param_->hp
            << " outsideHP=" << outside->param_->hp << " wallBroken=" << breakable->isDead << std::endl;
    }
}
inline void AfterDraw(SceneManager* manager,const std::string& path) {
    static std::ofstream log(path,std::ios::trunc);static ULONGLONG start=GetTickCount64();
    static int phase=0,frames=0;static bool done=false;static EnemyBomber* bomber=nullptr;static EnemyBomb* thrown=nullptr;
    static std::unique_ptr<Camera> camera;static Camera* previous=nullptr;static engine::Vector3 base{},target{},release{};
    static engine::Matrix4x4 initialWvp{},initialViewProjection{};
    if(done || !manager)return;
    auto finish=[&](const std::string& message){log<<message<<std::endl;done=true;CameraManager::GetInstance()->SetActiveCamera(previous);WinApp::CloseNow();};
    try {
        Require(GetTickCount64()-start<360000,"enemy projectile probe timeout");
        if(manager->IsTransitioning())return;
        if(phase==0) { StageManager::GetInstance()->SetCurrentStageById("stage1");manager->ChangeScene("GAMEPLAY");phase=1;return; }
        auto* scene=dynamic_cast<GamePlayScene*>(manager->GetCurrentScene());if(!scene)return;
        auto* player=GetScenePlayer(scene);if(!player)return;
        if(phase==1) {
            if(++frames<12 || !player->IsControlActive() || player->IsCinematicLocked())return;
            BombExplosionCases(scene,log);
            if (GetEnvironmentVariableA("CG2_BOMB_EXPLOSION_TEST", nullptr, 0) > 0) {
                finish("PASS: player bomb world radius, fuse/contact splash, breakable walls and single damage");
                return;
            }
            FireCases(scene,log);
            BombPhysicsCases(scene,log);
            Require(CannonDamageRuntimeProbe::RunCase(manager,0,0.6f,0,false,false,log),"cannon damage regression");
            Require(CannonDamageRuntimeProbe::RunPlayerProjectileCase(manager,log),"player projectile damage regression");
            manager->SetIsPlaying(true);
            base=player->GetWorldPosition()+engine::Vector3{0,2,8};target=base+engine::Vector3{0,0,12};
            auto created=EnemyFactory::GetInstance()->CreateEnemy("Bomber",scene->GetObject3dCommon());
            bomber=dynamic_cast<EnemyBomber*>(created.get());Require(bomber,"bomber factory failed");
            bomber->SetTranslate(base);bomber->ApplyManagedScale({2,2,2});bomber->SetTarget(player);
            bomber->SetGravity(0);bomber->SetMaxFallSpeed(0);bomber->param_->gravity=bomber->param_->maxFallSpeed=bomber->param_->speed=0;
            bomber->param_->hp=bomber->param_->maxHp=200;bomber->SetVelocity({0,0,0});bomber->UpdateWorldMatrix(true);
            bomber->SetSpawnCallback([scene](std::unique_ptr<BaseEnemy> object){thrown=dynamic_cast<EnemyBomb*>(object.get());scene->AddObject(std::move(object));});
            scene->AddObject(std::move(created));
            previous=CameraManager::GetInstance()->GetActiveCameraOverride();camera=std::make_unique<Camera>();camera->Initialize();camera->SetFovY(0.65f);
            Camera::CameraOverrideParams view;view.duration=0;view.fixedEyePos=base+engine::Vector3{0,4,-13};view.fixedTargetPos=base+engine::Vector3{0,1,0};view.trackTargetX=view.trackTargetY=view.trackTargetZ=false;camera->StartOverride(view);camera->Update();CameraManager::GetInstance()->GetMainCamera()->StartOverride(view);CameraManager::GetInstance()->SetActiveCamera(camera.get());
            Access::Hold(bomber);phase=2;frames=0;return;
        }
        if(phase==2) {
            auto* held=Access::Held(bomber);Require(held && held->GetIsVisible(),"held bomb disappeared");
            const auto expected=Access::Expected(bomber);
            Require(engine::Math::Length(held->GetWorldPosition()-expected)<0.002f,"held bomb did not follow final owner pose");
            auto* resource=held->GetMeshRenderer()->GetWvpResource();void* address=nullptr;D3D12_RANGE range{0,sizeof(MeshRenderer::TransformationMatrix)};
            Require(SUCCEEDED(resource->Map(0,&range,&address)),"held bomb render buffer map failed");MeshRenderer::TransformationMatrix gpu;std::memcpy(&gpu,address,sizeof(gpu));D3D12_RANGE noWrite{0,0};resource->Unmap(0,&noWrite);
            Require(Same(gpu.world,held->GetWorldMatrix()),"held bomb world was not uploaded");
            const auto wvp=engine::Math::Multiply(held->GetWorldMatrix(),CameraManager::GetInstance()->GetActiveCamera()->GetViewProjectionMatrix());
            Require(Same(gpu.WVP,wvp,0.05f),"held bomb camera matrix was stale");
            if(++frames==8) {
                initialWvp=gpu.WVP;initialViewProjection=CameraManager::GetInstance()->GetActiveCamera()->GetViewProjectionMatrix();GateEntryRuntimeProbe::Capture(path+".held-camera-a.png");
                Camera::CameraOverrideParams view;view.duration=0;view.fixedEyePos=base+engine::Vector3{6,5,-11};view.fixedTargetPos=base+engine::Vector3{0,1,0};view.trackTargetX=view.trackTargetY=view.trackTargetZ=false;camera->StartOverride(view);camera->Update();CameraManager::GetInstance()->GetMainCamera()->StartOverride(view);
                bomber->SetTranslate(base+engine::Vector3{2,0,0});
            }
            if(frames==16) {
                Require(!Same(initialViewProjection,CameraManager::GetInstance()->GetActiveCamera()->GetViewProjectionMatrix(),0.01f),"test camera did not move");
                Require(!Same(initialWvp,gpu.WVP,0.05f),"held bomb stayed fixed on screen after camera moved");
                GateEntryRuntimeProbe::Capture(path+".held-camera-b.png");
                log<<"PASS held bomb GPU world/WVP follow final owner and moving camera"<<std::endl;
                Access::Release(bomber);Require(!Access::Held(bomber)->GetIsVisible(),"held model remained after release");Require(thrown && !thrown->IsExploded(),"real bomb did not spawn");release=thrown->GetTranslate();phase=3;frames=0;return;
            }
            Access::Hold(bomber);return;
        }
        if(phase==3) {
            Require(!Access::Held(bomber)->GetIsVisible(),"held model reappeared after throw");
            if(++frames<15)return;
            Require(thrown && !thrown->IsExploded() && engine::Math::Length(thrown->GetTranslate()-release)>0.1f,"thrown bomb did not fly in world");
            GateEntryRuntimeProbe::Capture(path+".thrown.png");
            log<<"PASS release hides held model and launches real world-space bomb"<<std::endl;
            finish("PASS: enemy fireball ground expiry, bomb physics, cannon regression and bomber moving-camera presentation");
        }
    }catch(const std::exception& error){GateEntryRuntimeProbe::Capture(path+".failed.png");finish(std::string("FAIL: ")+error.what());}
}
} // namespace EnemyProjectileRuntimeProbe
#endif
