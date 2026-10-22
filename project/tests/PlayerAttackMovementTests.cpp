#include "game/actor/enemy/combat/EnemyAttackProfile.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

static void Require(bool value, const std::string& text) {
    // 呼び出し元へ失敗理由を返し、一つでも不一致なら終了コードを失敗にします。
    if (!value) throw std::runtime_error(text);
}

int main(int argc, char** argv) {
    try {
        // 本番アセットの発射待ち・本体姿勢と、移動制限のJSON往復を検証する。
        Require(argc == 2, "temporary json path required");
        EnemyAttackProfile profile;
        std::string error;
        for (const char* type : {"Slime", "FireSlime", "ThunderSlime", "WindSlime", "GiantSlime", "PrismSlime", "MagmaSlime", "FalseKingSlime"}) {
            EnemyAttackProfile legacy; Require(legacy.LoadForEnemy(type, &error), error);
        }
        Require(profile.LoadForEnemy("FireSlime", &error), error);
        const auto* attack = profile.FindAttack("player_fireball");
        Require(attack && attack->playerMovement.enabled, "player policy missing");
        Require(attack->sequence.tracks.size() == 1 && attack->sequence.tracks[0].type == "body_animation", "body track missing");
        AttackSequenceClock clock;
        Require(clock.Start(attack->sequence), "clock start");
        int shots = 0;
        auto execute = [&](const AttackSequenceEvent& event) { if (event.operation == "projectile") ++shots; };
        auto always = [](const std::string&) { return true; };
        clock.Update(0, always, execute);
        clock.Update(0.19f, always, execute);
        Require(shots == 0, "projectile escaped windup");
        clock.Update(0.01f, always, execute);
        Require(shots == 1, "projectile not released at motion peak");
        clock.Update(1, always, execute);
        Require(shots == 1, "duplicate projectile");
        auto movement = attack->playerMovement;
        const engine::Vector3 current{10, -7, 0}, desired{-10, 0, 0};
        const auto ground = movement.Apply(current, desired, true, 1.0f / 60);
        Require(std::abs(ground.x + 4.5f) < 0.0001f && ground.y == -7, "ground movement or gravity changed");
        const auto air = movement.Apply(current, desired, false, 1.0f / 60);
        Require(air.x > 0 && air.x < 10 && air.y == -7, "air momentum or gravity lost");
        auto slow = current, fast = current;
        for (int i = 0; i < 30; ++i) slow = movement.Apply(slow, desired, false, 1.0f / 30);
        for (int i = 0; i < 120; ++i) fast = movement.Apply(fast, desired, false, 1.0f / 120);
        Require(std::abs(slow.x - fast.x) < 0.0001f && fast.x < 0, "air response depends on frame rate or cannot turn");
        Require(movement.Apply(current, desired, false, 0).x == 10, "pause changed velocity");
        movement.enabled = false;
        Require(movement.Apply(current, desired, false, 0.1f).x == -10, "unrestricted abilities changed");
        auto authored = *attack;
        authored.playerMovement.groundSpeedScale = 0.32f;
        profile.attacks = {authored};
        Require(profile.SaveToFile(argv[1], &error), error);
        EnemyAttackProfile reopened;
        Require(reopened.LoadFromFile(argv[1], &error), error);
        Require(reopened.FindAttack("player_fireball")->playerMovement.groundSpeedScale == 0.32f, "policy round trip lost values");
        profile.attacks.front().playerMovement.airResponse = std::numeric_limits<float>::quiet_NaN();
        Require(!profile.SaveToFile(argv[1], &error), "invalid policy saved");
        Require(reopened.LoadFromFile(argv[1], &error), "failed save damaged json");
        Require(!reopened.FindAttack("fireball")->playerMovement.enabled, "enemy policy changed");
        std::cout << "PASS: fireball windup, motion track, ground/air control, frame independence, pause, roundtrip and invalid-save preservation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
