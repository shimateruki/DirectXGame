#pragma once
#ifdef USE_IMGUI
#include "SceneManager.h"
#include "game/system/scene/SceneGameplayAccess.h"
#include "game/system/progress/StageManager.h"
#include "Player.h"
#include "WinApp.h"
#include "CollisionManager.h"
#include <fstream>
#include <cmath>

namespace RollerRuntimeProbe {
inline void AfterDraw(SceneManager* manager, const std::string& path) {
    // 専用起動だけで乗車し、入力なしで回転・離脱・固定軸・処理時間を記録する。
    static std::ofstream log(path, std::ios::trunc);
    static int phase=0, frames=0;
    static ULONGLONG began=GetTickCount64(), last=began;
    static engine::Vector3 origin{};
    static bool moved=false, detached=false;
    if (phase==3 || !manager || manager->IsTransitioning()) return;
    auto finish=[&](const char* text) { log<<text<<std::endl; phase=3; WinApp::CloseNow(); };
    if (GetTickCount64()-began>120000) { finish("FAIL timeout"); return; }
    if (phase==0) {
        StageManager::GetInstance()->SetCurrentStageById("stage2");
        manager->ChangeScene("GAMEPLAY"); phase=1; return;
    }
    auto* scene=manager->GetCurrentScene();
    auto* player=GetScenePlayer(scene);
    if (!player || manager->GetCurrentSceneName()!="GAMEPLAY") return;
    engine::Object3d* roller=nullptr;
    for (const auto& o:scene->GetObjects())
        if (o && o->GetName()=="Stage2_Route_11_RollingBasalt") roller=o.get();
    if (!roller) { finish("FAIL missing roller"); return; }
    if (phase==1) {
        // 入場演出は操作ロック解除後も続くため、通常の物理更新へ戻るまで待つ。
        if (++frames<600 || player->IsCinematicLocked()) return;
        origin=roller->GetWorldPosition();
        for (const auto& o:scene->GetObjects()) {
            if (o && o->GetName()=="Stage2_Route_11_RollingBasalt_Collision") {
                const auto c=o->GetWorldPosition(); const auto bounds=o->GetAABB();
                log<<"COLLIDER position="<<c.x<<','<<c.y<<','<<c.z<<" attr="<<o->GetCollisionAttribute()
                   <<" volume="<<o->GetCollider()->HasModelVolume()<<" top="<<bounds.max.y<<std::endl;
            }
        }
        auto position=origin;
        position.y=roller->GetAABB().max.y-(player->GetAABB().min.y-player->GetWorldPosition().y)+0.05f;
        player->DebugPrepareForTeleport();
        player->SetIsControlActive(true);
        player->SetGrounded(false);
        player->SetTranslate(position); player->SetVelocity({}); player->UpdateWorldMatrix(false);
        player->SetDamageInvincible(true);
        phase=2; frames=0; last=GetTickCount64();
        log<<"START origin="<<origin.x<<','<<origin.y<<','<<origin.z
           <<" spawn="<<position.x<<','<<position.y<<','<<position.z<<std::endl;
        return;
    }
    const auto center=roller->GetWorldPosition(), r=roller->GetRotation(), p=player->GetWorldPosition();
    if (frames<5) for (const auto& o:scene->GetObjects()) {
        if (o && o->GetName()=="Stage2_Route_11_RollingBasalt_Collision") {
            float distance=0; engine::Vector3 normal{};
            const bool ray=o->GetCollider()->RaycastModelVolume(origin+engine::Vector3{0,10,0},{0,-1,0},20,distance,normal);
            const auto contact=player->CheckCollision(o.get());
            const auto a=o->GetAABB(), b=player->GetAABB();
            PhysicsQueryFilter filter;
            filter.mask=CollisionCategories::Get("ground");
            filter.ignoredObject=player;
            const auto overlaps=CollisionManager::GetInstance()->OverlapAABB(b,filter);
            log<<"BOUNDS floor="<<a.min.x<<','<<a.max.x<<','<<a.min.y<<','<<a.max.y<<','<<a.min.z<<','<<a.max.z
               <<" player="<<b.min.x<<','<<b.max.x<<','<<b.min.y<<','<<b.max.y<<','<<b.min.z<<','<<b.max.z
               <<" overlaps="<<overlaps.size()<<" groundBit="<<CollisionCategories::Get("ground")<<std::endl;
            log<<"PLAYER registered="<<(std::find(CollisionManager::GetInstance()->GetObjects().begin(),
                CollisionManager::GetInstance()->GetObjects().end(),player)!=CollisionManager::GetInstance()->GetObjects().end())
               <<" types="<<static_cast<int>(player->GetColliderType())<<','<<static_cast<int>(o->GetColliderType())
               <<" playing="<<manager->IsPlaying()<<" singleton="<<SceneManager::GetInstance()->IsPlaying()<<std::endl;
            log<<"QUERY hull="<<o->GetCollider()->GetWorldHull().size()<<" ray="<<ray<<" distance="<<distance
               <<" contact="<<contact.isColliding<<" normal="<<normal.y
               <<" push="<<contact.normal.y<<" mask="<<player->GetCollisionMask()
               <<" otherMask="<<o->GetCollisionMask()<<" attr="<<player->GetCollisionAttribute()
               <<" static="<<o->IsStatic()<<" pStatic="<<player->IsStatic()
               <<" locked="<<player->IsCinematicLocked()
               <<" registered="<<(std::find(CollisionManager::GetInstance()->GetObjects().begin(),
                    CollisionManager::GetInstance()->GetObjects().end(),o.get())!=CollisionManager::GetInstance()->GetObjects().end())<<std::endl;
        }
    }
    if (std::abs(center.x-origin.x)+std::abs(center.y-origin.y)+std::abs(center.z-origin.z)>0.02f) {
        finish("FAIL axis translated"); return;
    }
    moved |= std::abs(r.x)>0.05f;
    detached |= moved && !player->IsGrounded() && p.y<origin.y+2;
    if (++frames<=15 || frames%30==0) {
        const auto now=GetTickCount64();
        log<<frames<<" rot="<<r.x<<','<<r.y<<','<<r.z<<" player="<<p.x<<','<<p.y<<','<<p.z
           <<" grounded="<<player->IsGrounded()<<" dead="<<player->IsDead()<<" avgMs="<<(now-last)/(frames<=15 ? 1.0:30.0)<<std::endl;
        last=now;
    }
    if (detached) { finish("PASS fixed axis, rotation and rider fall"); return; }
    if (!moved && p.y<-6) { finish("FAIL passed through cylinder"); return; }
    if (frames>=480) finish(moved ? "FAIL rider never fell" : "FAIL rotation never started");
}
}
#endif
