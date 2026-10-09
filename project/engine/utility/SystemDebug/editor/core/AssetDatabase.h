#pragma once

#include "json.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

/// エディターが拡張子から識別する資産の種類。
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

/// ソースファイルと .meta を合わせた一件分の索引情報。
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

/// 走査中に見つけた問題。path は表示と原因特定に使用します。
struct AssetDatabaseIssue {
    AssetDatabaseIssueSeverity severity = AssetDatabaseIssueSeverity::Warning;
    std::string path;
    std::string message;
};

/// 更新で作成・再利用した件数。初回走査の進捗表示にも使用します。
struct AssetDatabaseRefreshResult {
    std::size_t assetCount = 0;
    std::size_t createdMetaCount = 0;
    std::size_t updatedMetaCount = 0;
    std::size_t reusedAssetCount = 0;
    std::size_t reparsedAssetCount = 0;
    std::size_t errorCount = 0;
    bool filesystemChanged = false;
    bool loadedPersistentCache = false;
};

/// DDS キャッシュを作る外部プロセスの状態。
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

    /// <summary>Resources の場所を確定し、永続索引と変更監視を準備します。</summary>
    /// <param name="resourcesRoot">プロジェクトから見た資産のルート。</param>
    /// <param name="createMissingMeta">不足した .meta を生成する場合は true。</param>
    bool Initialize(const std::string& resourcesRoot = "Resources", bool createMissingMeta = true);

    /// <summary>資産を同期的に再走査します。UI の通常更新には RequestRefresh を使用します。</summary>
    AssetDatabaseRefreshResult Refresh(bool createMissingMeta = true);

    /// <summary>フレーム単位で進む再走査を予約します。</summary>
    void RequestRefresh(bool createMissingMeta = true);

    /// <summary>索引更新後の DDS キャッシュ作成を予約します。</summary>
    void RequestDDSCacheBuild();

    /// <summary>変更監視、段階的索引作成、DDS プロセスの結果回収を進めます。</summary>
    bool Update();

    // 以下の getter は初回索引作成の進捗と最後の結果を UI に公開します。
    bool IsInitialized() const { return initialized_; }
    bool IsInitialIndexBuildInProgress() const { return initialIndexBuildInProgress_; }
    bool IsInitialDirectoryScanInProgress() const { return initialDirectoryScanInProgress_; }
    std::size_t GetInitialDiscoveredAssetCount() const { return pendingSourcePaths_.size(); }
    std::size_t GetInitialIndexProgress() const { return pendingSourcePathIndex_; }
    std::size_t GetInitialIndexTotal() const { return pendingSourcePaths_.size(); }
    std::size_t GetInitialReusedAssetCount() const { return pendingRefreshResult_.reusedAssetCount; }
    std::size_t GetInitialReparsedAssetCount() const { return pendingRefreshResult_.reparsedAssetCount; }
    std::uint64_t GetGeneration() const { return generation_; }
    const std::string& GetResourcesRoot() const { return resourcesRootPath_; }
    const AssetDatabaseRefreshResult& GetLastRefreshResult() const { return lastRefreshResult_; }
    const std::vector<EditorAssetRecord>& GetAssets() const { return assets_; }
    const std::vector<AssetDatabaseIssue>& GetIssues() const { return issues_; }
    DDSCacheBuildState GetDDSCacheBuildState() const { return ddsCacheBuildState_; }
    const std::string& GetDDSCacheBuildMessage() const { return ddsCacheBuildMessage_; }
    std::uint32_t GetLastDDSCacheBuildExitCode() const { return lastDDSCacheBuildExitCode_; }

    /// GUID またはソースパスから既存の資産レコードを探します。見つからない場合は nullptr。
    const EditorAssetRecord* FindByGuid(const std::string& guid) const;
    const EditorAssetRecord* FindByPath(const std::string& sourcePath) const;

    /// GUID 参照と従来のパス参照を共通の資産パスへ解決します。
    std::string ResolveAssetPath(const std::string& pathOrGuidReference) const;
    std::string ResolveAssetGuid(const std::string& sourcePath) const;
    std::string MakeGuidReference(const std::string& sourcePath) const;

    /// 指定ディレクトリ内の資産を返します。recursive が true なら子階層も含めます。
    std::vector<const EditorAssetRecord*> GetAssetsInDirectory(
        const std::string& directory,
        bool recursive = false) const;
    // 索引に含まれる直下の子ディレクトリを返す。未登録なら空。
    std::vector<std::string> GetSubdirectories(const std::string& directory) const;

    /// ソースと対応する .meta を一緒に移動し、索引を更新します。
    bool RenameAsset(
        const std::string& sourcePath,
        const std::string& newFileName,
        std::string* errorMessage = nullptr);
    bool MoveAsset(
        const std::string& sourcePath,
        const std::string& destinationPath,
        std::string* errorMessage = nullptr);
    /// ソースと .meta を退避先へ移し、復元可能なパスを返します。
    bool MoveAssetToTrash(
        const std::string& sourcePath,
        std::string* recoveredPath = nullptr,
        std::string* errorMessage = nullptr);

    // GUID は区切り記号なしの32桁の16進文字列として検査する。
    static bool IsGuidValid(const std::string& guid);
    static const char* GetAssetTypeName(EditorAssetType type);

