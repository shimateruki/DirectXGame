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
// 関連：AssetDatabase（この型を使う機能の本体）
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
// 関連：AssetDatabase（この型を使う機能の本体）
struct AssetDatabaseIssue {
    AssetDatabaseIssueSeverity severity = AssetDatabaseIssueSeverity::Warning;
    std::string path;
    std::string message;
};

/// 更新で作成・再利用した件数。初回走査の進捗表示にも使用します。
// 関連：AssetDatabase（この型を使う機能の本体）
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
enum class DdsCacheBuildState {
    Idle,
    Queued,
    Running,
    Succeeded,
    Failed,
};

// 担当：Resourcesと.metaを索引化し、GUID・パス・欠損情報を管理する。
// 関連：ProjectWindow / AssetReferenceExplorer（利用）、EditorAssetRecord（索引項目）
// 主な入口：Initialize / Refresh / Update / FindByGuid / FindByPath
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
    void RequestDdsCacheBuild();

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

    DdsCacheBuildState GetDdsCacheBuildState() const { return ddsCacheBuildState_; }

    const std::string& GetDdsCacheBuildMessage() const { return ddsCacheBuildMessage_; }

    std::uint32_t GetLastDdsCacheBuildExitCode() const { return lastDdsCacheBuildExitCode_; }

    /// GUID またはソースパスから既存の資産レコードを探します。見つからない場合は nullptr。
    // 返すレコードや一覧内のポインターは索引の借用参照。再走査後は取得し直す。
    const EditorAssetRecord* FindByGuid(const std::string& guid) const;

    // 指定パスの索引を返す。見つからなければnullptr。
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
    // 関連：AssetDatabase（この型を使う機能の本体）
    struct MetaLoadResult {
        bool success = false;
        bool created = false;
        bool updated = false;
        EditorAssetRecord record;
    };

    /// 起動をまたいで再利用するAsset情報と、変更検知に必要なファイル状態です。
    // 関連：AssetDatabase（この型を使う機能の本体）
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

    // バックアップなど、索引の対象から除外するパスか判定する。
    bool ShouldSkipPath(const std::filesystem::path& path) const;

    // .meta と永続索引の読み書き。変更のないファイルは記録済み状態を再利用します。
    MetaLoadResult LoadOrCreateMeta(
        const std::filesystem::path& sourcePath,
        bool createMissingMeta,
        std::unordered_map<std::string, std::string>& guidOwners);

    // Assetの識別情報を.metaへ保存し、失敗時はfalseを返す。
    bool WriteMeta(const EditorAssetRecord& record, std::string* errorMessage = nullptr) const;

    // 前回保存した索引キャッシュを読み込む。
    bool LoadPersistentIndexCache();

    // 次回起動で再利用する索引キャッシュを保存する。
    bool SavePersistentIndexCache();

    bool TryReusePersistentAsset(
        const std::filesystem::path& sourcePath,
        std::unordered_map<std::string, std::string>& guidOwners,
        EditorAssetRecord* reusedRecord);

    // 更新したAssetを永続索引へ反映するために保持する。
    void StorePendingPersistentAsset(const EditorAssetRecord& record);

    // 変更通知と初回走査は Update() から段階的に進めます。
    bool StartFilesystemWatcher();

    // ファイル変更監視を終了し、監視用資源を解放する。
    void StopFilesystemWatcher();

    // 現在のResourcesルートで変更監視を開始し直す。
    void RestartFilesystemWatcher();

    std::vector<std::filesystem::path> CollectSourcePaths();

    // 初回の段階走査を開始し、.meta作成とDDS準備の条件を保持する。
    void BeginInitialIndexBuild(bool createMissingMeta, bool buildDdsCacheAfterCompletion);

    // 初回索引の構築を一段階進める。
    bool ProcessInitialIndexBuild();

    // 初回索引を確定し、検索表・キャッシュと後続処理を更新する。
    void CompleteInitialIndexBuild();

    // DDS 作成ツールは別プロセスで実行し、Update() で終了状態を回収します。
    bool StartDdsCacheBuild();

    // DDS生成プロセスの終了を確認し、状態と終了コードを更新する。
    bool PollDdsCacheBuild();

    // DDS生成プロセスの監視ハンドルを閉じる。
    void CloseDdsCacheProcessHandle();

    // 現在のAsset一覧からGUID・パスの検索表を作り直す。
    void RebuildLookupTables();

    // 重要度・対象パスと説明を、索引の診断一覧へ追加する。
    void AddIssue(AssetDatabaseIssueSeverity severity, std::string path, std::string message);

    // ファイルの拡張子などからAsset種別を判定する。
    static EditorAssetType DetectAssetType(const std::filesystem::path& path);

    static std::string GetDefaultImporter(EditorAssetType type);

    static nlohmann::json GetDefaultImportSettings(EditorAssetType type);

    // Asset識別に使う新しいGUID文字列を作る。
    static std::string GenerateGuid();

    // 公開する現在の索引と、作成途中の索引は完了まで分けて保持します。
    bool initialized_ = false;
    bool createMissingMeta_ = true;

    std::filesystem::path projectRoot_;
    std::filesystem::path resourcesRoot_;
    std::string resourcesRootPath_ = "Resources";

    std::vector<EditorAssetRecord> assets_;
    std::vector<AssetDatabaseIssue> issues_;

    std::unordered_map<std::string, std::size_t> assetIndexByGuid_; // GUIDからAsset一覧の位置を引く索引。
    std::unordered_map<std::string, std::size_t> assetIndexByPath_; // パスからAsset一覧の位置を引く索引。

    std::unordered_map<std::string, std::vector<std::size_t>> assetsByDirectory_;
    std::unordered_map<std::string, std::vector<std::string>> subdirectoriesByDirectory_;

    std::uint64_t generation_ = 0; // 索引更新の世代。UIの再走査判断に使う。

    // 初回走査のカーソルと GUID 所有者。同一フレームに全資産を処理しません。
    bool initialIndexBuildInProgress_ = false;
    bool initialDirectoryScanInProgress_ = false;
    bool pendingCreateMissingMeta_ = true;
    std::filesystem::recursive_directory_iterator pendingDirectoryIterator_;
    std::vector<std::filesystem::path> pendingSourcePaths_;
    std::size_t pendingSourcePathIndex_ = 0; // 段階走査で次に処理するパスの位置。
    std::unordered_map<std::string, std::string> pendingGuidOwners_;

    // 前回起動時の永続索引と、今回保存する索引。
    std::unordered_map<std::string, PersistentAssetEntry> persistentAssetsByPath_;
    std::unordered_map<std::string, PersistentAssetEntry> pendingPersistentAssetsByPath_;
    bool persistentIndexCacheInitialized_ = false;
    bool persistentIndexCacheAvailable_ = false; // 再利用可能な索引キャッシュを読み込めたか。
    AssetDatabaseRefreshResult pendingRefreshResult_;

    // Windows の監視・プロセスハンドルは対応する停止処理で解放します。
    std::vector<void*> changeNotificationHandles_;
    bool filesystemChangePending_ = false;
    std::chrono::steady_clock::time_point filesystemChangeReadyAt_{};
    AssetDatabaseRefreshResult lastRefreshResult_;
    DdsCacheBuildState ddsCacheBuildState_ = DdsCacheBuildState::Idle;
    std::string ddsCacheBuildMessage_;
    std::uint32_t lastDdsCacheBuildExitCode_ = 0;
    void* ddsCacheProcessHandle_ = nullptr;
    bool buildDdsCacheAfterIndex_ = false; // 初回索引の完了後にDDS生成を実行するか。
    bool refreshAfterDdsCacheBuild_ = false;
};
