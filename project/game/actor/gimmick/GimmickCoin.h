#pragma once
#include "BaseGimmick.h"

// プレイヤーが取得できるコインギミック
class GimmickCoin : public BaseGimmick {
public:
    virtual ~GimmickCoin() = default;

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
    std::unique_ptr<Object3d> Clone() const override;

private:
    void Collect();

private:
    bool isCollected_ = false;
    float rotationSpeed_ = 3.0f;
    float collectAnimationTimer_ = 0.0f;
    Vector3 collectStartPosition_{};
    Vector3 collectStartScale_{ 0.6f, 0.6f, 0.15f };
};
