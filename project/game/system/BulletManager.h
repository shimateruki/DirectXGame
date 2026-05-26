#pragma once
#include "Bullet.h" 
#include <list>
#include <memory>
#include <string>

// 前方宣言
class Object3dCommon;
class CollisionManager;

/// <summary>
/// 弾を管理する「エンジン側」のシステム
/// </summary>
class BulletManager {
public:
    static BulletManager* GetInstance();

    /// <summary>
    /// 初期化 (Object3dCommon と CollisionManager が必要)
    /// </summary>
    void Initialize(Object3dCommon* common, CollisionManager* colManager);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 全ての弾を更新 (移動、衝突による削除)
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// 全ての弾を描画
    /// </summary>
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    /// <summary>
    /// 弾を発射
    /// </summary>
    void Fire(const Vector3& pos, const Vector3& vel,
        uint32_t attr, uint32_t mask,
        const std::string& model = "sphere", float radius = 0.2f, float life = 120);

    const std::list<std::unique_ptr<Bullet>>& GetBullets() const { return bullets_; }

private:
    BulletManager() = default;
    ~BulletManager() = default;
    BulletManager(const BulletManager&) = delete;
    BulletManager& operator=(const BulletManager&) = delete;

    std::list<std::unique_ptr<Bullet>> bullets_;
    Object3dCommon* common_ = nullptr;
    CollisionManager* colManager_ = nullptr;
};