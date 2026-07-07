#pragma once
#include "BaseGimmick.h"

// 上から踏むと大ジャンプするジャンプ台ギミック
class GimmickTrampoline : public BaseGimmick {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
};
