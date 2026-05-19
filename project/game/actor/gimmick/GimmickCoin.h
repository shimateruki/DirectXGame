#pragma once
#include "BaseGimmick.h"

// プレイヤーが収集可能なコインギミック
class GimmickCoin : public BaseGimmick {
public:
    virtual ~GimmickCoin() = default;

    // 初期化
    void Initialize(Object3dCommon* common, const std::string& modelName) override;

    // 更新処理
    void Update(float deltaTime) override;

    // 衝突判定
    bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;

private:
    void Collect(); // コイン獲得処理

private:
    bool isCollected_ = false;
    float rotationSpeed_ = 3.0f; // 回転速度
};
