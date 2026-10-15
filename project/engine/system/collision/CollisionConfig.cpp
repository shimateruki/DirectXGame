#include "CollisionConfig.h"
#include "engine/utility/math/Math.h"
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <Windows.h>

namespace {
constexpr const char* kCategoryPath = "Resources/json/collision/categories.json";
// 古いシーンの数値マスクを読むための対応表。新しいカテゴリ定義には使いません。
constexpr std::array<std::pair<const char*, unsigned>, 9> kLegacyCategories = {{
    {"player", 0}, {"enemy", 1}, {"ground", 2}, {"player_bullet", 3},
    {"trigger", 4}, {"map_block", 5}, {"player_attack", 6},
    {"enemy_attack", 7}, {"hook_anchor", 8},
}};

/// <summary>衝突カテゴリの定義とIDからビット値への検索表を保持する。</summary>
struct CategoryState {
    /// <summary>カテゴリIDを一時文字列なしで検索するためのハッシュ関数。</summary>
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };
    std::vector<CollisionCategoryDefinition> definitions;
    std::unordered_map<std::string, uint32_t, StringHash, std::equal_to<>> bitsById;
    bool loadedFromJson = false;
};

// カテゴリ一覧からIDと衝突ビットの検索表を作り直します。
void RebuildLookup(CategoryState& state) {
    state.bitsById.clear();
    for (const auto& definition : state.definitions) {
        state.bitsById.emplace(definition.id, definition.bit);
    }
}

bool IsValidId(const std::string& id) {
    // カテゴリ ID は小文字で始め、以降も英小文字・数字・下線に限定する。
    if (id.empty() || !std::islower(static_cast<unsigned char>(id[0]))) return false;
    return std::all_of(id.begin() + 1, id.end(), [](unsigned char ch) {
        return std::islower(ch) || std::isdigit(ch) || ch == '_';
    });
}