private:
    /// .meta 読込時に生成・更新したかを呼び出し元へ返す内部結果。
    struct MetaLoadResult {
        bool success = false;
        bool created = false;
        bool updated = false;
        EditorAssetRecord record;
    };

    /// 起動をまたいで再利用するAsset情報と、変更検知に必要なファイル状態です。
    struct PersistentAssetEntry {
        EditorAssetRecord record;
        std::uintmax_t metaFileSize = 0;
        std::int64_t sourceWriteTimeTicks = 0;
        std::int64_t metaWriteTimeTicks = 0;
        bool metaExists = false;
    };

    // パスの正規化と Resources 配下の判定を資産操作で共有します。
    std::string NormalizeProjectPath(const std::filesystem::path& path) const;
    std::filesystem::path ResolveAbsolutePath(const std::string& projectPath) const;
    bool IsPathInsideResources(const std::filesystem::path& absolutePath) const;
    bool ShouldSkipPath(const std::filesystem::path& path) const;
    // .meta と永続索引の読み書き。変更のないファイルは記録済み状態を再利用します。
    MetaLoadResult LoadOrCreateMeta(
        const std::filesystem::path& sourcePath,
        bool createMissingMeta,
        std::unordered_map<std::string, std::string>& guidOwners);
    bool WriteMeta(const EditorAssetRecord& record, std::string* errorMessage = nullptr) const;
    bool LoadPersistentIndexCache();
    bool SavePersistentIndexCache();
    bool TryReusePersistentAsset(
        const std::filesystem::path& sourcePath,
        std::unordered_map<std::string, std::string>& guidOwners,
        EditorAssetRecord* reusedRecord);
    void StorePendingPersistentAsset(const EditorAssetRecord& record);
    // 変更通知と初回走査は Update() から段階的に進めます。
    bool StartFilesystemWatcher();
    void StopFilesystemWatcher();
    void RestartFilesystemWatcher();
    std::vector<std::filesystem::path> CollectSourcePaths();
    void BeginInitialIndexBuild(bool createMissingMeta, bool buildDDSCacheAfterCompletion);
    bool ProcessInitialIndexBuild();
    void CompleteInitialIndexBuild();
    // DDS 作成ツールは別プロセスで実行し、Update() で終了状態を回収します。
    bool StartDDSCacheBuild();
    bool PollDDSCacheBuild();
    void CloseDDSCacheProcessHandle();
    void RebuildLookupTables();
    void AddIssue(AssetDatabaseIssueSeverity severity, std::string path, std::string message);

    static EditorAssetType DetectAssetType(const std::filesystem::path& path);
    static std::string GetDefaultImporter(EditorAssetType type);
    static nlohmann::json GetDefaultImportSettings(EditorAssetType type);
    static std::string GenerateGuid();

    // 公開する現在の索引と、作成途中の索引は完了まで分けて保持します。
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
    // 初回走査のカーソルと GUID 所有者。同一フレームに全資産を処理しません。
    bool initialIndexBuildInProgress_ = false;
    bool initialDirectoryScanInProgress_ = false;
    bool pendingCreateMissingMeta_ = true;
    std::filesystem::recursive_directory_iterator pendingDirectoryIterator_;
    std::vector<std::filesystem::path> pendingSourcePaths_;
    std::size_t pendingSourcePathIndex_ = 0;
    std::unordered_map<std::string, std::string> pendingGuidOwners_;
    // 前回起動時の永続索引と、今回保存する索引。
    std::unordered_map<std::string, PersistentAssetEntry> persistentAssetsByPath_;
    std::unordered_map<std::string, PersistentAssetEntry> pendingPersistentAssetsByPath_;
    bool persistentIndexCacheInitialized_ = false;
    bool persistentIndexCacheAvailable_ = false;
    AssetDatabaseRefreshResult pendingRefreshResult_;
    // Windows の監視・プロセスハンドルは対応する停止処理で解放します。
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
