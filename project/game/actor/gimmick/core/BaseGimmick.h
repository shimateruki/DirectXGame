#pragma once
#include "Object3d.h"
#include <string>

// 全てのギミックが共有する初期化、更新、衝突処理の基底クラス
class BaseGimmick : public Object3d {
public:
    virtual ~BaseGimmick() = default;

    // モデル名を指定してギミック本体を初期化する
    virtual void Initialize(Object3dCommon* common, const std::string& modelName);

    // ギミック共通の更新処理
    virtual void Update(float deltaTime) override;

    // ギミック共通の衝突処理
    virtual bool OnCollision(Object3d* other) override;

    std::unique_ptr<Object3d> Clone() const override;
};
