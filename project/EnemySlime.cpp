#include "EnemySlime.h"
#include "engine/utility/math/Math.h" // Vector3の計算用
#include <cmath> // atan2用

void EnemySlime::Update(float deltaTime) {
    // [削除] ここで呼んでいた BaseEnemy::Update(deltaTime); を消す

    // 1. 先にAIロジックで「どう動くか（velocity_）」を決める
    if (target_) {
        Vector3 myPos = transform_.translate;
        Vector3 targetPos = target_->GetWorldPosition();
        Vector3 toTarget = targetPos - myPos;
        toTarget.y = 0.0f;

        static Math math;
        float length = math.Length(toTarget);

        if (length < 20.0f && length > 1.0f) {
            Vector3 dir = math.Normalize(toTarget);
            float speed = 3.0f;

            velocity_.x = dir.x * speed;
            velocity_.z = dir.z * speed;

            transform_.rotate.y = std::atan2(dir.x, dir.z);
        } else {
            velocity_.x = 0.0f;
            velocity_.z = 0.0f;
        }
    }

    // 2. 最後に親クラスを呼んで、決定した速度で移動させる！
    BaseEnemy::Update(deltaTime); 
}

std::unique_ptr<Object3d> EnemySlime::Clone() const {
    auto newSlime = std::make_unique<EnemySlime>();
    // 初期化
    newSlime->Initialize(common_, modelName_);
    // 2. 親クラス(Object3d)の機能を使って、座標やモデル設定をコピーしてもらう
    newSlime->CopyFrom(this);
    newSlime->SetTarget(this->target_);
    return newSlime;
}