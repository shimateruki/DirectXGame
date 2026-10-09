#include "game/actor/enemy/combat/EnemyAttackProfile.h"
#include "game/actor/combat/AttackSequence.h"
#include "game/actor/combat/AttackPlacementMath.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

// 実際のJSON読込・複製・保存を通し、描画を必要としない時計と条件分岐を検証します。
void Check(bool result, const std::string& message) {
    if (!result) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "temporary output path required");
        EnemyAttackProfile profile;
        std::string error;
        for (const char* type : {"Slime", "FireSlime", "ThunderSlime", "WindSlime", "GiantSlime", "PrismSlime", "MagmaSlime", "FalseKingSlime"}) {
            EnemyAttackProfile existing;
            Check(existing.LoadForEnemy(type, &error), error);
        }
        Check(profile.LoadForEnemy("FireSlime", &error), error);
        EnemyAttackProfile missing;
        Check(!missing.LoadForEnemy("Bomber", &error) && missing.enemyType == "Bomber", "missing file fallback failed");
        const auto* enemy = profile.FindAttack("fireball");
        const auto* player = profile.FindAttack("player_fireball");
        Check(enemy && player && enemy->sequence.enabled && player->sequence.enabled, "shared definitions missing");
        Check(player->damage == 14 && player->sequence.binding == "player_special", "player configuration lost");

        auto copy = *enemy;
        Check(copy.sequence.actorControl.enabled && copy.sequence.tracks.size() == 1,
            "fireball lifecycle and body clip missing");
        // この後の時計単体テストでは、独立した予備動作・姿勢区間を持ち込みません。
        copy.sequence.actorControl = {};
        copy.sequence.tracks.clear();
        copy.id = "test_three_shots";
        copy.sequence.duration = 1.0f;
        copy.sequence.events.clear();
        AttackSequenceEvent first, last, middle;
        first.time = 0; middle.time = 0.5f; last.time = 1;
        middle.count = 3;
        copy.sequence.events = {last, first, middle}; // 保存順に依存しないことを確認します。
        profile.attacks.push_back(copy);
        Check(profile.SaveToFile(argv[1], &error), error);
        EnemyAttackProfile reloaded;
        Check(reloaded.LoadFromFile(argv[1], &error), error);
        const auto* saved = reloaded.FindAttack(copy.id);
        Check(saved && saved->sequence.events.size() == 3, "clone round trip failed");
        AttackSequenceClock clock;
        Check(clock.Start(saved->sequence), "start failed");
        std::vector<float> fired;
        int projectileCount = 0;
        auto execute = [&](const AttackSequenceEvent& event) { fired.push_back(event.time); projectileCount += event.count; };
        auto always = [](const std::string&) { return true; };
        clock.Update(0, always, execute);
        Check(fired.size() == 1 && fired[0] == 0, "time zero did not execute");
        clock.Update(1, always, execute);
        Check(fired.size() == 3 && fired[1] == 0.5f && fired[2] == 1 && projectileCount == 5, "crossed events/order lost");
        clock.Update(1, always, execute);
        Check(fired.size() == 3 && !clock.IsRunning(), "finished sequence executed again");

        auto conditional = saved->sequence;
        conditional.events = {first, middle, last};
        conditional.events[1].condition = "grounded";
        Check(clock.Start(conditional), "condition start failed");
        fired.clear();
        clock.Update(1, [](const std::string& name) { return name == "always"; }, execute);
        Check(fired.size() == 2 && clock.GetSkippedCount() == 1, "skip branch failed");
        conditional.events[1].stopOnFalse = true;
        Check(clock.Start(conditional), "stop start failed");
        fired.clear();
        clock.Update(1, [](const std::string& name) { return name == "always"; }, execute);
        Check(fired.size() == 1 && !clock.IsRunning(), "stop branch leaked later events");

        Check(clock.Start(saved->sequence), "restart failed");
        fired.clear();
        clock.Update(-1, always, execute);
        clock.Update(std::numeric_limits<float>::quiet_NaN(), always, execute);
        Check(fired.empty(), "invalid time executed events");
        clock.Reset();
        clock.Update(2, always, execute);
        Check(fired.empty(), "cancel failed");
        Check(clock.Start(saved->sequence), "seek restart failed");
        for (int i = 0; i < 60; ++i) clock.Update(1.0f / 60, always, execute);
        Check(fired.size() == 3, "fixed step replay differs");
        auto invalid = saved->sequence;
        invalid.events[0].operation = "unknown";
        Check(!clock.Start(invalid), "unknown command accepted");
        invalid = saved->sequence;
        invalid.events[0].time = 99;
        Check(!clock.Start(invalid), "out of range time accepted");
        auto* badAttack = reloaded.FindAttack(copy.id);
        badAttack->sequence = invalid;
        Check(!reloaded.SaveToFile(argv[1], &error), "invalid sequence overwrote saved asset");
        EnemyAttackProfile intact;
        Check(intact.LoadFromFile(argv[1], &error) && intact.FindAttack(copy.id)->sequence.Validate(), "failed save damaged asset");
        // 旧Prism専用式と同じ位置・速度・角度になり、5発と発射光が各一回実行されることを確認します。
        EnemyAttackProfile prism;
        Check(prism.LoadForEnemy("PrismSlime", &error), error);
        const auto* fan = prism.FindAttack("fire_fan");
        Check(fan && fan->sequence.enabled && fan->sequence.events.size() == 10, "prism volley missing");
        AttackSequenceContext context;
        context.position = {1, 2, 3}; context.ownerSize = {2, 3, 2};
        context.hasTarget = true; context.targetDistance = 9;
        int shots = 0, flashes = 0;
        Check(clock.Start(fan->sequence), "prism clock failed");
        auto verifyFan = [&](const AttackSequenceEvent& event) {
            const auto emission = ResolveAttackSequenceEmission(event, context, fan->minSpeed, fan->maxSpeed);
            const int index = static_cast<int>(std::round(event.time / 0.095f));
            const float angle = (index - 2) * 0.13f;
            auto near = [](float a, float b) { return std::abs(a - b) < 0.00001f; };
            Check(near(emission.direction.x, std::sin(angle)) && near(emission.direction.z, std::cos(angle)), "fan angle changed");
            Check(near(emission.position.x, 1 + std::sin(angle) * 0.96f) &&
                near(emission.position.y, 2 + 1.44f + std::abs(index - 2) * 0.1f) &&
                near(emission.position.z, 3 + std::cos(angle) * 0.96f), "fan spawn changed");
            Check(near(emission.speed, 17.95f), "fan speed changed");
            if (event.operation == "projectile") ++shots;
            else { ++flashes; Check(event.directed && event.particleSpeedScale == 1.15f, "directed flash lost"); }
        };
        clock.Update(0, always, verifyFan);
        Check(shots == 1 && flashes == 1, "prism first shot delayed");
        clock.Update(0.30f, always, verifyFan);
        Check(shots == 4 && flashes == 4, "prism skipped staggered shots");
        clock.Update(1, always, verifyFan);
        clock.Update(1, always, verifyFan);
        Check(shots == 5 && flashes == 5 && !clock.IsRunning(), "prism shot count incorrect");
        context.targetDistance = 100;
        Check(ResolveAttackSequenceEmission(fan->sequence.events[0], context, 13, 28).speed == 28, "maximum speed clamp failed");
        context.targetDistance = 0;
        Check(ResolveAttackSequenceEmission(fan->sequence.events[0], context, 13, 28).speed == 13, "minimum speed clamp failed");
        AttackSequenceContext coincident;
        coincident.forward = {}; coincident.hasTarget = true;
        AttackSequenceEvent coincidentShot;
        coincidentShot.up = coincidentShot.forward = coincidentShot.targetHeight = 0;
        const auto fallback = ResolveAttackSequenceEmission(coincidentShot, coincident, 13, 28);
        Check(fallback.direction.x == 0 && fallback.direction.y == 0 && fallback.direction.z == 1,
            "coincident target lost fallback direction");
        auto prismCopy = *fan;
        prismCopy.id = "prism_variant";
        prism.attacks.push_back(prismCopy);
        const std::string prismPath = std::string(argv[1]) + ".prism.json";
        Check(prism.SaveToFile(prismPath, &error), error);
        Check(prism.LoadFromFile(prismPath, &error), error);
        const auto* variant = prism.FindAttack("prism_variant");
        Check(variant && variant->sequence.fireAppearance.green == 0.30f &&
            variant->sequence.fireAppearance.trailSpeedScale == 1.16f &&
            variant->sequence.events[0].sizeForward == 0.48f &&
            variant->sequence.events[0].distanceSpeedBias == 4 &&
            variant->sequence.events[1].directed, "prism round trip lost parameters");
        auto invalidAppearance = variant->sequence;
        invalidAppearance.fireAppearance.trailInterval = 0;
        Check(!invalidAppearance.Validate(), "zero trail interval accepted");
        invalidAppearance = variant->sequence;
        invalidAppearance.events[0].yawDegrees = std::numeric_limits<float>::quiet_NaN();
        Check(!invalidAppearance.Validate(), "invalid angle accepted");
        std::cout << "Prism sequence tests passed: 5 staggered shots, directed flashes, legacy position/speed/angle equivalence, appearance round trip\n";
        std::cout << "Attack sequence tests passed: JSON round trip, clone, ordering, zero/boundary time, branch skip/stop, cancel, replay, validation\n";
        AttackSequenceDefinition authored;
        for (const char* sample : {"Resources/json/attack_assets/authoring_sweep.json", "Resources/json/attack_assets/authoring_double_sweep.json"}) {
            AttackSequenceDefinition example;
            Check(EnemyAttackProfile::LoadSequenceAsset(sample, example, &error), error);
            Check(example.events.empty() && example.tracks.size() >= 2, "authoring example did not contain independent visual/hitbox tracks");
        }
        authored.enabled = true;
        authored.duration = 2;
        AttackSequenceTrack hitbox;
        hitbox.type = "hitbox";
        hitbox.begin = 0.25f; hitbox.end = 1.25f;
        auto secondKey = hitbox.keys.front();
        secondKey.time = 1; secondKey.position = {2, 3, 4};
        hitbox.keys.push_back(secondKey);
        authored.tracks.push_back(hitbox);
        Check(authored.Validate(&error), error);
        Check(hitbox.Evaluate(0.5f).position[0] == 1, "linear interpolation failed");
        hitbox.keys[0].interpolation = 0;
        Check(hitbox.Evaluate(0.99f).position[0] == 0 && hitbox.Evaluate(1).position[0] == 2, "step key boundary failed");
        hitbox.keys[0].interpolation = 2;
        Check(std::abs(hitbox.Evaluate(0.25f).position[0] - 0.3125f) < 0.00001f, "smoothstep interpolation failed");
        auto invalidTracks = authored;
        invalidTracks.tracks[0].keys[1].time = 0;
        Check(!invalidTracks.Validate(), "duplicate key times accepted");
        invalidTracks = authored;
        invalidTracks.tracks[0].end = 0.2f;
        Check(!invalidTracks.Validate(), "inverted interval accepted");
        invalidTracks = authored;
        invalidTracks.tracks[0].keys[0].position[1] = std::numeric_limits<float>::infinity();
        Check(!invalidTracks.Validate(), "nonfinite track position accepted");
        const std::string assetPath = std::string(argv[1]) + ".asset.json";
        Check(EnemyAttackProfile::SaveSequenceAsset(assetPath, authored, &error), error);
        AttackSequenceDefinition reference;
        reference.binding = "player_special"; reference.priority = 23;
        Check(EnemyAttackProfile::LoadSequenceAsset(assetPath, reference, &error), error);
        Check(reference.assetPath == assetPath && reference.binding == "player_special" && reference.priority == 23,
            "shared asset overwrote local binding");
        Check(reference.tracks.size() == 1 && reference.tracks[0].keys[1].position[2] == 4, "track asset round trip failed");
        EnemyAttackProfile assetProfile;
        assetProfile.enemyType = "AuthoringTest";
        EnemyAttackDefinition assetAttack;
        assetAttack.id = "authored"; assetAttack.sequence = reference;
        assetProfile.attacks.push_back(assetAttack);
        const std::string profilePath = std::string(argv[1]) + ".asset-profile.json";
        Check(assetProfile.SaveToFile(profilePath, &error), error);
        authored.tracks[0].radius = 2;
        Check(EnemyAttackProfile::SaveSequenceAsset(assetPath, authored, &error), error);
        Check(assetProfile.LoadFromFile(profilePath, &error), error);
        Check(assetProfile.FindAttack("authored")->sequence.tracks[0].radius == 2, "profile ignored updated shared asset");
        std::filesystem::remove(assetPath);
        Check(!assetProfile.LoadFromFile(profilePath, &error), "missing shared asset silently fell back");
        Check(assetProfile.FindAttack("authored")->sequence.tracks[0].radius == 2, "failed asset load destroyed valid profile");
        Check(clock.Start(authored), "track-only sequence could not start");
        clock.Update(2, always, execute);
        Check(!clock.IsRunning() && !clock.WasStoppedByCondition(), "track-only sequence did not finish normally");
        authored.events.emplace_back();
        authored.events[0].condition = "grounded";
        authored.events[0].time = 0.5f;
        authored.events[0].stopOnFalse = true;
        Check(clock.Start(authored), "conditional track sequence failed");
        clock.Update(10, [](const std::string&) { return false; }, execute);
        Check(clock.WasStoppedByCondition(), "large dt concealed conditional stop");
        Check(clock.GetElapsed() == 0.5f, "conditional stop evaluated tracks after the stop time");
        std::cout << "Attack authoring tests passed: track-only playback, interpolation, validation, shared assets, binding isolation, missing asset transaction, conditional stop\n";
        AttackSequenceDefinition combo;
        Check(EnemyAttackProfile::LoadSequenceAsset("Resources/json/attack_assets/authoring_contact_combo.json", combo, &error), error);
        Check(combo.tracks.size() == 6 && combo.tracks[2].conditionTrack == 1 &&
            combo.tracks[5].condition == "no_contact", "contact combo wiring missing");
        auto invalidCombo = combo;
        invalidCombo.tracks[2].conditionTrack = 50;
        Check(!invalidCombo.Validate(), "missing branch source accepted");
        invalidCombo = combo;
        invalidCombo.tracks[2].conditionTrack = 0;
        Check(!invalidCombo.Validate(), "visual branch source accepted");
        invalidCombo = combo;
        invalidCombo.tracks[1].enabled = false;
        Check(!invalidCombo.Validate(), "disabled branch source accepted");
        invalidCombo = combo;
        invalidCombo.tracks[1].end = 0.8f;
        Check(!invalidCombo.Validate(), "branch started before source completion");
        invalidCombo = combo;
        invalidCombo.tracks[5].begin = 0.5f;
        Check(!invalidCombo.Validate(), "overlapping body animations accepted");
        invalidCombo = combo;
        invalidCombo.tracks[4].playbackSpeed = 0;
        Check(!invalidCombo.Validate(), "zero animation speed accepted");
        invalidCombo = combo;
        invalidCombo.tracks[4].blendOut = std::numeric_limits<float>::infinity();
        Check(!invalidCombo.Validate(), "nonfinite animation blend accepted");
        // 配列順を逆にしても参照と時間が正しければ保存可能。実行時は開始時刻順で評価します。
        auto reorderedCombo = combo;
        std::reverse(reorderedCombo.tracks.begin(), reorderedCombo.tracks.end());
        for (auto& track : reorderedCombo.tracks)
            if (track.conditionTrack >= 0) track.conditionTrack = static_cast<int>(reorderedCombo.tracks.size()) - 1 - track.conditionTrack;
        Check(reorderedCombo.Validate(&error), error);
        const std::string comboPath = std::string(argv[1]) + ".combo.json";
        combo.tracks[4].clipOffset = 0.15f;
        combo.tracks[4].playbackSpeed = 1.5f;
        combo.tracks[4].loop = true;
        Check(EnemyAttackProfile::SaveSequenceAsset(comboPath, combo, &error), error);
        AttackSequenceDefinition loadedCombo;
        Check(EnemyAttackProfile::LoadSequenceAsset(comboPath, loadedCombo, &error), error);
        Check(loadedCombo.tracks[4].type == "body_animation" && loadedCombo.tracks[4].clipOffset == 0.15f &&
            loadedCombo.tracks[4].playbackSpeed == 1.5f && loadedCombo.tracks[4].loop &&
            loadedCombo.tracks[4].blendOut == 0.08f && loadedCombo.tracks[5].conditionTrack == 1,
            "animation/branch JSON round trip lost settings");
        BodyAnimationClip body;
        Check(body.Load(BodyAnimationClip::ResolveAssetPath(combo.tracks[4].preset)), "sample body clip missing");
        BodyAnimationClip::Sample poseA, poseB, poseEnd;
        Check(body.Evaluate(0.2f, false, poseA) && body.Evaluate(0.2f + body.GetDuration(), true, poseB), "body sampling failed");
        Check(std::abs(poseA.scale.y - poseB.scale.y) < 0.0001f, "body loop sampling diverged");
        Check(body.Evaluate(100, false, poseEnd), "body end hold failed");
        Check(std::abs(poseEnd.scale.y - body.GetKeys().back().scale.y) < 0.0001f, "body end pose was not held");
        std::cout << "Body/branch asset tests passed: source validation, interval dependency, reordered references, body overlap, JSON round trip, clip sampling\n";
        AttackSequenceDefinition homing;
        Check(EnemyAttackProfile::LoadSequenceAsset("Resources/json/attack_assets/authoring_homing.json", homing, &error), error);
        Check(homing.tracks[1].parentTrack == 0 && homing.tracks[0].motion == "homing", "motion sample wiring failed");
        auto badMotion = homing;
        badMotion.tracks[1].parentTrack = 1;
        Check(!badMotion.Validate(), "self parent accepted");
        badMotion = homing;
        badMotion.tracks[1].end = 3;
        badMotion.duration = 3;
        Check(!badMotion.Validate(), "child outlived parent");
        badMotion = homing;
        badMotion.tracks[0].launchDelay = 2.5f;
        Check(!badMotion.Validate(), "launch outside interval accepted");
        badMotion = homing;
        badMotion.tracks[0].moveSpeed = -1;
        Check(!badMotion.Validate(), "negative motion speed accepted");
        auto motionTrack = homing.tracks[0];
        motionTrack.motion = "aim_launch";
        AttackMotionState straight;
        straight.Advance(motionTrack, 0.5f, {0, 0, 100}, true);
        Check(Math::Length(straight.position) == 0, "projectile moved before launch");
        straight.Advance(motionTrack, 1.5f, {100, 0, 0}, true);
        Check(std::abs(straight.position.z - 4) < 0.001f && std::abs(straight.position.x) < 0.001f,
            "locked aim changed after launch");
        motionTrack.motion = "homing"; motionTrack.turnDegreesPerSecond = 90; motionTrack.homingDuration = 0.5f;
        AttackMotionState seeking;
        seeking.Advance(motionTrack, 0.5f, {0, 0, 100}, true);
        seeking.Advance(motionTrack, 1.0f, {100, 0, 0}, true);
        Check(seeking.direction.x > 0.6f && std::acos(std::clamp(seeking.direction.z, -1.0f, 1.0f)) < 0.786f,
            "homing exceeded angular speed or did not turn");
        const auto locked = seeking.direction;
        seeking.Advance(motionTrack, 2.0f, {-100, 0, 0}, true);
        Check(Math::Length(seeking.direction - locked) < 0.00001f, "homing continued after tracking window");
        AttackMotionState reverse;
        reverse.launched = true; reverse.elapsed = 0.5f;
        reverse.Advance(motionTrack, 0.75f, {0, 0, -100}, true);
        Check(std::isfinite(reverse.direction.x) && std::abs(Math::Length(reverse.direction) - 1) < 0.0001f,
            "opposite target broke homing rotation");
        AttackMotionState missingTargetMotion;
        missingTargetMotion.Advance(motionTrack, 1.5f, {}, false);
        Check(std::abs(missingTargetMotion.position.z - 4) < 0.001f, "missing target did not continue forward");
        const std::string motionPath = std::string(argv[1]) + ".motion.json";
        Check(EnemyAttackProfile::SaveSequenceAsset(motionPath, homing, &error), error);
        AttackSequenceDefinition loadedMotion;
        Check(EnemyAttackProfile::LoadSequenceAsset(motionPath, loadedMotion, &error), error);
        Check(loadedMotion.tracks[0].turnDegreesPerSecond == 100 && loadedMotion.tracks[0].homingDuration == 1.2f &&
            loadedMotion.tracks[1].parentTrack == 0, "motion round trip lost settings");
        EnemyAttackProfile lottery;
        lottery.enemyType = "SelectionTest";
        EnemyAttackDefinition a, b;
        a.id = "a"; a.sequence = homing; a.minRange = 0; a.maxRange = 10; a.selectionWeight = 1; a.sequence.priority = 3;
        a.sequence.assetPath.clear(); // エディターの新規作成・複製と同じ独立した攻撃を検証します。
        b = a; b.id = "b"; b.selectionWeight = 3; b.sequence.priority = 1;
        lottery.attacks = {a, b};
        Check(lottery.SelectSequence(2, 10, {}, 0.9f)->id == "a", "priority selection changed");
        lottery.selectionMode = "weighted";
        int countA = 0;
        for (int i = 0; i < 100; ++i) if (lottery.SelectSequence(2, 10, {}, (i + 0.5f) / 100)->id == "a") ++countA;
        Check(countA == 25, "weighted selection probabilities incorrect");
        lottery.attacks[0].selectionWeight = 0;
        Check(lottery.SelectSequence(2, 10, {}, 0)->id == "b", "zero weight remained eligible");
        Check(lottery.SelectSequence(2, 10, "a", 0)->id == "a", "preview could not force zero weight attack");
        lottery.attacks[1].minRange = 3;
        Check(!lottery.SelectSequence(2, 10, {}, 0), "range-ineligible attack selected");
        lottery.selectionMode = "uniform";
        lottery.attacks[1].minRange = 0;
        Check(lottery.SelectSequence(2, 10, {}, 0.49f)->id == "a" && lottery.SelectSequence(2, 10, {}, 0.5f)->id == "b",
            "uniform selection boundaries failed");
        const std::string lotteryPath = std::string(argv[1]) + ".selection.json";
        Check(lottery.SaveToFile(lotteryPath, &error), error);
        EnemyAttackProfile loadedLottery;
        Check(loadedLottery.LoadFromFile(lotteryPath, &error), error);
        Check(loadedLottery.selectionMode == "uniform" && loadedLottery.FindAttack("b")->selectionWeight == 3,
            "selection round trip lost settings");
        // 既存IDの上書き編集で攻撃が増殖せず、選択していない攻撃も保持されることを確認します。
        const auto originalCount = loadedLottery.attacks.size();
        auto* edited = loadedLottery.FindAttack("a");
        edited->displayName = "再編集した攻撃";
        edited->sequence.tracks[0].keys.front().position = {2, 3, 4};
        edited->sequence.tracks[0].keys.front().scale = {0.5f, 0.75f, 1.25f};
        edited->sequence.tracks[1].begin = 0.75f;
        Check(loadedLottery.SaveToFile(lotteryPath, &error), error);
        EnemyAttackProfile reopened;
        Check(reopened.LoadFromFile(lotteryPath, &error), error);
        const auto* reopenedAttack = reopened.FindAttack("a");
        Check(reopened.attacks.size() == originalCount && reopenedAttack && reopenedAttack->displayName == "再編集した攻撃",
            "re-edit duplicated or lost existing attack");
        Check(reopenedAttack->sequence.tracks[0].keys.front().position[1] == 3 &&
            reopenedAttack->sequence.tracks[0].keys.front().scale[2] == 1.25f &&
            reopenedAttack->sequence.tracks[1].begin == 0.75f && reopenedAttack->sequence.tracks[1].parentTrack == 0,
            "re-edit lost placement, interval or parent relationship");
        Check(reopened.FindAttack("b")->sequence.tracks[0].keys.front().position == b.sequence.tracks[0].keys.front().position,
            "re-edit changed a different attack");
        // 壊れたファイルを開いた場合、読込対象が持っていた有効な編集を残します。
        const std::string brokenPath = std::string(argv[1]) + ".broken.json";
        { std::ofstream broken(brokenPath); broken << "{ invalid json"; }
        Check(!reopened.LoadFromFile(brokenPath, &error), "malformed profile accepted");
        Check(reopened.FindAttack("a") && reopened.FindAttack("a")->displayName == "再編集した攻撃",
            "failed reload destroyed edited profile");
        std::cout << "Re-edit tests passed: stable IDs/count, placement, timing, parent, unrelated attack and failed reload preservation\n";
        std::cout << "Motion/selection tests passed: locked aim, turn limit, opposite/missing target, tracking end, parent validation, JSON round trip, weighted/uniform selection\n";
        // 弾ごとの設定を再編集して保存しても、隣の弾と旧形式の互換性を保ちます。
        EnemyAttackDefinition legacy = *reloaded.FindAttack("fireball");
        AttackSequenceEvent legacyEvent;
        const auto componentFallback = ResolveProjectileComponent(legacy, legacyEvent);
        Check(std::abs(componentFallback.radius - legacy.radius * legacy.sequence.visualScale) < 0.0001f,
            "legacy world radius changed");
        EnemyAttackProfile components;
        components.enemyType = "ComponentsTest";
        legacy.id = "components";
        legacy.sequence.assetPath.clear();
        legacy.sequence.events = {legacyEvent, legacyEvent};
        for (auto& event : legacy.sequence.events) event.projectile = componentFallback;
        auto& part = legacy.sequence.events.front();
        part.name = "火球・当たり判定なし";
        part.enabled = false;
        part.stopOnFalse = true;
        part.projectile.collisionEnabled = false;
        part.projectile.radius = 1.25f;
        part.projectile.visualScale = 0.8f;
        part.projectile.trail.clear();
        part.projectile.impact = "custom_impact";
        part.projectile.appearance.green = 0.7f;
        components.attacks.push_back(legacy);
        const std::string componentsPath = std::string(argv[1]) + ".components.json";
        Check(components.SaveToFile(componentsPath, &error), error);
        EnemyAttackProfile reopenedComponents;
        Check(reopenedComponents.LoadFromFile(componentsPath, &error), error);
        const auto& events = reopenedComponents.attacks.front().sequence.events;
        Check(events[0].name == part.name && !events[0].enabled && !events[0].projectile.collisionEnabled &&
            events[0].projectile.radius == 1.25f && events[0].projectile.visualScale == 0.8f &&
            events[0].projectile.trail.empty() && events[0].projectile.impact == "custom_impact" &&
            events[0].projectile.appearance.green == 0.7f, "projectile component round trip lost edits");
        Check(events[1].projectile.radius == componentFallback.radius && events[1].projectile.collisionEnabled,
            "editing one projectile changed another projectile");
        Check(clock.Start(reopenedComponents.attacks.front().sequence), "component clock start failed");
        int enabledEvents = 0;
        clock.Update(1, always, [&](const AttackSequenceEvent&) { ++enabledEvents; });
        Check(enabledEvents == 1 && !clock.WasStoppedByCondition(), "disabled projectile stopped later events");
        auto badComponent = componentFallback;
        badComponent.radius = std::numeric_limits<float>::quiet_NaN();
        Check(!badComponent.Validate(), "non-finite projectile radius accepted");
        badComponent = componentFallback; badComponent.minSpeed = badComponent.maxSpeed + 1;
        Check(!badComponent.Validate(), "inverted projectile speed range accepted");
        std::cout << "Projectile component tests passed: legacy radius, independent edits, JSON round trip, disabled events, invalid values\n";
        // 配置ギズモの編集値を実ランタイムの座標解決へ渡して往復を確認します。
        AttackSequenceContext placementContext;
        placementContext.position = {10,2,-7};
        placementContext.forward = {1,0,0};
        placementContext.ownerSize = {2,3,4};
        AttackSequenceEvent placed;
        placed.right = 1.3f; placed.up = 2; placed.forward = -0.7f;
        placed.sizeRight = 0.4f; placed.sizeForward = 0.8f; placed.yawDegrees = 35;
        const auto initialPosition = ResolveAttackSequenceEmission(placed,placementContext,0,0).position;
        AttackPlacementMath::RotateEvent(placed,placementContext.ownerSize,1.2f);
        const auto rotatedPosition = ResolveAttackSequenceEmission(placed,placementContext,0,0).position;
        Check(Math::Length(rotatedPosition-initialPosition)<0.0001f,"gizmo rotation moved emission position");
        const Vector3 worldDelta{3,1,-2};
        const auto localDelta = AttackPlacementMath::ToLocalDelta(worldDelta,1.57079632679f+placed.yawDegrees*0.01745329252f);
        placed.right += localDelta.x; placed.up += localDelta.y; placed.forward += localDelta.z;
        Check(Math::Length(ResolveAttackSequenceEmission(placed,placementContext,0,0).position-initialPosition-worldDelta)<0.0001f,
            "gizmo translation did not preserve world delta");
        AttackSequenceTrack keyed;
        auto endKey = keyed.keys.front(); endKey.time = 1; endKey.position = {2,4,6}; keyed.keys.push_back(endKey);
        auto midKey = keyed.Evaluate(0.5f); midKey.time = 0.5f; midKey.position[0] = 9;
        Check(AttackPlacementMath::PutKey(keyed,midKey) && keyed.keys.size()==3 && keyed.keys[1].position[0]==9,
            "gizmo did not insert interpolated key at current time");
        midKey.position[0] = 8;
        Check(AttackPlacementMath::PutKey(keyed,midKey) && keyed.keys.size()==3 && keyed.keys[1].position[0]==8,
            "gizmo duplicated existing time key");
        components.attacks.front().sequence.events = {placed};
        components.attacks.front().sequence.duration = 1;
        components.attacks.front().sequence.tracks = {keyed};
        components.attacks.front().sequence.tracks.front().preset = "Resources/json/effect/test.json";
        Check(components.SaveToFile(componentsPath,&error),error);
        Check(reopenedComponents.LoadFromFile(componentsPath,&error),error);
        const auto& placedSaved = reopenedComponents.attacks.front().sequence;
        Check(Math::Length(ResolveAttackSequenceEmission(placedSaved.events.front(),placementContext,0,0).position-initialPosition-worldDelta)<0.0001f &&
            placedSaved.tracks.front().keys[1].position[0]==8,"placement save/reopen changed transform");
        std::cout << "Placement tests passed: rotated offset basis, size offsets, rotation pivot, current-time keys, save/reopen\n";
        auto lifecycle = *profile.FindAttack("fireball");
        lifecycle.sequence.actorControl.aimLockTime = 0.19f;
        lifecycle.sequence.actorControl.telegraphWidth = 1.7f;
        EnemyAttackProfile lifecycleProfile;
        lifecycleProfile.enemyType = "FireSlime";
        lifecycleProfile.attacks = {lifecycle};
        Check(lifecycleProfile.SaveToFile(componentsPath,&error),error);
        EnemyAttackProfile lifecycleReloaded;
        Check(lifecycleReloaded.LoadFromFile(componentsPath,&error),error);
        const auto& control = lifecycleReloaded.FindAttack("fireball")->sequence.actorControl;
        Check(control.enabled && control.aimLockTime == 0.19f && control.telegraphWidth == 1.7f,
            "actor lifecycle roundtrip failed");
        lifecycleProfile.attacks.front().sequence.actorControl.aimLockTime = 99;
        Check(!lifecycleProfile.SaveToFile(componentsPath,&error),"invalid lifecycle accepted");
        Check(lifecycleReloaded.LoadFromFile(componentsPath,&error) &&
            lifecycleReloaded.FindAttack("fireball")->sequence.actorControl.aimLockTime == 0.19f,
            "invalid save damaged existing file");
        std::cout << "Lifecycle tests passed: actor settings roundtrip and invalid-save preservation\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
