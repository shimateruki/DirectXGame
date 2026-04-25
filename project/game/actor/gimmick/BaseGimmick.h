#pragma once
#include "Object3d.h"
#include <string>

// 全てのギミックの共通ルールを決めるクラス
class BaseGimmick : public Object3d {
public:
    virtual ~BaseGimmick() = default;

    // 初期化（モデル名を引数で受け取る）
    virtual void Initialize(Object3dCommon* common, const std::string& modelName);

    // 更新処理
    virtual void Update(float deltaTime) override;

    // 衝突判定
    virtual bool OnCollision(Object3d* other) override;
};
