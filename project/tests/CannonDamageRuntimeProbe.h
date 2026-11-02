#pragma once
#ifdef USE_IMGUI
#include "SceneManager.h"
#include "GimmickFireCannon.h"
#include "Player.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "CollisionManager.h"
#include "game/system/combat/BulletManager.h"
#include "game/system/rules/EventManager.h"
#include "WinApp.h"
#include <fstream>
#include <stdexcept>

// 実際の砲台から撃ち、共通BulletとGameRuleでHPが減ることを隔離Actorで確認します。
namespace CannonDamageRuntimeProbe {
inline bool Enabled() {
    // 通常起動ではテスト用Actorを生成しません。
    static const bool enabled = GetEnvironmentVariableA("CG2_CANNON_DAMAGE_TEST", nullptr, 0) > 0;
    return enabled;
}
// 隔離Actorを配置して実際の弾更新と衝突を通す。step は検証時の経過秒。
inline bool RunCase(SceneManager* manager, int mode, float radius, float blast, bool preview, bool invincible, std::ostream& log, float step = 0.2f) {
    // 配置データを変えず、高空の隔離空間で一発だけ飛ばします。
    auto* scene = manager->GetCurrentScene();
    auto* bullets = BulletManager::GetInstance();
    auto* collisions = CollisionManager::GetInstance();
    bullets->Clear();
    auto player = std::make_unique<Player>();
    player->Initialize(scene->GetObject3dCommon(), engine::InputManager::GetInstance(), scene->GetParticleSystem(), scene->GetSpriteCommon());
    player->SetName("__CannonDamageTarget"); player->SetClassName("Player"); player->SetEditorInternal(true);
    player->SetParameter(&engine::EntityParameter::hp, player->SetParameter(&engine::EntityParameter::maxHp, 20));
    player->SetScale({1, 1, 1});
    engine::Object3d::ColliderConfig collider; collider.type = ColliderType::kSphere; collider.center = {0, 0, 0}; collider.size = {0.5f, 0.5f, 0.5f};
    player->SetColliderConfig(collider);
    player->SetCollisionRadius(0.5f);
    if (player->GetCollider()) player->GetCollider()->SetScaleOverride({1, 1, 1});
    player->SetCollisionAttribute(CollisionCategories::Get("player"));
    player->SetCollisionMask(CollisionCategories::Get("enemy_attack"));
    player->SetDamageInvincible(invincible);
    auto cannon = std::make_unique<GimmickFireCannon>();
    cannon->Initialize(scene->GetObject3dCommon(), "Gimmicks/volcanic_bombard");
    cannon->SetTranslate({0, 1000, 0}); cannon->SetScale({1, 1, 1}); cannon->SetRotation({0, 0, 0});
    cannon->SetParameter(&engine::EntityParameter::actionMode, mode); cannon->SetParameter(&engine::EntityParameter::speed, 60); cannon->SetParameter(&engine::EntityParameter::interval, 0.08f);
    cannon->SetParameter(&engine::EntityParameter::moveAmount, radius); cannon->SetParameter(&engine::EntityParameter::healAmount, blast);
    cannon->SetParameter(&engine::EntityParameter::projectileLifetime, 3); cannon->SetParameter(&engine::EntityParameter::startActive, true);
    cannon->SetParameter(&engine::EntityParameter::editorPreviewFiring, preview);
    manager->SetIsPlaying(!preview);
    cannon->Update(0.2f);
    manager->SetIsPlaying(false);
    if (bullets->GetBullets().size() != 1) throw std::runtime_error("cannon did not fire one projectile");
    auto* projectile = bullets->GetBullets().front().get();
    const auto direction = engine::Math::Normalize(projectile->GetVelocity());
    player->SetTranslate(projectile->GetTranslate() + direction * 8.0f);
    player->UpdateWorldMatrix(true);
    collisions->AddObject(player.get());
    int damageEvents = 0;
    const auto subscription = EventManager::GetInstance()->Subscribe([&](const DamageEvent& event) {
        if (event.target == player.get()) ++damageEvents;
    });
    // 通常のフレーム間隔と低FPSの大きな変位で、途中の命中と一度だけの被弾を確認する。
    for (int i = 0; i < 20; ++i) bullets->Update(step);
    const float damage = 20 - player->GetHp();
    const float expected = preview || invincible ? 0.0f : 1.0f;
    const bool eventsValid = preview ? damageEvents == 0 : (invincible ? damageEvents <= 1 : damageEvents == 1);
    const bool passed = std::abs(damage - expected) < 0.001f && eventsValid && (preview || player->IsInvincible());
    log << (passed ? "OK" : "FAIL") << " mode=" << mode << " radius=" << radius << " blast=" << blast
        << " preview=" << preview << " invincible=" << invincible << " hpDamage=" << damage
        << " expected=" << expected << " events=" << damageEvents << " step=" << step << std::endl;
    EventManager::GetInstance()->Unsubscribe(subscription);
    collisions->RemoveObject(player.get());
    bullets->Clear();
    return passed;
}
inline bool RunPlayerProjectileCase(SceneManager* manager, std::ostream& log) {
    // 共通Bulletを使うプレイヤー側の弾も、敵に一度だけ命中することを確認する。
    auto* scene = manager->GetCurrentScene();
    auto* bullets = BulletManager::GetInstance();
    auto* collisions = CollisionManager::GetInstance();
    bullets->Clear();
    auto enemy = EnemyFactory::GetInstance()->CreateEnemy("Slime", scene->GetObject3dCommon());
    if (!enemy) throw std::runtime_error("regression enemy missing");
    enemy->SetParameter(&engine::EntityParameter::hp, enemy->SetParameter(&engine::EntityParameter::maxHp, 20));
    enemy->SetScale({1, 1, 1}); enemy->SetTranslate({0, 1000, 8});
    engine::Object3d::ColliderConfig collider; collider.type = ColliderType::kSphere; collider.size = {0.5f, 0.5f, 0.5f};
    enemy->SetColliderConfig(collider); enemy->SetCollisionRadius(0.5f);
    enemy->SetCollisionAttribute(CollisionCategories::Get("enemy"));
    enemy->SetCollisionMask(CollisionCategories::Get("player_attack"));
    enemy->UpdateWorldMatrix(true);
    collisions->AddObject(enemy.get());
    BulletVisualConfig visual;
    visual.collisionRadiusIsWorld = true;
    bullets->Fire({0, 1000, 0}, {0, 0, 60}, CollisionCategories::Get("player_attack"), CollisionCategories::Get("enemy"),
                  "Primitives/sphere", 0.3f, 3.0f, visual, 2.0f, {}, DamageType::Fire);
    int events = 0;
    const auto subscription = EventManager::GetInstance()->Subscribe([&](const DamageEvent& event) {
        if (event.target == enemy.get()) ++events;
    });
    bullets->Update(0.2f); bullets->Update(0.2f);
    const float damage = 20 - enemy->GetParameters()->hp;
    const bool ok = std::abs(damage - 2) < 0.001f && events == 1;
    log << (ok ? "OK" : "FAIL") << " player projectile hpDamage=" << damage << " events=" << events << std::endl;
    EventManager::GetInstance()->Unsubscribe(subscription);
    collisions->RemoveObject(enemy.get());
    bullets->Clear();
    return ok;
}
inline void AfterDraw(SceneManager* manager, const std::string& path) {
    // Sceneの読み込みが完了してから、一組ずつ生成・破棄して検証します。
    static std::ofstream log(path, std::ios::trunc);
    static int phase = 0, frame = 0;
    static bool done = false;
    if (done || !manager || manager->IsTransitioning()) return;
    if (phase == 0) { manager->ChangeScene("GAMEPLAY"); phase = 1; return; }
    if (manager->GetCurrentSceneName() != "GAMEPLAY") return;
    manager->SetIsPlaying(false);
    if (++frame < 3) return;
    try {
        bool ok = true;
        ok = RunCase(manager, 0, 1.0f, 0.5f, false, false, log) && ok;
        ok = RunCase(manager, 3, 3.0f, 0.5f, false, false, log) && ok;
        ok = RunCase(manager, 3, 0.6f, 3.0f, false, false, log) && ok;
        ok = RunCase(manager, 0, 1.0f, 0.5f, false, true, log) && ok;
        ok = RunCase(manager, 3, 3.0f, 0.5f, false, true, log) && ok;
        ok = RunCase(manager, 0, 1.0f, 0.5f, true, false, log) && ok;
        ok = RunCase(manager, 3, 3.0f, 0.5f, true, false, log) && ok;
        ok = RunCase(manager, 0, 1.0f, 0.5f, false, false, log, 1.0f / 60) && ok;
        ok = RunPlayerProjectileCase(manager, log) && ok;
        log << (ok ? "PASS:" : "FAIL:") << " cannon direct/bombard damage, large direct hit, invincibility and harmless preview" << std::endl;
    } catch (const std::exception& error) { log << "FAIL: " << error.what() << std::endl; }
    done = true;
    WinApp::CloseNow();
}
} // namespace CannonDamageRuntimeProbe
#endif
