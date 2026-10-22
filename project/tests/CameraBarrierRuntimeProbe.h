#pragma once
#ifdef USE_IMGUI
#include "SceneManager.h"
#include "Player.h"
#include "Camera.h"
#include "CollisionManager.h"
#include "CollisionConfig.h"
#include "WinApp.h"
#include <fstream>
#include <stdexcept>

// 専用プロセスで実際の追従カメラとプレイヤー接触を検証する。
namespace CameraBarrierRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_CAMERA_BARRIER_TEST", nullptr, 0) > 0; }
inline void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline void Run(BaseScene* scene, bool isSelect, std::ostream& log) {
    auto* common = scene->GetObject3dCommon();
    auto target = std::make_unique<engine::Object3d>(); target->Initialize(common);
    target->SetClassName("Player"); target->SetTranslate({0, 1000, 0}); target->UpdateWorldMatrix(true);
    auto wall = std::make_unique<engine::Object3d>(); wall->Initialize(common);
    wall->SetTranslate({0, 1002, -4});
    engine::Object3d::ColliderConfig box; box.type = ColliderType::kAABB; box.size = {4, 4, 0.5f};
    wall->SetColliderConfig(box); wall->SetCollisionAttribute(CollisionCategories::Get("ground"));
    wall->SetCollisionMask(~0u); wall->UpdateWorldMatrix(true);
    auto rear = std::make_unique<engine::Object3d>(); rear->Initialize(common);
    rear->SetTranslate({0, 1002, -8}); rear->SetColliderConfig(box);
    rear->SetCollisionAttribute(CollisionCategories::Get("ground")); rear->SetCollisionMask(~0u); rear->UpdateWorldMatrix(true);
    auto* collisions = CollisionManager::GetInstance(); collisions->AddObject(wall.get());
    struct Unregister {
        CollisionManager* collisions; engine::Object3d* wall; engine::Object3d* rear;
        ~Unregister() { collisions->RemoveObject(wall); collisions->RemoveObject(rear); }
    } unregister{collisions, wall.get(), rear.get()};
    auto eyeZ = [&] {
        Camera camera; camera.Initialize(); camera.SetFollowTarget(target.get());
        camera.SetFollowMode(Camera::FollowMode::kAimable); camera.ConfigAimable(12, 2, {0, 0, 0});
        camera.SetAimCameraSuppressed(true); camera.Update(1.0f/60);
        return camera.GetEye().z;
    };
    auto blocked = [&](float z) { Require(std::abs(z - (wall->GetAABB().max.z + 0.2f)) < 0.01f, "ordinary wall camera correction changed"); };
    auto unchangedRay = [&] {
        const auto hit = collisions->Raycast({0, 1002, 0}, {0, 0, -1}, 12, CollisionCategories::Get("ground"));
        Require(hit.isHit && hit.hitObject == wall.get(), "general ray lost barrier collision");
    };
    auto actor = std::make_unique<Player>();
    actor->Initialize(common, engine::InputManager::GetInstance(), scene->GetParticleSystem(), scene->GetSpriteCommon());
    actor->SetEditorInternal(true); actor->SetCollisionAttribute(CollisionCategories::Get("player")); actor->SetCollisionMask(~0u);
    engine::Object3d::ColliderConfig actorBox; actorBox.type = ColliderType::kAABB; actorBox.size = {0.5f, 0.5f, 0.5f};
    actor->SetColliderConfig(actorBox); actor->SetScale({1,1,1});
    auto playerBlocked = [&] {
        actor->SetTranslate({0, 1002, -3.5f}); actor->UpdateWorldMatrix(true); actor->SetVelocity({0,0,-8});
        Require(actor->OnCollision(wall.get()), "player no longer contacts barrier");
        Require(actor->GetTranslate().z > -3.4f && actor->GetVelocity().z > -0.01f, "player can pass through barrier");
    };
    // 可視性や透明度で一括除外せず、指定した種類だけを外す。
    wall->SetClassName("Model"); wall->SetGimmickType(""); blocked(eyeZ());
    wall->SetIsVisible(false); wall->SetColor({1,1,1,0}); blocked(eyeZ());
    wall->SetCollisionMask(0);
    Require(std::abs(eyeZ()+12) < 0.01f, "disabled collision mask unexpectedly blocked camera");
    wall->SetCollisionMask(~0u);
    wall->SetGimmickType("PrismBarrier");
    Require(std::abs(eyeZ()+12) < 0.01f, "prism barrier still pushes camera");
    unchangedRay(); playerBlocked();
    collisions->AddObject(rear.get());
    const float behindBarrier = eyeZ();
    Require(std::abs(behindBarrier - (rear->GetAABB().max.z + 0.2f)) < 0.01f, "excluded barrier hid ordinary wall behind it");
    collisions->RemoveObject(rear.get());
    wall->SetGimmickType(""); wall->SetClassName("InvisibleBox");
    if (isSelect) Require(std::abs(eyeZ()+12) < 0.01f, "select invisible collider still pushes camera");
    else blocked(eyeZ());
    unchangedRay(); playerBlocked();
    wall->SetClassName("Model"); blocked(eyeZ());
    log << "OK " << (isSelect ? "SELECT" : "GAMEPLAY")
        << ": prism camera bypass, scene-specific InvisibleBox, hidden ordinary wall, wall behind barrier, normal ray and player blocking" << std::endl;
}
inline void AfterDraw(SceneManager* manager, const std::string& path) {
    static std::ofstream log(path, std::ios::trunc);
    static int phase = 0, frames = 0;
    static bool done = false;
    static ULONGLONG started = GetTickCount64();
    if (done || !manager) return;
    auto finish = [&](const std::string& message) { log << message << std::endl; done = true; WinApp::CloseNow(); };
    try {
        Require(GetTickCount64()-started < 240000, "camera barrier probe timeout");
        if (manager->IsTransitioning()) return;
        if (phase == 0) { manager->ChangeScene("SELECT"); phase = 1; return; }
        const bool isSelect = phase == 1;
        if (manager->GetCurrentSceneName() != (isSelect ? "SELECT" : "GAMEPLAY") || ++frames < 3) return;
        manager->SetIsPlaying(false);
        Run(manager->GetCurrentScene(), isSelect, log);
        if (isSelect) { manager->ChangeScene("GAMEPLAY"); phase = 2; frames = 0; return; }
        finish("PASS: only camera bypasses prism barriers and SELECT invisible colliders; player blocking retained");
    } catch (const std::exception& error) { finish(std::string("FAIL: ") + error.what()); }
}
}
#endif
