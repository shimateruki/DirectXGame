#pragma once

#ifdef USE_IMGUI

#include "AssetDatabase.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class DebugEditor;

// AssetReferenceExplorerは、Assetの参照元、Missing参照、動的参照候補を分けて確認します。
class AssetReferenceExplorer {
public:
    void Initialize(DebugEditor* editor);
    static std::string Normalize(std::string value);
    void Finalize();
    void Open(const std::string& assetPath = {});
    void Draw();
    bool IsOpen() const { return open_; }

private:
    enum class UsageKind {
        Direct,
        DynamicCandidate,
    };

    struct Usage {
        std::string ownerPath;
        std::string jsonPointer;
        std::string value;
        std::string ownerCategory;
        UsageKind kind = UsageKind::Direct;
    };

    struct MissingReference {
        std::string ownerPath;
        std::string jsonPointer;
        std::string key;
        std::string value;
        std::vector<std::string> candidateGuids;
        int selectedCandidate = 0;
    };

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
    std::vector<std::size_t> ResolveCandidates(const std::string& value, const std::string& key) const;
    std::vector<std::size_t> FindRepairCandidates(const MissingReference& missing) const;
    std::string MakeReferenceValue(const EditorAssetRecord& asset, const std::string& key) const;
    bool RepairMissingReference(MissingReference& missing, std::string& message);
    void SelectAssetByPath(const std::string& path);
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
