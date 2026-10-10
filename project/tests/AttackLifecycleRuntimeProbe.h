#pragma once
#ifdef USE_IMGUI
#include "SceneManager.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "EnemyFireSlime.h"
#include "game/actor/enemy/combat/ISequencePreviewEnemy.h"
#include "Player.h"
#include "BulletManager.h"
#include "EventManager.h"
#include "WinApp.h"
#include <fstream>

// 明示的な検証起動だけで実GAMEPLAYのAI、Bullet、Collision、GameRuleを通します。
// プロファイルの復元は起動スクリプトがfinallyで担当します。
namespace AttackLifecycleRuntimeProbe {
inline bool Enabled() {
    static const bool enabled = GetEnvironmentVariableA("CG2_ATTACK_LIFECYCLE_TEST", nullptr, 0) > 0;
    return enabled;
}
inline bool WantsPlay(SceneManager* manager) {
    return Enabled() && manager && !manager->IsTransitioning() && manager->GetCurrentSceneName() == "GAMEPLAY";
}
inline void AfterDraw(SceneManager* manager, const std::string& path) {
    if (!Enabled() || !manager) return;
    static std::ofstream log(path, std::ios::trunc);
    static ULONGLONG began = GetTickCount64();
    static int phase = 0, frames = 0;
    static BaseEnemy* enemy = nullptr;
    static float hpBefore = 0;
    static int damageEvents = 0;
    static bool sawBullet = false, sawAction = false, ended = false;
    static EnemyAttackProfile authored;
    static bool done = false;
    if (done) return;
    auto finish = [&](const std::string& message) { log << message << std::endl; done = true; WinApp::CloseNow(); };
    if (GetTickCount64() - began > 300000) { finish("FAIL: lifecycle timeout"); return; }
    if (manager->IsTransitioning()) return;
    if (phase == 0) {
        manager->ChangeScene("GAMEPLAY"); phase = 1;
        log << "REQUEST GAMEPLAY" << std::endl; return;
    }
    auto* scene = manager->GetCurrentScene();
    if (!scene || manager->GetCurrentSceneName() != "GAMEPLAY") return;
    auto* player = scene->GetPlayer();
    if (!player || !player->param_) { finish("FAIL: no runtime player"); return; }
    if (phase == 1) {
        // 入場ムービーの無敵・操作ロックが解けてから通常の被弾を検証します。
        if (++frames < 240 || player->IsCinematicLocked() || !player->IsControlActive()) return;
        for (auto& object : scene->GetObjects())
            if (auto* other = dynamic_cast<BaseEnemy*>(object.get())) { other->SetIsVisible(false); other->SetCollisionMask(0); }
        std::string error;
        if (!authored.LoadForEnemy("FireSlime", &error)) { finish("FAIL: " + error); return; }
        auto attack = *authored.FindAttack("fireball");
        attack.id = "runtime_authored_attack";
        attack.displayName = "保存再読込の検証用攻撃";
        attack.sequence.priority = 999;
        attack.cooldown = 30;
        attack.statusEffectType = "none"; // 継続ダメージと直撃の結果を分離します。
        attack.sequence.events.front().projectile.damage = 2;
        attack.sequence.events.front().name = "RuntimeAuthoredProjectile";
        authored.attacks.push_back(attack);
        EventManager::GetInstance()->Subscribe([](const DamageEvent& event) {
            const auto* bullet = dynamic_cast<const Bullet*>(event.attacker);
            if (bullet && bullet->GetName() == "RuntimeAuthoredProjectile") ++damageEvents;
        });
        phase = 2; frames = 0;
    }
    if (phase == 2 || phase == 4) {
        const bool hitEnabled = phase == 2;
        if (enemy) { enemy->SetIsVisible(false); enemy->SetCollisionMask(0); }
        auto* attack = authored.FindAttack("runtime_authored_attack");
        attack->sequence.events.front().projectile.collisionEnabled = hitEnabled;
        const auto profilePath = EnemyAttackProfile::GetDefaultPath("FireSlime");
        std::string error;
        if (!authored.SaveToFile(profilePath, &error)) { finish("FAIL save: " + error); return; }
        EnemyAttackProfile reopened;
        if (!reopened.LoadFromFile(profilePath, &error)) { finish("FAIL reopen: " + error); return; }
        const auto* saved = reopened.FindAttack(attack->id);
        if (!saved || saved->sequence.events.front().projectile.collisionEnabled != hitEnabled ||
            !saved->sequence.actorControl.enabled) { finish("FAIL: saved data mismatch"); return; }
        EnemyAttackProfile::InvalidateCache("FireSlime");
        auto created = EnemyFactory::GetInstance()->CreateEnemy("FireSlime", scene->GetObject3dCommon());
        if (!created || !created->param_) { finish("FAIL: runtime enemy initialization"); return; }
        // ステージの障害物から離し、実PlayerのCollider設定は変更しません。
        player->SetTranslate({0, 100, 0}); player->SetVelocity({});
        player->SetGravity(0); player->SetMaxFallSpeed(0);
        player->param_->gravity = player->param_->maxFallSpeed = 0;
        player->param_->hp = player->param_->maxHp = 20;
        player->SetTutorialSafetyEnabled(false); player->SetDamageInvincible(false);
        player->SetDashInvincible(false); player->SetGuardInvincible(false);
        player->UpdateWorldMatrix(true);
        created->SetTranslate({0, 100, -8}); created->SetVelocity({});
        created->SetGravity(0); created->SetMaxFallSpeed(0);
        created->param_->gravity = created->param_->maxFallSpeed = created->param_->speed = 0;
        created->param_->detectionRange = 30;
        created->SetDetectionRange(30); created->SetTarget(player);
        created->ApplyManagedScale({2, 2, 2}); created->SetGrounded(true);
        created->UpdateWorldMatrix(true);
        enemy = created.get(); scene->AddObject(std::move(created));
        hpBefore = player->GetHp(); damageEvents = 0; sawBullet = sawAction = ended = false;
        frames = 0; ++phase;
        log << "SAVED REOPENED SPAWNED collision=" << hitEnabled << " hp=" << hpBefore << std::endl;
        return;
    }
    ++frames;
    const auto* sequence = dynamic_cast<const ISequencePreviewEnemy*>(enemy);
    const char* stateLabel = sequence ? sequence->GetSequenceStateLabel() : nullptr;
    const std::string state = stateLabel ? stateLabel : "";
    if (state.find("実行中") != std::string::npos) sawAction = true;
    if (sawAction && state.find("終了") != std::string::npos) ended = true;
    for (const auto& bullet : BulletManager::GetInstance()->GetBullets())
        if (bullet->GetName() == "RuntimeAuthoredProjectile") sawBullet = true;
    if (frames % 120 == 0) log << "FRAME " << frames << " hp=" << player->GetHp() << " state=" << state << " bullet=" << sawBullet << std::endl;
    if (!ended || frames < 420) return;
    if (!sawBullet || !sawAction) { finish("FAIL: runtime did not execute authored attack"); return; }
    const float lost = hpBefore - player->GetHp();
    log << "RESULT collision=" << (phase == 3) << " hpLost=" << lost << " damageEvents=" << damageEvents << std::endl;
    if (phase == 3) {
        if (std::abs(lost - 2.0f) > 0.01f || damageEvents != 1) { finish("FAIL: runtime projectile damage mismatch"); return; }
        phase = 4;
    } else {
        if (lost != 0 || damageEvents != 0) { finish("FAIL: disabled collider caused damage"); return; }
        for (const auto& bullet : BulletManager::GetInstance()->GetBullets())
            if (bullet->GetName() == "RuntimeAuthoredProjectile") { finish("FAIL: lifetime did not retire projectile"); return; }
        finish("PASS: save/reopen, AI selection, windup/body clip, shot, real player damage, end, collider OFF and lifetime");
    }
}
}
#endif
