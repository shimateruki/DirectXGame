#pragma once

#include "json.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

enum class EditorAssetType {
    Unknown,
    Model,
    Texture,
    Json,
    Audio,
    Shader,
    Font,
    Binary,
};

struct EditorAssetRecord {
    std::string guid;
    std::string sourcePath;
    std::string metaPath;
    EditorAssetType type = EditorAssetType::Unknown;
    std::string importer;
    std::uintmax_t fileSize = 0;
    std::filesystem::file_time_type lastWriteTime{};
    nlohmann::json importSettings = nlohmann::json::object();
};

enum class AssetDatabaseIssueSeverity {
    Warning,
    Error,
};

struct AssetDatabaseIssue {
    AssetDatabaseIssueSeverity severity = AssetDatabaseIssueSeverity::Warning;
    std::string path;
    std::string message;
};

struct AssetDatabaseRefreshResult {
    std::size_t assetCount = 0;
    std::size_t createdMetaCount = 0;
    std::size_t updatedMetaCount = 0;
    std::size_t errorCount = 0;
    bool filesystemChanged = false;
};

enum class DDSCacheBuildState {
    Idle,
    Queued,
    Running,
    Succeeded,
    Failed,
};

/// Resources配下のAssetと.metaを索引化し、GUIDとPathを相互解決します。
/// 実行時の既存Path参照は維持し、Editor側から段階的にGUID参照へ移行するための基盤です。
class AssetDatabase {
public:
    static AssetDatabase* GetInstance();
    ~AssetDatabase();

    bool Initialize(const std::string& resourcesRoot = "Resources", bool createMissingMeta = true);
    AssetDatabaseRefreshResult Refresh(bool createMissingMeta = true);
    void RequestRefresh(bool createMissingMeta = true);
    void RequestDDSCacheBuild();
    bool Update();

    bool IsInitialized() const { return initialized_; }
    bool IsInitialIndexBuildInProgress() const { return initialIndexBuildInProgress_; }
    bool IsInitialDirectoryScanInProgress() const { return initialDirectoryScanInProgress_; }
    std::size_t GetInitialDiscoveredAssetCount() const { return pendingSourcePaths_.size(); }
    std::size_t GetInitialIndexProgress() const { return pendingSourcePathIndex_; }
    std::size_t GetInitialIndexTotal() const { return pendingSourcePaths_.size(); }
    std::uint64_t GetGeneration() const { return generation_; }
    const std::string& GetResourcesRoot() const { return resourcesRootPath_; }
    const AssetDatabaseRefreshResult& GetLastRefreshResult() const { return lastRefreshResult_; }
    const std::vector<EditorAssetRecord>& GetAssets() const { return assets_; }
    const std::vector<AssetDatabaseIssue>& GetIssues() const { return issues_; }
    DDSCacheBuildState GetDDSCacheBuildState() const { return ddsCacheBuildState_; }
    const std::string& GetDDSCacheBuildMessage() const { return ddsCacheBuildMessage_; }
    std::uint32_t GetLastDDSCacheBuildExitCode() const { return lastDDSCacheBuildExitCode_; }

    const EditorAssetRecord* FindByGuid(const std::string& guid) const;
    const EditorAssetRecord* FindByPath(const std::string& sourcePath) const;
    std::string ResolveAssetPath(const std::string& pathOrGuidReference) const;
    std::string ResolveAssetGuid(const std::string& sourcePath) const;
    std::string MakeGuidReference(const std::string& sourcePath) const;

    std::vector<const EditorAssetRecord*> GetAssetsInDirectory(
        const std::string& directory,
        bool recursive = false) const;
    std::vector<std::string> GetSubdirectories(const std::string& directory) const;

