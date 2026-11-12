#pragma once
#include "Object3D.h"
#include <string>

// 全てのギミックが共有する初期化、更新、衝突処理の基底クラス
// 関連：Object3dの配置・衝突基盤を使う。固有の反応は各ギミックが上書きする。
// 主な入口：Initialize / Update / OnCollision
class BaseGimmick : public engine::Object3D {
  public:
    virtual ~BaseGimmick() = default;

    // モデル名を指定してギミック本体を初期化する
    virtual void Initialize(engine::Object3DCommon* common, const std::string& modelName);

    // ギミック共通の更新処理
    virtual void Update(float deltaTime) override;

    // ギミック共通の衝突処理
    virtual bool OnCollision(engine::Object3D* other) override;

    // 派生型を基底型として複製しない。各派生クラスが自分の複製を担当する。
    std::unique_ptr<engine::Object3D> Clone() const override;
};
