#pragma once
#include "Object3d.h"

/// <summary>
/// マップを構成するブロックアクター
/// ボスが吸収して操ることができる
/// </summary>
class MapBlock : public Object3d {
public:
    void Initialize(Object3dCommon* common) override;
    void Update(float deltaTime) override;

    /// <summary>
    /// ボスに吸収された時の処理
    /// </summary>
    void OnAbsorbed();

private:
    bool isAbsorbed_ = false;
};