    bool RenameAsset(
        const std::string& sourcePath,
        const std::string& newFileName,
        std::string* errorMessage = nullptr);
    bool MoveAsset(
        const std::string& sourcePath,
        const std::string& destinationPath,
        std::string* errorMessage = nullptr);
    bool MoveAssetToTrash(
        const std::string& sourcePath,
        std::string* recoveredPath = nullptr,
        std::string* errorMessage = nullptr);

    static bool IsGuidValid(const std::string& guid);
    static const char* GetAssetTypeName(EditorAssetType type);

private:
    struct MetaLoadResult {
        bool success = false;
        bool created = false;
        bool updated = false;
        EditorAssetRecord record;
    };

    std::string NormalizeProjectPath(const std::filesystem::path& path) const;
    std::filesystem::path ResolveAbsolutePath(const std::string& projectPath) const;
    bool IsPathInsideResources(const std::filesystem::path& absolutePath) const;
    bool ShouldSkipPath(const std::filesystem::path& path) const;
    MetaLoadResult LoadOrCreateMeta(
        const std::filesystem::path& sourcePath,
        bool createMissingMeta,
        std::unordered_map<std::string, std::string>& guidOwners);
    bool WriteMeta(const EditorAssetRecord& record, std::string* errorMessage = nullptr) const;
    bool StartFilesystemWatcher();
    void StopFilesystemWatcher();
    void RestartFilesystemWatcher();
    std::vector<std::filesystem::path> CollectSourcePaths();
    void BeginInitialIndexBuild(bool createMissingMeta, bool buildDDSCacheAfterCompletion);
    bool ProcessInitialIndexBuild();
    void CompleteInitialIndexBuild();
    bool StartDDSCacheBuild();
    bool PollDDSCacheBuild();
    void CloseDDSCacheProcessHandle();
    void RebuildLookupTables();
    void AddIssue(AssetDatabaseIssueSeverity severity, std::string path, std::string message);

    static EditorAssetType DetectAssetType(const std::filesystem::path& path);
    static std::string GetDefaultImporter(EditorAssetType type);
    static nlohmann::json GetDefaultImportSettings(EditorAssetType type);
    static std::string GenerateGuid();

    bool initialized_ = false;
    bool createMissingMeta_ = true;
    std::filesystem::path projectRoot_;
    std::filesystem::path resourcesRoot_;
    std::string resourcesRootPath_ = "Resources";
    std::vector<EditorAssetRecord> assets_;
    std::vector<AssetDatabaseIssue> issues_;
    std::unordered_map<std::string, std::size_t> assetIndexByGuid_;
    std::unordered_map<std::string, std::size_t> assetIndexByPath_;
    std::unordered_map<std::string, std::vector<std::size_t>> assetsByDirectory_;
    std::unordered_map<std::string, std::vector<std::string>> subdirectoriesByDirectory_;
    std::uint64_t generation_ = 0;
    bool initialIndexBuildInProgress_ = false;
    bool initialDirectoryScanInProgress_ = false;
    bool pendingCreateMissingMeta_ = true;
    std::filesystem::recursive_directory_iterator pendingDirectoryIterator_;
    std::vector<std::filesystem::path> pendingSourcePaths_;
    std::size_t pendingSourcePathIndex_ = 0;
    std::unordered_map<std::string, std::string> pendingGuidOwners_;
    AssetDatabaseRefreshResult pendingRefreshResult_;
    std::vector<void*> changeNotificationHandles_;
    bool filesystemChangePending_ = false;
    std::chrono::steady_clock::time_point filesystemChangeReadyAt_{};
    AssetDatabaseRefreshResult lastRefreshResult_;
    DDSCacheBuildState ddsCacheBuildState_ = DDSCacheBuildState::Idle;
    std::string ddsCacheBuildMessage_;
    std::uint32_t lastDDSCacheBuildExitCode_ = 0;
    void* ddsCacheProcessHandle_ = nullptr;
    bool buildDDSCacheAfterIndex_ = false;
    bool refreshAfterDDSCacheBuild_ = false;
};