// JSONの形式・ID・ビット重複を検証して衝突カテゴリを読み込みます。
bool LoadCategories(CategoryState& state) {
    // IDとビット位置の重複を拒否し、壊れた定義で衝突Maskを組み替えない。
    std::ifstream file(kCategoryPath, std::ios::binary);
    if (!file) return false;
    try {
        const auto root = nlohmann::json::parse(file);
        if (!root.is_object() || root.value("version", 0) != 1 ||
            !root.contains("categories") || !root["categories"].is_array()) return false;

        std::vector<CollisionCategoryDefinition> parsed;
        std::unordered_set<std::string> ids;
        uint32_t usedBits = 0;
        for (const auto& item : root["categories"]) {
            if (!item.is_object() || !item.contains("id") || !item["id"].is_string() ||
                !item.contains("displayName") || !item["displayName"].is_string() ||
                !item.contains("bitIndex") || !item["bitIndex"].is_number_integer()) return false;
            const std::string id = item["id"].get<std::string>();
            const std::string name = item["displayName"].get<std::string>();
            const int index = item["bitIndex"].get<int>();
            if (!IsValidId(id) || name.empty() || index < 0 || index >= 32 || !ids.insert(id).second) return false;
            const uint32_t bit = uint32_t{1} << index;
            if ((usedBits & bit) != 0) return false;
            usedBits |= bit;
            parsed.push_back({id, name, bit});
        }
        for (const auto& [id, ignoredIndex] : kLegacyCategories) {
            // 旧Maskとの互換変換に必要なカテゴリは削除を許さない。
            if (ids.count(id) == 0) return false;
        }
        state.definitions = std::move(parsed);
        RebuildLookup(state);
        state.loadedFromJson = true;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

CategoryState& Categories() {
    static CategoryState state = [] {
        CategoryState loaded;
        if (!LoadCategories(loaded)) {
            // 配布時にJSONが欠けても既存の判定だけは維持し、追加操作は失敗として知らせます。
            for (const auto& [id, index] : kLegacyCategories) {
                loaded.definitions.push_back({id, id, uint32_t{1} << index});
            }
            RebuildLookup(loaded);
        }
        return loaded;
    }();
    return state;
}

bool SaveCategories(const std::vector<CollisionCategoryDefinition>& definitions) {
    // 一時ファイルを完成させてから置き換え、書き込み途中のJSONを残さない。
    namespace fs = std::filesystem;
    const fs::path path(kCategoryPath);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;
    nlohmann::json root;
    root["version"] = 1;
    root["categories"] = nlohmann::json::array();
    for (const auto& definition : definitions) {
        unsigned index = 0;
        while ((definition.bit >> index) != 1u) ++index;
        root["categories"].push_back({
            {"id", definition.id}, {"displayName", definition.displayName}, {"bitIndex", index},
        });
    }
    fs::path temporary = path;
    temporary += L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file << root.dump(2) << '\n';
        if (!file.good()) return false;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}
} // namespace

uint32_t CollisionCategories::Get(std::string_view id) {
    const auto& lookup = Categories().bitsById;
    const auto it = lookup.find(id);
    return it == lookup.end() ? 0 : it->second;
}

std::vector<CollisionCategoryDefinition> CollisionCategories::GetDefinitions() {
    return Categories().definitions;
}

bool CollisionCategories::IsDefinitionLoaded() {
    return Categories().loadedFromJson;
}

bool CollisionCategories::Add(const std::string& id, const std::string& displayName, std::string* error) {
    // 使用中ビットを飛ばして最初の空きを割り当て、保存成功後にメモリ上の索引を更新する。
    auto fail = [error](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (!IsValidId(id) || displayName.empty()) return fail("IDは英小文字で始まる英小文字・数字・_、表示名は空欄不可です。");
    auto& definitions = Categories().definitions;
    if (!Categories().loadedFromJson) return fail("カテゴリ定義JSONを読み込めません。ファイルを確認してください。");
    if (Get(id) != 0) return fail("同じIDのカテゴリが既に存在します。");
    uint32_t used = 0;
    for (const auto& definition : definitions) used |= definition.bit;
    uint32_t freeBit = 0;
    for (unsigned index = 0; index < 32; ++index) {
        const uint32_t candidate = uint32_t{1} << index;
        if ((used & candidate) == 0) { freeBit = candidate; break; }
    }
    if (freeBit == 0) return fail("32個の衝突カテゴリを使い切っています。");
    auto updated = definitions;
    updated.push_back({id, displayName, freeBit});
    if (!SaveCategories(updated)) return fail("カテゴリ定義JSONへの保存に失敗しました。");
    definitions = std::move(updated);
    RebuildLookup(Categories());
    if (error) error->clear();
    return true;
}

uint32_t CollisionCategories::FromLegacyMask(uint32_t legacyMask) {
    // 旧カテゴリの下位ビットだけを現在の ID 定義へ置き換え、拡張ビットは保持する。
    uint32_t resolved = legacyMask & ~((uint32_t{1} << 9) - 1);
    for (const auto& [id, index] : kLegacyCategories) {
        if ((legacyMask & (uint32_t{1} << index)) != 0) resolved |= Get(id);
    }
    return resolved;
}

std::vector<std::string> CollisionCategories::GetIds(uint32_t mask) {
    // 現在登録されているカテゴリだけを ID 一覧へ戻す。
    std::vector<std::string> ids;
    for (const auto& definition : Categories().definitions) {
        if ((mask & definition.bit) != 0) ids.push_back(definition.id);
    }
    return ids;
}

uint32_t CollisionCategories::FromIds(const std::vector<std::string>& ids) {
    uint32_t mask = 0;
    for (const auto& id : ids) mask |= Get(id);
    return mask;
}

uint32_t CollisionCategories::GetKnownMask() {
    uint32_t mask = 0;
    for (const auto& definition : Categories().definitions) mask |= definition.bit;
    return mask;
}

uint32_t CollisionCategories::GetDefaultGameplayMask() {
    // 旧データと同じ基本カテゴリだけを初期のゲーム用マスクへ含める。
    uint32_t mask = 0;
    for (const auto& [id, index] : kLegacyCategories) {
        if (index < 8) mask |= Get(id);
    }
    return mask;
}

// [1] AABB vs AABB 判定
CollisionInfo CheckAABBCollision(const AABB& a, const AABB& b) {
    CollisionInfo info;
    info.isColliding = false; // 初期化

    // 各軸の中心座標
    engine::Vector3 centerA = (a.min + a.max) * 0.5f;
    engine::Vector3 centerB = (b.min + b.max) * 0.5f;

    // 各軸の(半分の)サイズ
    engine::Vector3 sizeA = (a.max - a.min) * 0.5f;
    engine::Vector3 sizeB = (b.max - b.min) * 0.5f; // 正しい計算

    // 中心間の距離ベクトル
    engine::Vector3 distanceVec = centerA - centerB;

    // 各軸でのめり込み量を計算
    // (サイズの合計) - (中心間距離の絶対値) が正なら重なっている
    float overlapX = (sizeA.x + sizeB.x) - std::abs(distanceVec.x);
    float overlapY = (sizeA.y + sizeB.y) - std::abs(distanceVec.y);
    float overlapZ = (sizeA.z + sizeB.z) - std::abs(distanceVec.z);

    // 1つでも重なっていない軸があれば衝突していない
    if (overlapX < 0.0f || overlapY < 0.0f || overlapZ < 0.0f) {
        return info;
    }

    // 衝突している
    info.isColliding = true;

    // めり込み量が最小の軸を特定 (これが衝突した軸)
    if (overlapY < overlapX && overlapY < overlapZ) {
        // --- Y軸 (上下) が最小 ---
        info.penetration = overlapY;
        // A(Player)がB(Block)より上なら、上(+Y)方向の法線
        info.normal = (distanceVec.y > 0.0f) ? engine::Vector3{ 0, 1, 0 } : engine::Vector3{ 0, -1, 0 };
    } else if (overlapX < overlapZ) {
        // --- X軸 (左右) が最小 ---
        info.penetration = overlapX;
        // A(Player)がB(Block)より右(+X)なら、右(+X)方向の法線
        info.normal = (distanceVec.x > 0.0f) ? engine::Vector3{ 1, 0, 0 } : engine::Vector3{ -1, 0, 0 };
    } else {
        // --- Z軸 (前後) が最小 ---
        info.penetration = overlapZ;
        // A(Player)がB(Block)より手前(+Z)なら、手前(+Z)方向の法線
        info.normal = (distanceVec.z > 0.0f) ? engine::Vector3{ 0, 0, 1 } : engine::Vector3{ 0, 0, -1 };
    }

    return info;
}


// [2] Sphere vs Sphere 判定 
CollisionInfo CheckSphereCollision(
    const engine::Vector3& posA, float rA, const engine::Vector3& posB, float rB) {
    // (以前のコードのまま)
    CollisionInfo info;
    engine::Math math;
    engine::Vector3 vecAtoB = posB - posA;
    float distance = math.Length(vecAtoB);
    float totalRadius = rA + rB;
    float penetration = totalRadius - distance;

    if (penetration > 0) {
        info.isColliding = true;
        info.penetration = penetration;
        if (distance > 0.001f) {
            info.normal = (vecAtoB / distance) * -1.0f;
        } else {
            info.normal = { 1.0f, 0, 0 };
        }
    } else {
        info.isColliding = false;
    }
    return info;
}

// [3] Sphere vs AABB 判定 
CollisionInfo CheckSphereAABBCollision(
    const engine::Vector3& spherePos, float sphereRadius, const AABB& aabb) {
    // (以前のコードのまま)
    CollisionInfo info;
    engine::Math math;

    engine::Vector3 closestPoint = {
        math.Clamp(spherePos.x, aabb.min.x, aabb.max.x),
        math.Clamp(spherePos.y, aabb.min.y, aabb.max.y),
        math.Clamp(spherePos.z, aabb.min.z, aabb.max.z)
    };
    const engine::Vector3 difference = spherePos - closestPoint;
    const float distanceSq = math.Dot(difference, difference);

    if (distanceSq < (sphereRadius * sphereRadius)) {
        info.isColliding = true;
        if (distanceSq > 0.001f) {
            float distance = std::sqrt(distanceSq); // sqrtが必要
            info.penetration = sphereRadius - distance;
            info.normal = (spherePos - closestPoint) / distance;
        } else {
            engine::Vector3 aabbCenter = (aabb.min + aabb.max) * 0.5f;
            engine::Vector3 vecToCenter = aabbCenter - spherePos;
            if (math.Length(vecToCenter) < 0.001f) {
                info.normal = { 1, 0, 0 };
            } else {
                info.normal = math.Normalize(vecToCenter) * -1.0f;
            }
            info.penetration = sphereRadius;
        }
    } else {
        info.isColliding = false;
    }
    return info;
}


CollisionFace GetCollisionFace(const engine::Vector3& normal, float threshold) {
    // Y軸 (上下) の判定を優先
    if (normal.y > threshold) { return CollisionFace::kTop; }
    if (normal.y < -threshold) { return CollisionFace::kBottom; }

    // X軸 (左右) の判定
    if (normal.x > threshold) { return CollisionFace::kRight; }
    if (normal.x < -threshold) { return CollisionFace::kLeft; }

    // Z軸 (前後) の判定
    if (normal.z > threshold) { return CollisionFace::kFront; }
    if (normal.z < -threshold) { return CollisionFace::kBack; }

    // どの軸にも強く当たっていない場合は「斜め」
    return CollisionFace::kOther;
}

// ------------------------------------------------------------
//  Sphere vs OBB
// ------------------------------------------------------------
CollisionInfo CheckSphereOBBCollision(const engine::Vector3& spherePos, float sphereRadius, const OBB& obb) {
    CollisionInfo info = { false, {0,0,0}, 0.0f };

    // 球の中心をOBBのローカル空間に変換するイメージで、
    // OBB上の「球に一番近い点」を探す
    engine::Vector3 centerToSphere = spherePos - obb.center;
    engine::Vector3 closestPoint = obb.center;

    engine::Math math; // Mathクラスのインスタンス (Dot計算用)

    // 各軸(X, Y, Z)について投影してクランプ
    for (int i = 0; i < 3; ++i) {
        // 距離を軸に投影
        float dist = math.Dot(centerToSphere, obb.orientations[i]);

        // OBBのサイズ(半サイズ)で制限
        float clamped = std::clamp(dist, -obb.size.x, obb.size.x);
        // ※ size.x, y, z を配列的にアクセスできない場合は以下のように分岐するか、sizeを配列にする
        if (i == 1) clamped = std::clamp(dist, -obb.size.y, obb.size.y);
        if (i == 2) clamped = std::clamp(dist, -obb.size.z, obb.size.z);

        // 最短点を加算
        closestPoint += obb.orientations[i] * clamped;
    }

    // 最短点と球の中心との距離をチェック
    engine::Vector3 diff = spherePos - closestPoint;
    float distanceSq = math.Dot(diff, diff);

    if (distanceSq <= (sphereRadius * sphereRadius)) {
        info.isColliding = true;

        float distance = std::sqrt(distanceSq);

        // めり込み量と法線
        if (distance > 0.0001f) {
            info.penetration = sphereRadius - distance;
            info.normal = math.Normalize(diff);
        } else {
            // 中心が完全に埋まっている場合
            info.penetration = sphereRadius;
            info.normal = obb.orientations[1]; // とりあえずY軸などで代用
        }
    }

    return info;
}

// -----------------------------------------------------------------
// OBB vs OBB（改良版） - 標準的な回転行列 + absR を使った SAT 実装
// -----------------------------------------------------------------
CollisionInfo CheckOBBCollision(const OBB& a, const OBB& b) {
    CollisionInfo info = { false, {0,0,0}, 0.0f };
    engine::Math math;

    const float aH[3] = { a.size.x, a.size.y, a.size.z };
    const float bH[3] = { b.size.x, b.size.y, b.size.z };

    float R[3][3];
    float absR[3][3];
    // ★ EPS は削除（外積の計算時にゼロ除算回避が入っているため不要）

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R[i][j] = math.Dot(a.orientations[i], b.orientations[j]);
            // ★ EPSを足さない純粋な絶対値にする（床が膨張するバグの修正）
            absR[i][j] = std::fabs(R[i][j]);
        }
    }

    engine::Vector3 tVec = b.center - a.center;
    float tA[3] = {
        math.Dot(tVec, a.orientations[0]),
        math.Dot(tVec, a.orientations[1]),
        math.Dot(tVec, a.orientations[2])
    };

    float minOverlap = std::numeric_limits<float>::infinity();
    engine::Vector3 minAxis = { 0,0,0 };

    // --- 1) Aの3軸をテスト ---
    for (int i = 0; i < 3; ++i) {
        float ra = aH[i];
        float rb = bH[0] * absR[i][0] + bH[1] * absR[i][1] + bH[2] * absR[i][2];
        float dist = std::fabs(tA[i]);
        float overlap = (ra + rb) - dist;
        if (overlap <= 0.0f) return info;
        if (overlap < minOverlap) {
            minOverlap = overlap;
            minAxis = a.orientations[i];
            if (math.Dot(a.center - b.center, minAxis) < 0.0f) minAxis = minAxis * -1.0f;
        }
    }

    // --- 2) Bの3軸をテスト ---
    for (int j = 0; j < 3; ++j) {
        float ra = aH[0] * absR[0][j] + aH[1] * absR[1][j] + aH[2] * absR[2][j];
        float rb = bH[j];
        float tB = std::fabs(tA[0] * R[0][j] + tA[1] * R[1][j] + tA[2] * R[2][j]);
        float overlap = (ra + rb) - tB;
        if (overlap <= 0.0f) return info;
        if (overlap < minOverlap) {
            minOverlap = overlap;
            minAxis = b.orientations[j];
            if (math.Dot(a.center - b.center, minAxis) < 0.0f) minAxis = minAxis * -1.0f;
        }
    }

    // --- 3) 9つの外積軸 (Ai x Bj) をテスト ---
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            engine::Vector3 axis = math.Cross(a.orientations[i], b.orientations[j]);
            float axisLen = math.Length(axis);
            if (axisLen < 1e-6f) continue; // ここで弾いているので EPS 不要
            axis = axis / axisLen;

            int i1 = (i + 1) % 3;
            int i2 = (i + 2) % 3;
            int j1 = (j + 1) % 3;
            int j2 = (j + 2) % 3;

            float ra = aH[i1] * absR[i2][j] + aH[i2] * absR[i1][j];
            float rb = bH[j1] * absR[i][j2] + bH[j2] * absR[i][j1];

            float proj = std::fabs(tA[i2] * R[i1][j] - tA[i1] * R[i2][j]);
            float overlap = (ra + rb) - proj;

            if (overlap <= 0.0f) return info;

            overlap /= axisLen; // 実際の長さに補正

            if (overlap < minOverlap) {
                minOverlap = overlap;
                minAxis = axis;
                if (math.Dot(a.center - b.center, minAxis) < 0.0f) minAxis = minAxis * -1.0f;
            }
        }
    }

    info.isColliding = true;
    info.penetration = minOverlap;
    info.normal = minAxis;
    return info;
}

