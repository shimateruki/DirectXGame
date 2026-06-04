#pragma once
#include "Object3d.h"
#include <memory>
#include <string>

class Player;

// プレイヤーが拾って効果を発動するアイテムの基底クラス
class BaseItem : public Object3d {
public:
    virtual ~BaseItem() = default;

    virtual void Initialize(Object3dCommon* common, const std::string& modelName);
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;

protected:
    virtual void Collect(Player* player);
    void MarkCollected();

    bool isCollected_ = false;
    float rotationSpeed_ = 2.4f;
};
