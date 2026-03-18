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
    /// ボスに吸収されるアニメーションを開始する
    /// </summary>
    /// <param name="target">吸収する側のオブジェクト（ボスなど）</param>
    void OnAbsorbed(Object3d* target);

private:
    bool isAbsorbed_ = false;       // 完全に吸収され、存在しない状態
    bool isAbsorbing_ = false;      // 吸収アニメーション中の状態
    Object3d* target_ = nullptr;    // 吸収先のターゲット
    float lerpTimer_ = 0.0f;        // アニメーション用タイマー
};
