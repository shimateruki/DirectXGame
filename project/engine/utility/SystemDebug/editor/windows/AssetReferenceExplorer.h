#pragma once

#ifdef USE_IMGUI

#include "AssetDatabase.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class DebugEditor;

/// 資産の参照元、欠損参照、動的参照の候補を分けて確認・修復するウィンドウ。
class AssetReferenceExplorer {
public:
    /// エディターへの参照を設定します。所有権は受け取りません。
    void Initialize(DebugEditor* editor);

    /// 検索用にパス区切りと大文字小文字を正規化します。
    static std::string Normalize(std::string value);
    void Finalize();

    /// 指定資産を選択して開き、次回描画時の再走査を予約します。
    void Open(const std::string& assetPath = {});

    /// 参照結果と修復 UI を描画します。
    void Draw();
    bool IsOpen() const { return open_; }

private:
    enum class UsageKind {
        Direct,
        DynamicCandidate,
    };

    /// JSON 内の値と参照元位置を保持します。
    struct Usage {
        std::string ownerPath;
        std::string jsonPointer;
        std::string value;
        std::string ownerCategory;
        UsageKind kind = UsageKind::Direct;
    };

    /// 解決できなかった値と、修復候補の GUID を保持します。
    struct MissingReference {
        std::string ownerPath;
        std::string jsonPointer;
        std::string key;
        std::string value;
        std::vector<std::string> candidateGuids;
        int selectedCandidate = 0;
    };

    // 参照索引の構築。AssetDatabase の世代が変わったときに再実行します。
    void RefreshIndex();
    void BuildAliasIndex();
    void ScanJsonAsset(const EditorAssetRecord& owner);
    void VisitJson(
        const nlohmann::json& value,
        const std::string& ownerPath,
        const std::string& pointer,
        const std::string& key,
        std::unordered_map<std::string, std::vector<Usage>>& destination,
        bool collectMissing);
    // パス・名前などの別名から候補を解決し、欠損参照の修復候補を絞ります。
    std::vector<std::size_t> ResolveCandidates(const std::string& value, const std::string& key) const;
    std::vector<std::size_t> FindRepairCandidates(const MissingReference& missing) const;
    std::string MakeReferenceValue(const EditorAssetRecord& asset, const std::string& key) const;
    bool RepairMissingReference(MissingReference& missing, std::string& message);
    void SelectAssetByPath(const std::string& path);
    // 参照索引の表示と利用者が選んだ修復先の入力を扱います。
    void DrawAssetBrowser();
    void DrawSelectedAssetDetails();
    void DrawSelectedObjectReferences();
    void DrawMissingReferences();
    void DrawUsageList(const std::vector<Usage>& usages, UsageKind kind) const;
    static std::string EscapeJsonPointerToken(const std::string& token);
    static std::string GetOwnerCategory(const std::string& path);
    static bool IsReferenceLikeKey(const std::string& key);
    static bool IsDirectReferenceValue(const std::string& value);

private:
    // UI の選択状態と、AssetDatabase の世代に対応する参照索引。
    DebugEditor* editor_ = nullptr;
    bool open_ = false;
    bool rescanRequested_ = false;
    std::uint64_t indexedGeneration_ = 0;
    std::string requestedAssetPath_;
    std::string selectedGuid_;
    std::unordered_map<std::string, std::vector<std::size_t>> aliasToAssetIndices_;
    std::unordered_map<std::string, std::vector<Usage>> usagesByGuid_;
    std::vector<MissingReference> missingReferences_;
    char assetFilter_[192] = {};
    char renameBuffer_[260] = {};
    std::string renameBufferGuid_;
    std::string statusMessage_;
    bool requestDeleteConfirm_ = false;
};

#endif
