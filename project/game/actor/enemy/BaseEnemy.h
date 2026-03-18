#pragma once
#include "Character.h"

// 全ての敵の共通ルールを決めるクラス
class BaseEnemy : public Character {
public:
    virtual ~BaseEnemy() = default;

    // 初期化（モデル名を引数で受け取るように少し改良します）
    virtual void Initialize(Object3dCommon* common, const std::string& modelName);

    // 更新処理
    virtual void Update(float deltaTime) override;

    // 衝突判定
    virtual bool OnCollision(Object3d* other) override;

    // プレイヤーを追いかけるためにターゲットを登録する関数
    void SetTarget(Object3d* target) { target_ = target; }

protected:
    Object3d* target_ = nullptr; // 追いかける対象（プレイヤー）
    float damageCooldownTimer_ = 0.0f; // 連続ヒットを防ぐ無敵タイマー
    float colorResetTimer_ = 0.0f;     // 赤色から元に戻すためのタイマー
    Vector4 defaultColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // 元の色を記憶
};