#pragma once
#include "engine/3d/Object3d.h"

// Object3dを継承したPlayerクラス
class Player : public Object3d {
public:
    /// <summary>
    /// 衝突時に呼び出される関数 (親の関数をオーバーライド)
    /// </summary>
    void OnCollision(Object3d* other) override;
};