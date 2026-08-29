#pragma once

#include "Object3d.h"

/// 新しいGimmick実装を追加するための最小基底クラスです。
class BaseGimmick : public Object3d {
public:
    BaseGimmick() = default;
    ~BaseGimmick() override = default;

    virtual void Initialize(Object3dCommon* common, const std::string& modelName = {});
    /// 派生型は動的型を維持するため、自身のCloneを必ずoverrideしてください。
    std::unique_ptr<Object3d> Clone() const override;
};
