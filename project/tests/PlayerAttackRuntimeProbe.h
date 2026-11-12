#pragma once
#ifdef USE_IMGUI
#include "SceneManager.h"
#include <numbers>
#include "Player.h"
#include "IMoveStrategy.h"
#include "game/system/combat/BulletManager.h"
#include "WinApp.h"
#include <fstream>
#include <stdexcept>

// 専用起動だけで隔離Actorを生成し、本編と同じ入力・能力・移動処理を固定刻みで確認します。
namespace PlayerAttackRuntimeProbe {
inline bool Enabled() {
    // 通常プレイでは起動しません。
    static const bool enabled = GetEnvironmentVariableA("CG2_PLAYER_ATTACK_TEST", nullptr, 0) > 0;
    return enabled;
}
class MoveInput final : public IMoveStrategy {
  public:
    engine::Vector3 value{10, 0, 0};
    engine::Vector3 CalculateVelocity(Player*) override {
        // 実機入力に依存しない移動速度を、通常のMoverへ渡します。
        return value;
    }
};
inline void AfterDraw(SceneManager* manager, const std::string& path) {
    static std::ofstream log(path, std::ios::trunc);
    static std::unique_ptr<Player> actor;
    static std::unique_ptr<PlayerMover> mover;
    static MoveInput* input = nullptr;
    static EnemyAttackProfile profile;
    static int phase = 0, frame = 0;
    static bool done = false;
    if (done || !manager || manager->IsTransitioning()) return;
    auto require = [](bool result, const std::string& text) { if (!result) throw std::runtime_error(text); };
    auto shots = [] {
        int count = 0;
        for (const auto& bullet : BulletManager::GetInstance()->GetBullets())
            if (bullet && bullet->GetName() == "__PlayerAttackProbe") ++count;
        return count;
    };
    auto finish = [&](const std::string& text) {
        // GPU・シーン基盤より先にActorとMoverを片付け、終了処理も検証します。
        log << text << std::endl;
        mover.reset(); actor.reset();
        done = true;
        WinApp::CloseNow();
    };
    try {
        if (phase == 0) { manager->ChangeScene("GAMEPLAY"); phase = 1; return; }
        if (manager->GetCurrentSceneName() != "GAMEPLAY") return;
        manager->SetIsPlaying(false);
        if (phase == 1) {
            if (++frame < 3) return;
            auto* scene = manager->GetCurrentScene();
            actor = std::make_unique<Player>();
            actor->Initialize(scene->GetObject3DCommon(), engine::InputManager::GetInstance(), scene->GetParticleSystem(), scene->GetSpriteCommon());
            actor->SetEditorInternal(true);
            actor->SetCollisionAttribute(0); actor->SetCollisionMask(0);
            actor->SetTranslate({0, 1000, 0}); actor->SetGrounded(true);
            actor->SetCopyPreviewInput({}); actor->SetCopyPreviewGroundHeight(1000);
            actor->SetGravity(50); actor->SetMaxFallSpeed(60);
            require(actor->ApplyStoredCopy("FireSlime"), "apply stored fire");
            std::string error;
            require(profile.LoadForEnemy("FireSlime", &error), error);
            for (auto& event : profile.FindAttack("player_fireball")->sequence.events)
                if (event.operation == "projectile") event.name = "__PlayerAttackProbe";
            actor->SetCopyPreviewProfile(profile);
            mover = std::make_unique<PlayerMover>();
            mover->Initialize(actor.get(), engine::InputManager::GetInstance(), scene->GetParticleSystem());
            auto strategy = std::make_unique<MoveInput>(); input = strategy.get();
            mover->SetStrategy(std::move(strategy));
            frame = 0; phase = 2;
            return;
        }
        constexpr float kFixedDeltaTime = 1.0f / 60;
        if (phase == 2 || phase == 3) {
            // 開始フレームの次からMove→物理→攻撃の順に進め、実プレイヤーと更新順を揃えます。
            PlayerCopyAbilityInput ability;
            ability.specialTriggered = true;
            ability.secondaryTriggered = frame > 0 && frame < 34;
            if (phase == 3 && frame == 5) input->value = {-10, 0, 0};
            if (phase == 3 && frame == 8) actor->SetMoveYaw(std::numbers::pi_v<float>);
            mover->Update(kFixedDeltaTime);
            actor->SetCopyPreviewInput(ability);
            actor->Update(kFixedDeltaTime);
            const float elapsed = (frame + 1) * kFixedDeltaTime;
            const int expectedBefore = phase == 2 ? 0 : 2;
            if (frame < 11) require(shots() == expectedBefore, "fireball emitted before windup");
            if (frame == 12) require(shots() == expectedBefore + 1, "fireball did not emit once");
            if (frame > 0 && frame < 22) {
                require(actor->GetAttackMovement().enabled, "attack interrupted or restriction absent");
                if (phase == 2) require(std::abs(actor->GetVelocity().x - 4.5f) < 0.01f, "ground slowdown missing");
                if (phase == 3 && frame == 1) require(actor->GetVelocity().x > 8.5f && actor->GetVelocity().x < 10, "air movement stopped abruptly");
            }
            if (frame == 26) {
                require(!actor->GetAttackMovement().enabled, "movement restriction outlived window");
                require(shots() == expectedBefore + 1, "recovery canceled into another shot");
            }
            if (frame == 49) {
                require(shots() == expectedBefore + 2, "next legal shot did not start");
                if (phase == 3) {
                    require(actor->GetVelocity().y < 0 && actor->GetTranslate().y < 1030, "air attack suspended gravity");
                    require(actor->GetVelocity().x < 0, "air steering disabled");
                }
                log << "OK " << (phase == 2 ? "ground" : "air") << " windup, spam rejection, release and next-shot; t=" << elapsed << std::endl;
                // 次の予備動作中にコピーを切り替え、古い発射イベントと減速を破棄する。
                actor->SetCopyPreviewProfile(profile);
                require(!actor->GetAttackMovement().enabled, "copy reset retained restriction");
                actor->SetCopyPreviewInput({true, false, false, false, false}); actor->Update(kFixedDeltaTime);
                for (int i = 0; i < 5; ++i) { actor->SetCopyPreviewInput({}); actor->Update(kFixedDeltaTime); }
                actor->SetCopyPreviewProfile(profile);
                for (int i = 0; i < 40; ++i) { actor->SetCopyPreviewInput({}); actor->Update(kFixedDeltaTime); }
                require(shots() == expectedBefore + 2, "canceled attack fired later");
                if (phase == 2) {
                    actor->SetTranslate({0, 1030, 0}); actor->SetVelocity({10, 4, 0}); actor->SetGrounded(false);
                    input->value = {10, 0, 0}; frame = 0; phase = 3;
                    return;
                }
                finish("PASS: player fireball ground/air runtime, windup, movement, recovery, steering, gravity and cancel");
                return;
            }
            ++frame;
        }
    } catch (const std::exception& error) { finish(std::string("FAIL: ") + error.what()); }
}
} // namespace PlayerAttackRuntimeProbe
#endif
