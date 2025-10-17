#pragma once
#include "engine/3d/Object3d.h"
#include <vector>
#include <list>

/// <summary>
/// 衝突判定を管理するクラス
/// </summary>
class CollisionManager {
public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static CollisionManager* GetInstance();

    /// <summary>
    /// 更新処理。毎フレーム、全オブジェクトの衝突をチェックする
    /// </summary>
    void Update();

    /// <summary>
    /// 衝突判定リストにオブジェクトを追加
    /// </summary>
    void AddObject(Object3d* object);

    /// <summary>
    /// 衝突判定リストから全てのオブジェクトをクリア
    /// </summary>
    void ClearObjects();

private:
    CollisionManager() = default;
    ~CollisionManager() = default;
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

private:
    // 衝突判定を取りたいオブジェクトのリスト
    std::list<Object3d*> objects_;
};