// ========================================================================
// AABB vs OBB 衝突判定
// ========================================================================
CollisionInfo CheckAABBOBBCollision(const AABB& a, const OBB& b) {
    OBB obbA;
    // AABBの中心とサイズを算出
    obbA.center = (a.min + a.max) * 0.5f;
    obbA.size = (a.max - a.min) * 0.5f;

    // AABBなので回転は単位行列（XYZ軸そのまま）
    obbA.orientations[0] = { 1.0f, 0.0f, 0.0f };
    obbA.orientations[1] = { 0.0f, 1.0f, 0.0f };
    obbA.orientations[2] = { 0.0f, 0.0f, 1.0f };
    // OBB同士として判定
    return CheckOBBCollision(obbA, b);
}

// -----------------------------------------------------------------
// Ring vs Sphere
// -----------------------------------------------------------------
CollisionInfo CheckRingSphereCollision(const Ring& ring, const engine::Vector3& spherePos, float sphereRadius) {
    CollisionInfo info = { false, {0,0,0}, 0.0f };
    engine::Math math;

    // 1. 球の中心からリング平面への垂直距離と、平面上の投影点Pを求める
    engine::Vector3 v = spherePos - ring.center;
    float distPlane = math.Dot(v, ring.normal); // 平面からの垂直距離
    engine::Vector3 P = spherePos - ring.normal * distPlane; // 平面上の投影点

    // 2. 厚み（高さ）の考慮
    // 平面からの距離を高さの範囲にクランプ
    float halfH = ring.height * 0.5f;
    float clampedH = std::clamp(distPlane, -halfH, halfH);
    
    // 3. 平面上の点Pからリング中心までの距離を求める
    engine::Vector3 centerToP = P - ring.center;
    float distCenterToP = math.Length(centerToP);

    engine::Vector3 closestPointOnAnnulus;
    if (distCenterToP < 0.0001f) {
        // 中心と重なっている場合
        closestPointOnAnnulus = ring.center;
        if (ring.innerRadius > 0.0f) {
            engine::Vector3 axis = { 1, 0, 0 };
            if (std::abs(math.Dot(axis, ring.normal)) > 0.99f) axis = { 0, 0, 1 };
            engine::Vector3 dir = math.Normalize(math.Cross(ring.normal, axis));
            closestPointOnAnnulus += dir * ring.innerRadius;
        }
    } else {
        // リングの半径範囲にクランプ
        float clampedRadius = std::clamp(distCenterToP, ring.innerRadius, ring.outerRadius);
        closestPointOnAnnulus = ring.center + math.Normalize(centerToP) * clampedRadius;
    }

    // 厚み分を戻す
    closestPointOnAnnulus += ring.normal * clampedH;

    // 4. リング上の最短点と球の中心との距離をチェック
    engine::Vector3 diff = spherePos - closestPointOnAnnulus;
    float distance = math.Length(diff);

    if (distance <= sphereRadius) {
        info.isColliding = true;
        if (distance > 0.0001f) {
            info.penetration = sphereRadius - distance;
            info.normal = diff / distance;
        } else {
            // 完全に一致している場合は法線方向
            info.penetration = sphereRadius;
            info.normal = ring.normal;
        }
    }

    return info;
}

