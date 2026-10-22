#pragma once
#ifdef USE_IMGUI
#include "SceneManager.h"
#include "Player.h"
#include "game/actor/player/state/PlayerState.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "CollisionConfig.h"
#include "InputManager.h"
#include "WinApp.h"
#include <fstream>
#include <stdexcept>

// 専用起動で入力を再生し、運搬中の拒否と解除後のフック復帰を確認する。
namespace CarryHookRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_CARRY_HOOK_TEST", nullptr, 0) > 0; }
inline void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class ExitCounter final : public IAnimationState {
public:
    explicit ExitCounter(int& count) : count_(count) {}
    void Enter(Player*) override {}
    void Update(Player*, float) override {}
    void Exit(Player*) override { ++count_; }
private:
    int& count_;
};
inline void Run(BaseScene* scene, std::ostream& log) {
    auto* input = engine::InputManager::GetInstance();
    auto* camera = CameraManager::GetInstance()->GetMainCamera();
    Require(camera != nullptr, "main camera missing");
    const auto savedInput = input->CaptureReplayState();
    auto* savedTarget = camera->GetFollowTarget();
    struct Restore {
        engine::InputManager* input; Camera* camera; engine::InputManager::ReplayState saved; engine::Object3d* target;
        ~Restore() { input->ApplyReplayState(saved); camera->SetFollowTarget(target); camera->SetAimCameraSuppressed(false); }
    } restore{input, camera, savedInput, savedTarget};
    // Restoreを最後に残さず、Actor破棄前にカメラ参照を戻す。
    auto enemy = EnemyFactory::GetInstance()->CreateEnemy("Slime", scene->GetObject3dCommon());
    auto target = std::make_unique<engine::Object3d>();
    target->Initialize(scene->GetObject3dCommon());
    target->SetTranslate({0, 1000, 15}); target->SetScale({1, 1, 1});
    engine::Object3d::ColliderConfig box; box.type = ColliderType::kAABB; box.size = {4, 4, 4};
    target->SetColliderConfig(box); target->SetCollisionAttribute(CollisionCategories::Get("enemy"));
    target->SetCollisionMask(~0u); target->UpdateWorldMatrix(true);
    CollisionManager::GetInstance()->AddObject(target.get());
    struct Unregister { engine::Object3d* object; ~Unregister() { CollisionManager::GetInstance()->RemoveObject(object); } } unregister{target.get()};
    auto actor = std::make_unique<Player>();
    actor->Initialize(scene->GetObject3dCommon(), input, scene->GetParticleSystem(), scene->GetSpriteCommon());
    actor->SetCollisionAttribute(0); actor->SetCollisionMask(0);
    actor->SetTranslate({0, 1000, 0}); actor->UpdateWorldMatrix(true);
    actor->SetGravity(60); actor->SetMaxFallSpeed(100);
    camera->EndOverride(0); camera->SetFollowTarget(actor.get()); camera->SetInputEnabled(true);
    camera->SetFollowMode(Camera::FollowMode::kAimable); camera->ConfigAimable(12, 2, {0, 0, 0});
    const auto* binding = KeyConfig::GetInstance()->GetBindData("SecondaryAction");
    Require(binding && binding->mouseButton >= 0 && binding->padCode, "secondary binding missing");
    auto replay = [&](bool pad, bool held, bool previous) {
        engine::InputManager::ReplayState state{}; state.gamepadMode = pad;
        if (pad) { state.gamepadButtons = held ? binding->padCode : 0; state.previousGamepadButtons = previous ? binding->padCode : 0; }
        else { state.mouseButtons[binding->mouseButton] = held ? 0x80 : 0; state.previousMouseButtons[binding->mouseButton] = previous ? 0x80 : 0; }
        input->ApplyReplayState(state);
    };
    constexpr float dt = 1.0f / 60;
    try {
        for (bool pad : {false, true}) {
            actor->ChangeState(std::make_unique<PlayerStateIdle>());
            actor->SetTranslate({0, 1000, 0}); actor->UpdateWorldMatrix(true);
            actor->SetLockOn(true); actor->SetLockOnAimPoint({0, 1000, 15}, true);
            actor->GetHookMarker()->SetIsVisible(true);
            enemy->SetCarried(true); actor->SetCarriedEnemy(enemy.get());
            Require(!actor->GetHookMarker()->GetIsVisible(), "capture retained stale marker");
            for (int tick = 0; tick < 3; ++tick) {
                replay(pad, tick < 2, tick > 0);
                actor->GetHookMarker()->SetIsVisible(true);
                actor->SetVelocity({0, 0, 0}); actor->SetGrounded(false);
                actor->Update(dt);
                Require(!actor->GetHookMarker()->GetIsVisible(), "held input exposed hook marker");
                Require(actor->GetCarriedEnemy() == enemy.get() && actor->IsControlActive(), "held input started hook or dropped enemy");
                Require(actor->GetVelocity().y < -0.5f, "held input reduced gravity as hook aim");
            }
            replay(pad, true, true); camera->Update(dt);
            const auto carryEye = camera->GetEye() - actor->GetWorldPosition();
            Require(std::abs(carryEye.z) > 5, "held input entered first-person camera");
            int exits = 0;
            actor->ChangeState(std::make_unique<ExitCounter>(exits));
            actor->ChangeState(std::make_unique<PlayerStatePullEnemy>(target.get(), target->GetWorldPosition()));
            actor->ChangeState(std::make_unique<PlayerStatePullObject>(target.get(), target->GetWorldPosition()));
            Require(exits == 0 && actor->GetCarriedEnemy() == enemy.get(), "direct hook transition exited carry state");
            actor->ReleaseCarriedEnemy();
            camera->Update(dt);
            Require(std::abs(camera->GetEye().z - actor->GetWorldPosition().z) < 1, "release did not restore aiming camera");
            actor->ChangeState(std::make_unique<PlayerStateIdle>());
            actor->SetVelocity({0, 0, 0}); actor->SetTranslate({0, 1000, 0}); actor->UpdateWorldMatrix(true);
            replay(pad, true, false); actor->Update(dt);
            Require(actor->GetHookMarker()->GetIsVisible(), "release did not restore target acquisition");
            replay(pad, false, true); actor->Update(dt);
            Require(!actor->IsControlActive(), "release did not restore hook launch");
            actor->ChangeState(std::make_unique<PlayerStateIdle>());
            Require(exits == 1, "state exit count mismatch");
            log << "OK " << (pad ? "gamepad" : "mouse") << ": carry press/hold/release, stale marker, gravity, camera, both state guards, released hook launch" << std::endl;
        }
        // SetCarriedEnemy(nullptr)がコピー側のカメラ抑制を解除しないことも確認する。
        Require(actor->ApplyStoredCopy("FireSlime"), "copy setup failed");
        actor->SetCarriedEnemy(nullptr); replay(false, true, true); camera->Update(dt);
        Require(std::abs(camera->GetEye().z - actor->GetWorldPosition().z) > 5, "copy camera suppression overwritten");
        log << "OK copy ability camera suppression retained" << std::endl;
        camera->SetFollowTarget(savedTarget);
    } catch (...) { camera->SetFollowTarget(savedTarget); actor->ReleaseCarriedEnemy(); throw; }
}
inline void AfterDraw(SceneManager* manager, const std::string& path) {
    static std::ofstream log(path, std::ios::trunc);
    static int phase = 0, frames = 0;
    static bool done = false;
    if (done || !manager || manager->IsTransitioning()) return;
    try {
        if (phase == 0) { manager->ChangeScene("GAMEPLAY"); phase = 1; return; }
        if (manager->GetCurrentSceneName() != "GAMEPLAY" || ++frames < 3) return;
        manager->SetIsPlaying(false);
        Run(manager->GetCurrentScene(), log);
        log << "PASS: carry hook blocked and released hook restored for mouse/gamepad" << std::endl;
    } catch (const std::exception& error) { log << "FAIL: " << error.what() << std::endl; }
    done = true; WinApp::CloseNow();
}
}
#endif
