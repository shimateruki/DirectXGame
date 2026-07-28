#pragma once 
#include "engine/utility/math/Math.h"
#include <string>
class Object3d;
class Bullet;
/// <summary>
/// プレイヤーが何かに衝突したときに発行されるイベント
/// </summary>
// PlayerHitEventは、プレイヤーが何かに接触したときの通知データです。
struct PlayerHitEvent {
    Object3d* me = nullptr;        
    Object3d* hitObject = nullptr; // ぶつかった相手（罠、アイテムなど）
    Vector3 normal = { 0,0,0 };
};

// BulletHitEventは、弾がObject3dへ当たったときの通知データです。
struct BulletHitEvent {
    Object3d* hitObject; // 衝突した相手
    Bullet* bullet;      // 衝突した弾
};

// =========================================================
// ダメージイベント (Enemyへの攻撃時などに発行する)
// =========================================================
enum class StatusEffectType {
    None = 0,
    Burning,
};

// 攻撃の性質を表し、被弾演出やリアクションの選択に使います。
enum class DamageType {
    Physical = 0,
    Fire,
    Electric,
    Explosion,
};

// 攻撃から対象へ付与する状態異常の実行時データです。
struct StatusEffectApplication {
    StatusEffectType type = StatusEffectType::None;
    float duration = 0.0f;
    float tickInterval = 0.5f;
    float tickDamage = 0.0f;
    std::string vfxPreset;

    bool IsValid() const {
        return type != StatusEffectType::None && duration > 0.0f;
    }
};

// DamageEventは、ダメージ対象、攻撃者、ダメージ量、ノックバックをまとめます。
struct DamageEvent {
    Object3d* target = nullptr;   // ダメージを受ける人
    Object3d* attacker = nullptr; // 攻撃した人
    float damageAmount = 0.0f;    // ダメージ量
    Vector3 knockbackVelocity = { 0,0,0 }; // 追加：吹き飛ばしベクトル
    DamageType damageType = DamageType::Physical;
    StatusEffectApplication statusEffect;
};

struct PlayerDeathEvent {
    class Player* player;
};

struct PlayerJumpEvent {
    class Player* player = nullptr;
};

// EventTypeは、ゲーム内イベント通知の種類を表します。
enum class EventType {
    None = 0,       // なし
    Damage,         // ダメージ
    Warp,           // ワープ
    Movie_Bridge,   // 映像演出
    Checkpoint,     // 中間地点
    Goal,           // ゴール
    StageSelect,    // ステージセレクト
    StarCoin,       // スターコイン
};
