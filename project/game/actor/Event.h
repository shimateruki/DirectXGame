#pragma once 
#include "engine/utility/math/Math.h"
class Object3d;
class Bullet;
/// <summary>
/// プレイヤーが何かに衝突したときに発行されるイベント
/// </summary>
struct PlayerHitEvent {
    Object3d* me = nullptr;        
    Object3d* hitObject = nullptr; // ぶつかった相手（罠、アイテムなど）
    Vector3 normal = { 0,0,0 };
};

struct BulletHitEvent {
    Object3d* hitObject; // 衝突した相手
    Bullet* bullet;      // 衝突した弾
};

// =========================================================
// ダメージイベント (Enemyへの攻撃時などに発行する)
// =========================================================
struct DamageEvent {
    Object3d* target = nullptr;   // ダメージを受ける人（今回は敵）
    Object3d* attacker = nullptr; // 攻撃した人（今回はプレイヤーの剣）
    float damageAmount = 0.0f;    // ダメージ量
};
enum class EventType {
    None = 0,       // 何もなし
    Damage,         // 触れるとダメージ
    Warp,
};