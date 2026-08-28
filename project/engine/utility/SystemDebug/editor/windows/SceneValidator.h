#pragma once

#include "CollisionConfig.h"
#include "Collider.h"
#include "IEditable.h"
#include "ScenePreloader.h"
#include "engine/utility/math/Math.h"

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

class SceneManager;
class Object3d;
class DebugEditor;
class PrimitiveDrawer;
struct ID3D12GraphicsCommandList;

/// <summary>
/// シーン内オブジェクトの参照切れや設定ミスを検査するEditorウィンドウ。
/// </summary>
/// 現在シーン内のモデル欠落、参照ミス、設定不備を検出して一覧表示する検証ウィンドウ。
class SceneValidator : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void DrawImGui() override;
    /// 選択中の監査問題をScene上へ強調表示する。
    void DrawDebug(PrimitiveDrawer& drawer, ID3D12GraphicsCommandList* commandList, int& instanceCount, int maxDrawLimit) const;
    std::string GetName() override;

private:
    enum class Severity {
        Info,
        Warning,
        Error
    };
    enum class FixKind {
        None,
        Translate,
        Rotation,
        Collider,
        EventId,
        Model,
        Texture,
        NormalMap,
        OrmMap,
        CollisionFilter,
    };

    struct FixState {
        Vector3 translate{};
        Vector3 rotation{};
        ColliderConfig collider{};
        int eventId = 0;
        uint32_t collisionAttribute = 0;
        uint32_t collisionMask = 0;
        std::string textValue;
    };

    /// 1件の検証結果として、重要度・対象Object名・カテゴリ・説明文を保持する。
    struct Issue {
        Severity severity = Severity::Info;
        std::string objectName;
        std::string objectGuid;
        std::string secondaryObjectName;
        std::string secondaryObjectGuid;
        std::string category;
        std::string message;
        OBB primaryBounds{};
        OBB secondaryBounds{};
        bool hasPrimaryBounds = false;
        bool hasSecondaryBounds = false;
        float penetration = 0.0f;
        float sampleTimeSeconds = -1.0f;
        std::string pathName;
        FixKind fixKind = FixKind::None;
        FixState beforeFix{};
        FixState afterFix{};
        std::string fixTitle;
        std::string beforeText;
        std::string afterText;
        bool hasFix = false;
        bool fixSelected = false;
        bool safeFix = false;
        std::vector<std::string> assetCandidates;
        int selectedAssetCandidate = 0;
        OBB proposedBounds{};
        bool hasProposedBounds = false;
    };

    struct AuditCollider {
        const Object3d* object = nullptr;
        OBB bounds{};
        bool isGround = false;
        bool isTrigger = false;
        bool isDynamic = false;
    };

