#define NOMINMAX
#include "MapBlock.h"
#include "CollisionConfig.h"
#include <cmath>
#include <algorithm>

void MapBlock::Initialize(Object3dCommon* common) {
    Object3d::Initialize(common);
    
    // マップブロックとしての属性を設定
    SetCollisionAttribute(kMapBlock);
    // 地形（Ground）としても機能させたい場合は以下のようにビットORをとる
    // SetCollisionAttribute(kMapBlock | kGround);
    
    // デフォルトでは押し出し対象にする
    SetCollisionMask(kPlayer | kEnemy); 
    
    SetClassName("MapBlock");
}

void MapBlock::Update(float deltaTime) {
    if (isAbsorbed_) return;

    if (isAbsorbing_ && target_) {
        // 吸収アニメーション：ターゲット（ボス）に向かって移動する
        Vector3 currentPos = GetTranslate();
        Vector3 targetPos = target_->GetTranslate();
        
        // 単純な線形補間または速度を持った移動
        // ここでは1秒で到達するイメージで実装
        float speed = 20.0f; // 1秒間に20m移動
        Vector3 direction = {
            targetPos.x - currentPos.x,
            targetPos.y - currentPos.y,
            targetPos.z - currentPos.z
        };
        
        float distance = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        
        if (distance < 1.0f) {
            // 十分に近づいたら吸収完了
            isAbsorbing_ = false;
            isAbsorbed_ = true;
            SetIsVisible(false);
        } else {
            // ターゲットに向かって移動
            direction.x /= distance;
            direction.y /= distance;
            direction.z /= distance;
            
            Vector3 newPos = {
                currentPos.x + direction.x * speed * deltaTime,
                currentPos.y + direction.y * speed * deltaTime,
                currentPos.z + direction.z * speed * deltaTime
            };
            SetTranslate(newPos);

            // スケールを小さくしていく演出
            Vector3 currentScale = GetScale();
            float shrinkSpeed = 1.0f;
            Vector3 newScale = {
                std::max(0.1f, currentScale.x - shrinkSpeed * deltaTime),
                std::max(0.1f, currentScale.y - shrinkSpeed * deltaTime),
                std::max(0.1f, currentScale.z - shrinkSpeed * deltaTime)
            };
            SetScale(newScale);
        }
    }

    Object3d::Update(deltaTime);
}

void MapBlock::OnAbsorbed(Object3d* target) {
    if (isAbsorbing_ || isAbsorbed_) return;

    isAbsorbing_ = true;
    target_ = target;
    
    // 吸収開始時に当たり判定を消す（プレイヤーを飛ばしたりしないため）
    SetCollisionAttribute(0);
}
