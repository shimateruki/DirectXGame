#pragma once
#include "Object3d.h"
#include <vector>
#include <list>
#include <unordered_map>
#include <cstdint>  
#include <unordered_set>
#include <functional>

struct Vector3i {
    int x, y, z;
};

// 2つのポインタのペアをハッシュ化するためのカスタムハッシュ
struct PairHash {
    std::size_t operator()(const std::pair<Object3d*, Object3d*>& p) const {
        // 簡易的なハッシュミックス
        auto hashA = std::hash<Object3d*>{}(p.first);
        auto hashB = std::hash<Object3d*>{}(p.second);
        return hashA ^ (hashB << 1);
    }
};

/// <summary>
/// レイキャストの結果を格納する構造体
/// </summary>
struct RaycastHit {
    bool isHit = false;            // 衝突したか
    Object3d* hitObject = nullptr; // 衝突したオブジェクト
    Vector3 hitPoint;              // 衝突した座標
    float distance = 0.0f;         // 衝突点までの距離
    Vector3 normal;                // 衝突点の法線
};

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
    /// (グリッド分割を使用して効率化)
    /// </summary>
    void Update();

    void RemoveObject(Object3d* object);

    /// <summary>
    /// 衝突判定リストにオブジェクトを追加
    /// </summary>
    void AddObject(Object3d* object);

    /// <summary>
    /// 衝突判定リストから全てのオブジェクトをクリア
    /// </summary>
    void ClearObjects();

    /// <summary>
    /// （デバッグ用）グリッドの1辺のサイズを設定
    /// </summary>
    void SetGridSize(float size) { gridSize_ = size; }


    /// <summary>
    /// レイ（光線）を飛ばし、指定したマスクのオブジェクトと衝突するか判定する
    /// </summary>
    /// <param name="start">レイの開始地点</param>
    /// <param name="direction">レイの方向（正規化されている必要あり）</param>
    /// <param name="maxDistance">レイの最大距離</param>
    /// <param name="mask">衝突対象とする属性マスク (例: kGround)</param>
    /// <returns>衝突情報 (衝突しなかった場合は hit.isHit = false)</returns>
    RaycastHit Raycast(const Vector3& start, const Vector3& direction,
        float maxDistance, uint32_t mask = 0xFFFFFFFF);
    RaycastHit RaycastFiltered(const Vector3& start, const Vector3& direction,
        float maxDistance, uint32_t mask, const std::function<bool(Object3d*)>& shouldIgnore);
	/// <summary>
	/// 登録されている全オブジェクトの取得 (デバッグ用)
    /// 
    const std::list<Object3d*>& GetObjects() const { return objects_; }

private:
    CollisionManager() = default;
    ~CollisionManager() = default;
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

    /// ★ グリッドの1辺のサイズ (シーンの広さに合わせて調整)
    float gridSize_ = 5.0f;

    /// ★ 3D座標からグリッドID（ハッシュ値）を計算する
    int64_t GetGridID(const Vector3& pos);

    Vector3i GetGridIndices(const Vector3& pos);



    /// ★ (x, y, z) のインデックスからグリッドIDを計算する
    int64_t GetGridIDFromIndices(int x, int y, int z);

    /// ★ 衝突判定のペアを実行するヘルパー関数
    void CheckCollisionPair(Object3d* objA, Object3d* objB);


    /// <summary>
    /// 静的オブジェクトを staticGrid_ に登録する (内部関数)
    /// </summary>
    void BuildStaticGrid();

    // ★ 静的オブジェクト（地面、壁など）専用のグリッド
    std::unordered_map<int64_t, std::list<Object3d*>> staticGrid_;

    // ★ 静的グリッドを再構築する必要があるか
    bool needsStaticGridRebuild_ = true;


private:
    // 衝突判定を取りたいオブジェクトのリスト
    std::list<Object3d*> objects_;

    // ★ グリッド本体。キーはグリッドID、値はそのセル内のオブジェクトリスト
    std::unordered_map<int64_t, std::list<Object3d*>> grid_;

    // ★ 判定済みのペアを記録するためのセット
    std::unordered_set<std::pair<Object3d*, Object3d*>, PairHash> checkedPairs_;
};