// -----------------------------------------------------------------
// Ring vs OBB
// -----------------------------------------------------------------
CollisionInfo CheckRingOBBCollision(const Ring& ring, const OBB& obb) {
    // 簡易化：OBBの中心に対してリング上の最短点を求め、その点がOBB内部にあるか判定
    // あるいは、球としての判定に飛ばす（OBBのサイズを考慮した球として）
    // ここでは一番シンプルな「中心点への最短距離」ベース
    engine::Math math;
    return CheckRingSphereCollision(ring, obb.center, math.Length(obb.size)); 
}

// -----------------------------------------------------------------
// Cylinder vs Sphere
// -----------------------------------------------------------------
CollisionInfo CheckCylinderSphereCollision(const Cylinder& cyl, const engine::Vector3& spherePos, float sphereRadius) {
    CollisionInfo info = { false, {0,0,0}, 0.0f };
    engine::Math math;

    // 円柱の中心軸上の最短点 P を求める
    engine::Vector3 d = spherePos - cyl.center;
    float distOnAxis = math.Dot(d, cyl.axis);
    
    // 高さ方向にクランプ
    float clampedH = std::clamp(distOnAxis, -cyl.height * 0.5f, cyl.height * 0.5f);
    engine::Vector3 P = cyl.center + cyl.axis * clampedH;

    // P から球の中心への距離
    engine::Vector3 diff = spherePos - P;
    float dist = math.Length(diff);

    if (dist <= cyl.radius + sphereRadius) {
        info.isColliding = true;
        info.penetration = (cyl.radius + sphereRadius) - dist;
        if (dist > 0.0001f) {
            info.normal = diff / dist;
        } else {
            info.normal = { 0, 1, 0 };
        }
    }
    return info;
}

// -----------------------------------------------------------------
// Cylinder vs OBB
// -----------------------------------------------------------------
CollisionInfo CheckCylinderOBBCollision(const Cylinder& cyl, const OBB& obb) {
    // 簡易化：円柱をOBBに変換して判定するか、中心点ベースで判定
    // ここでは円柱をその向きのOBBとして近似して判定に投げる
    engine::Math math;
    OBB cylObb;
    cylObb.center = cyl.center;
    cylObb.orientations[1] = cyl.axis;
    
    // axisと直交する2軸を作る
    engine::Vector3 axisX = { 1, 0, 0 };
    if (std::abs(math.Dot(axisX, cyl.axis)) > 0.99f) axisX = { 0, 0, 1 };
    cylObb.orientations[0] = math.Normalize(math.Cross(cyl.axis, axisX));
    cylObb.orientations[2] = math.Normalize(math.Cross(cylObb.orientations[0], cyl.axis));
    
    cylObb.size = { cyl.radius, cyl.height * 0.5f, cyl.radius };

    return CheckOBBCollision(cylObb, obb);
}
