#pragma once

#include "Object3d.h"

/// 新しいItem実装を追加するための最小基底クラスです。
class BaseItem : public Object3d {
public:
    BaseItem() = default;
    ~BaseItem() override = default;

    virtual void Initialize(Object3dCommon* common, const std::string& modelName = {});
    /// 派生型は動的型を維持するため、自身のCloneを必ずoverrideしてください。
    std::unique_ptr<Object3d> Clone() const override;
};
