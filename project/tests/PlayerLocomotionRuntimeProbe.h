#pragma once
#ifdef USE_IMGUI
#include "game/actor/player/Player.h"
#include "game/actor/player/movement/PlayerMover.h"
#include "game/component/IMoveStrategy.h"
#include "game/system/rules/GameplayStatusManager.h"
#include "SceneManager.h"
#include "WinApp.h"
#include <fstream>
#include <stdexcept>

// 隔離したPlayerと実際のMoverで、ジャンプ・外力・Editor設定の保存まで確認する。
namespace PlayerLocomotionRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_PLAYER_LOCOMOTION_TEST", nullptr, 0) > 0; }
struct Access {
    static void QueueJump(PlayerMover& mover) { mover.jumpRequest_.Advance(1.0f / 60, true, true, true, false); }
};
class MoveInput final : public IMoveStrategy {
  public:
    engine::Vector3 value{};
    engine::Vector3 CalculateVelocity(Player*) override { return value; }
};
inline void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
inline void Run(BaseScene* scene, std::ostream& log) {
    auto* settings = GameplayStatusManager::GetInstance();
    const auto original = settings->GetPlayerLocomotion();
    settings->GetMutablePlayerLocomotion() = {};
    auto actor = std::make_unique<Player>();
    actor->Initialize(scene->GetObject3DCommon(), engine::InputManager::GetInstance(), scene->GetParticleSystem(), scene->GetSpriteCommon());
    actor->SetEditorInternal(true); actor->SetCollisionAttribute(0); actor->SetCollisionMask(0);
    actor->SetCopyPreviewInput({}); actor->SetCopyPreviewGroundHeight(1000);
    actor->SetParameter(&engine::EntityParameter::speed, 10); actor->SetParameter(&engine::EntityParameter::gravity, 50); actor->SetParameter(&engine::EntityParameter::maxFallSpeed, 60);
    auto mover = std::make_unique<PlayerMover>();
    mover->Initialize(actor.get(), engine::InputManager::GetInstance(), scene->GetParticleSystem());
    auto strategy = std::make_unique<MoveInput>(); auto* input = strategy.get(); mover->SetStrategy(std::move(strategy));
    constexpr float kFixedDeltaTime = 1.0f / 60;
    auto reset = [&] {
        mover->ResetTransientState(); actor->SetTranslate({0, 1000, 0}); actor->SetVelocity({}); actor->SetGrounded(true);
        actor->UpdateWorldMatrix(true); input->value = {};
    };
    auto step = [&] { mover->Update(kFixedDeltaTime); actor->SetCopyPreviewInput({}); actor->Update(kFixedDeltaTime); };
    reset(); input->value = {10, 0, 0}; step();
    Require(actor->GetVelocity().x > 0 && actor->GetVelocity().x < 1, "runtime startup snapped to full speed");
    for (int i = 0; i < 23; ++i) step();
    Require(std::abs(actor->GetVelocity().x - 10) < 0.001f, "runtime maximum was not reached");
    input->value = {}; for (int i = 0; i < 6; ++i) step();
    Require(std::abs(actor->GetVelocity().x) < 0.001f, "runtime release failed to stop");
    log << "OK actual Player/Mover acceleration and quick braking" << std::endl;
    reset(); Access::QueueJump(*mover); step();
    Require(actor->GetVelocity().y > 0 && std::abs(actor->GetVelocity().x) < 0.001f && std::abs(actor->GetVelocity().z) < 0.001f,
            "standing jump injected full horizontal speed");
    reset(); input->value = {10, 0, 0}; step(); step(); Access::QueueJump(*mover); step();
    Require(actor->GetVelocity().y > 0 && actor->GetVelocity().x > 0 && actor->GetVelocity().x < 4,
            "jump bypassed acceleration");
    log << "OK standing and accelerating jumps preserve horizontal momentum" << std::endl;
    reset(); mover->SetPlatformMotion({10, 0, 0}); Access::QueueJump(*mover); step();
    const float carried = actor->GetVelocity().x;
    Require(std::abs(carried - 5.5f) < 0.01f, "moving floor takeoff momentum changed");
    step(); Require(actor->GetVelocity().x > 0 && actor->GetVelocity().x < carried, "moving floor momentum doubled");
    reset(); actor->SetGrounded(false); actor->SetVelocity({8.5f, 18, 0}); mover->SetAbilityAirImpulse({8.5f, 0, 0}); step();
    Require(std::abs(actor->GetVelocity().x - 8.5f) < 0.01f, "blast jump impulse doubled");
    for (int i = 0; i < 60; ++i) step();
    Require(std::abs(actor->GetVelocity().x) < 0.05f, "blast momentum became permanent locomotion");
    log << "OK moving floor and blast jump impulses are added once and decay" << std::endl;
    reset(); input->value = {10, 0, 0}; step();
    const auto before = actor->GetVelocity(); mover->Update(0);
    Require(actor->GetVelocity().x == before.x, "paused mover changed velocity");
    actor->SetVelocity({}); step();
    Require(actor->GetVelocity().x < 1, "wall stop retained hidden full speed");
    mover->ApplyDashPanelBoost(1, 1.5f, 1);
    Require(std::abs(actor->GetVelocity().x - 15) < 0.01f, "dash panel impulse was reduced");
    log << "OK pause, collision restart and dash panel keep their intended behavior" << std::endl;
    auto& movement = settings->GetMutablePlayerLocomotion();
    movement.accelerationTime = 0.41f; movement.brakingTime = 0.12f; movement.airControl = 0.42f;
    settings->MarkDirty(); Require(settings->Save(), "locomotion settings save failed");
    movement = {}; Require(settings->Reload(), "locomotion settings reload failed");
    const auto reopened = settings->GetPlayerLocomotion();
    Require(std::abs(reopened.accelerationTime - 0.41f) < 0.001f && std::abs(reopened.brakingTime - 0.12f) < 0.001f &&
            std::abs(reopened.airControl - 0.42f) < 0.001f, "locomotion editor settings were not preserved");
    settings->GetMutablePlayerLocomotion() = original; settings->MarkDirty(); Require(settings->Save(), "restore settings failed");
    log << "OK Editor settings save/reload round trip" << std::endl;
}
inline void AfterDraw(SceneManager* manager, const std::string& path) {
    static std::ofstream log(path, std::ios::trunc); static int phase = 0, frames = 0; static bool done = false;
    if (done || !manager || manager->IsTransitioning()) return;
    if (phase == 0) { manager->ChangeScene("GAMEPLAY"); phase = 1; return; }
    if (manager->GetCurrentSceneName() != "GAMEPLAY") return;
    manager->SetIsPlaying(false);
    if (++frames < 3) return;
    try { Run(manager->GetCurrentScene(), log); log << "PASS: player locomotion startup, braking, jump, impulses, pause and settings" << std::endl; }
    catch (const std::exception& error) { log << "FAIL: " << error.what() << std::endl; }
    done = true; WinApp::CloseNow();
}
} // namespace PlayerLocomotionRuntimeProbe
#endif