private:
    /// 現在シーンを再走査し、検証結果一覧を作り直す。
    void Refresh();
    /// 見た目モデルが床用コライダーへ深く入り込んでいる候補を抽出する。
    void CheckFloorPenetrations();
    /// 回転を考慮したOBBで、シーン内Object同士の深い重なりを検査する。
    void CheckObjectOverlaps();
    /// Ghost Pathを持つObjectを時間サンプリングし、移動途中の重なりを検査する。
    void CheckDynamicPathOverlaps();
    /// スターコインなど正面を持つ収集物の傾き・向きが統一されているか検査する。
    void CheckCollectibleOrientations();
    void CheckColliderFits();
    void CheckRotationNormalization();
    /// 検出した問題を重要度付きで一覧へ追加する。
    void AddIssue(Severity severity, const Object3d* object, const std::string& category, const std::string& message);
    void AddSpatialIssue(Severity severity, const Object3d* primary, const Object3d* secondary,
        const std::string& category, const std::string& message,
        const OBB& primaryBounds, const OBB& secondaryBounds, float penetration,
        float sampleTimeSeconds = -1.0f, const std::string& pathName = {});
    std::vector<AuditCollider> CollectAuditColliders() const;
    OBB GetAuditBounds(const Object3d* object) const;
    OBB BuildSampledBounds(const Object3d* object, const Vector3& position,
        const Vector3& rotation, const Vector3& scale) const;
    OBB BuildBoundsForConfig(const Object3d* object, const ColliderConfig& config,
        const Vector3& position, const Vector3& rotation, const Vector3& scale) const;
    bool ShouldIgnorePair(const AuditCollider& first, const AuditCollider& second, float penetration) const;
    Object3d* FindObjectByName(const std::string& name) const;
    Object3d* FindObjectByGuid(const std::string& guid, const std::string& fallbackName = {}) const;
    void SelectIssue(std::size_t issueIndex);
    void DrawFixPreview(Issue& issue);
    void ApplySelectedFixes();
    bool ApplyIssueFix(std::size_t issueIndex);
    bool ApplyFixState(const std::string& objectGuid, const std::string& objectName,
        FixKind kind, const FixState& state);
    std::vector<std::string> FindAssetCandidates(
        const std::string& missingValue, bool modelAsset, std::size_t maxCount = 5) const;
    std::string MakeModelReference(const std::string& sourcePath) const;
    void SetTranslateFix(Issue& issue, const Object3d* object, const Vector3& after,
        const std::string& title, bool safeFix);
    void SetRotationFix(Issue& issue, const Object3d* object, const Vector3& after,
        const std::string& title, bool safeFix);
    void SetColliderFix(Issue& issue, const Object3d* object, const ColliderConfig& after,
        const std::string& title, bool safeFix);
    void SetEventIdFix(Issue& issue, const Object3d* object, int after,
        const std::string& title, bool safeFix);
    void SetAssetFix(Issue& issue, const Object3d* object, FixKind kind,
        const std::string& before, const std::vector<std::string>& candidates,
        const std::string& title);
    void SetCollisionFilterFix(Issue& issue, const Object3d* object,
        uint32_t afterAttribute, uint32_t afterMask, const std::string& title, bool safeFix);
    /// 現在の監査結果をJSON・SVG・Game View画像としてまとめて出力する。
    bool ExportVisualAudit();
    /// 選択中の問題だけをGame View画像として保存する。
    bool CaptureSelectedIssue();
    /// 機械処理向けの監査結果をJSONで保存する。
    bool WriteJsonReport(const std::filesystem::path& outputPath) const;
    /// シーン全体の重なりを俯瞰できるSVGを保存する。
    bool WriteSvgOverview(const std::filesystem::path& outputPath) const;
    /// Objectが参照しているモデル名がResources内に存在するか確認する。
    bool DoesModelExist(const std::string& modelName) const;
    bool DoesFileExist(const std::string& path) const;
    const char* GetSeverityLabel(Severity severity) const;
    unsigned int GetSeverityColor(Severity severity) const;

private:
    // SceneManagerへの参照。SceneValidatorは所有しない。
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;

    std::vector<Issue> issues_;
    bool autoRefresh_ = false;
    bool checkFloorPenetrations_ = true;
    bool checkObjectOverlaps_ = true;
    bool checkDynamicPaths_ = true;
    bool checkCollectibleOrientations_ = true;
    bool checkColliderFits_ = true;
    bool checkRotationNormalization_ = true;
    float floorPenetrationThreshold_ = 0.18f;
    float objectOverlapThreshold_ = 0.20f;
    float dynamicOverlapThreshold_ = 0.15f;
    int dynamicSampleCount_ = 12;
    float collectibleRotationToleranceDegrees_ = 8.0f;
    float colliderFitTolerance_ = 0.30f;
    int selectedSeverityFilter_ = 0;
    int selectedIssueIndex_ = -1;
    bool showFixPreview_ = true;
    float autoRefreshTimer_ = 0.0f;
    std::filesystem::path lastReportDirectory_;
    std::string auditStatusText_;
    SceneDependencyReport dependencyReport_;
    bool hasDependencyReport_ = false;
    std::string dependencyStatusText_;
};
