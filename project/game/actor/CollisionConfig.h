#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "engine/utility/math/Math.h"

/// <summary>JSONで定義する衝突カテゴリ。idは保存用の不変キー、bitは実行時のビット値です。</summary>
struct CollisionCategoryDefinition {
    std::string id;
    std::string displayName;
    uint32_t bit = 0;
};

namespace CollisionCategories {
/// <summary>カテゴリIDに対応するビットを取得する。未登録の場合は0を返します。</summary>
uint32_t Get(std::string_view id);
/// <summary>Inspectorに表示するカテゴリ定義のコピーを返します。</summary>
std::vector<CollisionCategoryDefinition> GetDefinitions();
/// <summary>定義JSONが正常に読み込まれたか返します。</summary>
bool IsDefinitionLoaded();
/// <summary>未使用ビットでカテゴリを追加し、定義JSONへ保存します。</summary>
bool Add(const std::string& id, const std::string& displayName, std::string* error = nullptr);
/// <summary>旧シーンの数値マスクを、現在のカテゴリ配置へ変換します。</summary>
uint32_t FromLegacyMask(uint32_t legacyMask);
/// <summary>現在の数値マスクに含まれるカテゴリIDを返します。</summary>
std::vector<std::string> GetIds(uint32_t mask);
/// <summary>保存されたカテゴリIDを実行時マスクに変換します。</summary>
uint32_t FromIds(const std::vector<std::string>& ids);
/// <summary>定義済みカテゴリが占有するビットをまとめて返します。</summary>
uint32_t GetKnownMask();
/// <summary>従来の0～7番ビットを対象にしていた配置物の既定Maskを再現します。</summary>
uint32_t GetDefaultGameplayMask();
} // namespace CollisionCategories

/// <summary>
/// コライダー（あたり判定）の形状タイプ
/// </summary>
// ColliderTypeは、Colliderが使う形状の種類を表します。
enum class ColliderType {
    kNone,   // 当たり判定なし
    kSphere, // 球
    kAABB,   // AABB（回転しない箱）
    kOBB,
    kCylinder,
    kRing,   // リング（ドーナツ型・衝撃波用）
    kTerrain // 高さ付き地形
};

// AABB構造体
// AABBは、回転しない箱型の衝突形状です。
struct AABB {
    engine::Vector3 min; // 箱の最小座標
    engine::Vector3 max; // 箱の最大座標
};

// OBB構造体 (中心、各軸の向き、サイズ)
// OBBは、回転を持つ箱型の衝突形状です。
struct OBB {
    engine::Vector3 center;          // 中心点
    engine::Vector3 orientations[3]; // 座標軸 (正規化された X, Y, Z 軸)
    engine::Vector3 size;            // 中心からの半サイズ (width/2, height/2, depth/2)
};

// リング構造体 (中心、向き、内径、外径)
struct Ring {
    engine::Vector3 center;
    engine::Vector3 normal = {0, 1, 0}; // リングが乗っている面の法線
    float innerRadius;
    float outerRadius;
    float height; // 厚み (中心から上下に height/2)
};

// 円柱構造体
struct Cylinder {
    engine::Vector3 center;
    engine::Vector3 axis = {0, 1, 0}; // 軸方向 (正規化)
    float radius;
    float height; // 全長 (中心から上下に height/2)
};

// 衝突情報（結果）を格納する構造体
// CollisionInfoは、衝突有無、法線、めり込み量など判定結果をまとめます。
struct CollisionInfo {
    bool isColliding = false;   // 衝突しているか
    engine::Vector3 normal = {0, 0, 0}; // 衝突法線 (押し戻す方向)
    float penetration = 0.0f;   // めり込み量
};
/// <summary>
/// 衝突面（法線）がどの方向を向いているかを示す
/// </summary>
enum class CollisionFace {
    kTop,    // 上面 (Y+)
    kBottom, // 底面 (Y-)
    kRight,  // 右面 (X+)
    kLeft,   // 左面 (X-)
    kFront,  // 正面 (Z+)
    kBack,   // 背面 (Z-)
    kOther   // 斜め
};

/// <summary>
/// 衝突法線ベクトルから、最も近い衝突面 (CollisionFace) を判定する
/// </summary>
/// <param name="normal">衝突法線 (正規化されていること)</param>
/// <param name="threshold">「斜め」と判断する閾値</param>
/// <returns>CollisionFace</returns>
CollisionFace GetCollisionFace(const engine::Vector3& normal, float threshold = 0.8f);

// --- 衝突判定ヘルパー関数群 (宣言) ---
CollisionInfo CheckAABBCollision(const AABB& a, const AABB& b);
CollisionInfo CheckSphereCollision(const engine::Vector3& posA, float rA, const engine::Vector3& posB, float rB);
CollisionInfo CheckSphereAABBCollision(const engine::Vector3& spherePos, float sphereRadius, const AABB& aabb);
CollisionInfo CheckSphereOBBCollision(const engine::Vector3& spherePos, float sphereRadius, const OBB& obb);
CollisionInfo CheckOBBCollision(const OBB& obb1, const OBB& obb2);
/// <summary>
/// AABBとOBBの衝突判定 (AABBをOBBに変換して判定)
/// </summary>
CollisionInfo CheckAABBOBBCollision(const AABB& a, const OBB& b);

// --- リング関連の衝突判定 ---
CollisionInfo CheckRingSphereCollision(const Ring& ring, const engine::Vector3& spherePos, float sphereRadius);
CollisionInfo CheckRingOBBCollision(const Ring& ring, const OBB& obb);

// --- 円柱関連の衝突判定 ---
CollisionInfo CheckCylinderSphereCollision(const Cylinder& cyl, const engine::Vector3& spherePos, float sphereRadius);
CollisionInfo CheckCylinderOBBCollision(const Cylinder& cyl, const OBB& obb);
