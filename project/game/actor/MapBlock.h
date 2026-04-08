#pragma once
#include "Object3d.h"
#include <vector>

/// <summary>
/// マップを構成するブロックアクター
/// ボスが吸収して操ることができる
/// </summary>
class MapBlock : public Object3d {
public:
    // ==========================================
    // 全MapBlockを管理する共有名簿！
    // ==========================================
    static std::vector<MapBlock*> s_activeBlocks;

    void Initialize(Object3dCommon* common) override;
    void Update(float deltaTime) override;
    ~MapBlock();

    /// <summary>
    /// ボスに吸収された時の処理
    /// </summary>
    void OnAbsorbed();

private:
    bool isAbsorbed_ = false;

    // ==========================================
    // 自分が密かに持っておくレーザー！
    // ==========================================
    std::unique_ptr<Object3d> laserBeam_;
};
