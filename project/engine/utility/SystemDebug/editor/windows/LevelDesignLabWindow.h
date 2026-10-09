#pragma once

#include "IEditable.h"
#include "engine/utility/math/Math.h"
#include "json.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

class DebugEditor;
class Object3d;
class SceneManager;

// 意味付きLevel Chunkと外部の配置Planを検証し、Game Viewへ安全にプレビュー配置します。
// AIはScene JSONを直接変更せず、この窓口が受理できる制限付きPlanだけを生成します。
class LevelDesignLabWindow : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "Level Design Lab"; }

private:
    enum class Severity {
        Info,
        Warning,
        Error,
    };

    // チャンク同士の接続位置。配置 Transform を掛ける前のチャンク内座標。
    struct Socket {
        Vector3 position{};
    };

    // 視界の遮蔽検査に使う局所的な箱。halfExtents は各軸の半分の大きさ。
    struct SightBound {
        Vector3 center{};
        Vector3 halfExtents{ 1.0f, 1.0f, 1.0f };
    };

    // カタログの再利用単位。モデル・衝突寸法・接続位置・通行と遮蔽の分類をまとめる。
    struct ChunkDefinition {
        std::string id;
        std::string displayName;
        std::string modelName;
        std::string role;
        Vector3 halfExtents{ 1.0f, 1.0f, 1.0f };
        Vector3 colliderSize{ 1.0f, 1.0f, 1.0f };
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::vector<SightBound> sightBounds;
        Socket entry;
        Socket exit;
        bool walkable = true;
        bool blocksSight = true;
    };

    // Plan の一配置。position はワールド座標、rotationY は度単位、scale は倍率です。
    struct Operation {
        std::string name;
        std::string chunkId;
        Vector3 position{};
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
        float rotationY = 0.0f;
        bool allowOverlap = false;
    };

    // 操作名で結ぶ通行可能性の要求。maxGap はワールド距離です。
    struct Link {
        std::string from;
        std::string to;
        std::string ability;
        float maxGap = 2.0f;
    };

    // 配置プランの診断結果。重大度とカテゴリを表示し、修正判断の材料にする。
    struct ValidationIssue {
        Severity severity = Severity::Info;
        std::string category;
        std::string message;
    };

    // 配置後の範囲を最小・最大座標で表す軸平行箱。半径や全幅ではない。
    struct Bounds {
        Vector3 min{};
        Vector3 max{};
    };

    bool LoadCatalog();
    bool LoadPlan();
    void ValidatePlan();
    // 検証済み Plan を Game View の配置プレビューへ渡します。確定操作まではシーンを変更しません。
    void StartPlanPreview();
    void ResetLoadedPlan();

    bool ReadVector3(const nlohmann::json& value, Vector3& output) const;
    bool ReadVector4(const nlohmann::json& value, Vector4& output) const;
    Vector3 TransformLocalPoint(const Operation& operation, const Vector3& localPoint) const;
    Bounds BuildBounds(const Operation& operation, const ChunkDefinition& chunk) const;
    Bounds BuildBounds(const Operation& operation, const Vector3& localCenter, const Vector3& localHalfExtents) const;
    bool SegmentIntersectsBounds(const Vector3& start, const Vector3& end, const Bounds& bounds) const;
    const Operation* FindOperation(const std::string& name) const;
    const ChunkDefinition* FindChunk(const Operation& operation) const;
    void AddIssue(Severity severity, const std::string& category, const std::string& message);
    bool HasErrors() const;
    const char* GetSeverityLabel(Severity severity) const;
    unsigned int GetSeverityColor(Severity severity) const;

private:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;

    std::unordered_map<std::string, ChunkDefinition> chunks_;
    std::vector<Operation> operations_;
    std::vector<Link> links_;
    std::vector<ValidationIssue> issues_;
    nlohmann::json planJson_;

    std::string planName_;
    std::string startOperation_;
    std::string goalOperation_;
    char catalogPath_[260] = "Resources/json/level_design/level_chunks.json";
    char planPath_[260] = "Resources/json/level_design/stage4_astral_garden_plan.json";
    std::string statusText_;
    bool catalogLoaded_ = false;
    bool planLoaded_ = false;
    bool validationComplete_ = false;
};
