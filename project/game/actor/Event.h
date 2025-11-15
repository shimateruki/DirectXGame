#pragma once 
#include "engine/utility/math/Math.h"
class Object3d;
class Bullet;
/// <summary>
/// プレイヤーが何かに衝突したときに発行されるイベント
/// </summary>
struct PlayerHitEvent {
    Object3d* hitObject = nullptr; // 衝突した相手のオブジェクト
    Vector3 normal = { 0,0,0 };
};

struct BulletHitEvent {
    Object3d* hitObject; // 衝突した相手
    Bullet* bullet;      // 衝突した弾
};