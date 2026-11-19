#define NOMINMAX
#include "AssetDatabase.h"
#include "AssetDatabase.Internal.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;
using json = nlohmann::json;

using namespace AssetDatabaseInternal;

namespace {

// 監視通知のパスは Resources からの相対パス。ファイルが消えた後も名前だけで判定する。
bool IsRuntimeOrCachePath(const fs::path& path) {
    const std::string name = ToLowerAscii(PathToUtf8(path));
    if (name == "json/save" || name.starts_with("json/save/") ||
        name == "json/user_config.json" || name == "json/user_config.json.meta") {
        return true;
    }
    for (const auto& part : path) {
        const auto component = ToLowerAscii(PathToUtf8(part));
        if (component == ".cache" || component == ".backup" || component == ".trash" || component == "tools") {
            return true;
        }
    }
    return false;
}

} // namespace

// 非同期要求が完了するまで、OVERLAPPED と通知バッファのアドレスを保持する。
struct AssetDatabase::FilesystemWatcher {
    HANDLE directory_ = INVALID_HANDLE_VALUE;
    OVERLAPPED request_{};
    alignas(DWORD) std::array<std::byte, 64 * 1024> buffer_{};
    bool pending_ = false;

    bool Arm() {
        // 前回の通知情報を消し、同じ完了イベントを初期化して次の非同期読み取りに使う。
        const HANDLE event = request_.hEvent;
        request_ = {};
        request_.hEvent = event;
        ResetEvent(event);
        pending_ = ReadDirectoryChangesW(directory_, buffer_.data(), static_cast<DWORD>(buffer_.size()),
            TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
            nullptr, &request_, nullptr) != FALSE;
        return pending_;
    }

    ~FilesystemWatcher() {
        if (pending_) {
            // 非同期 I/O のキャンセル完了を待ってからバッファとイベントを解放する。
            CancelIoEx(directory_, &request_);
            DWORD ignored = 0;
            GetOverlappedResult(directory_, &request_, &ignored, TRUE);
        }
        if (request_.hEvent) {
            CloseHandle(request_.hEvent);
        }
        if (directory_ != INVALID_HANDLE_VALUE) {
            CloseHandle(directory_);
        }
    }
};

AssetDatabase::AssetDatabase() = default;

AssetDatabase::~AssetDatabase() {
    StopFilesystemWatcher();
    CloseDdsCacheProcessHandle();
}

bool AssetDatabase::StartFilesystemWatcher() {
    StopFilesystemWatcher();
    if (!initialized_ || resourcesRoot_.empty()) {
        return false;
    }

    auto watcher = std::make_unique<FilesystemWatcher>();
    watcher->directory_ = CreateFileW(resourcesRoot_.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (watcher->directory_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    // 通知待機を確立できてから監視器の所有権を移す。途中で失敗したときは仮監視器の破棄に任せる。
    watcher->request_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!watcher->request_.hEvent || !watcher->Arm()) {
        return false;
    }
    filesystemWatcher_ = std::move(watcher);
    return true;
}

void AssetDatabase::StopFilesystemWatcher() {
    filesystemWatcher_.reset();
    filesystemChangePending_ = false;
}

void AssetDatabase::RestartFilesystemWatcher() {
    StartFilesystemWatcher();
}

bool AssetDatabase::PollFilesystemWatcher() {
    if (!filesystemWatcher_) {
        return false;
    }

    auto& watcher = *filesystemWatcher_;
    DWORD bytes = 0;
    if (!GetOverlappedResult(watcher.directory_, &watcher.request_, &bytes, FALSE)) {
        if (GetLastError() == ERROR_IO_INCOMPLETE) {
            return false;
        }
        // 通知を取得できなかった区間は、全体再走査で取り戻す。
        watcher.pending_ = false;
        RestartFilesystemWatcher();
        return true;
    }
    watcher.pending_ = false;

    // 0バイトの完了や壊れた通知は個別の変更を追えないため、全体の再走査を要求する。
    bool changed = bytes == 0;
    constexpr auto headerSize = offsetof(FILE_NOTIFY_INFORMATION, FileName);
    for (DWORD offset = 0; !changed && offset < bytes;) {
        const auto remaining = bytes - offset;
        if (remaining < headerSize) {
            changed = true;
            break;
        }
        const auto* entry = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(watcher.buffer_.data() + offset);
        if (entry->FileNameLength > remaining - headerSize || entry->FileNameLength % sizeof(wchar_t) != 0) {
            changed = true;
            break;
        }
        const fs::path relativePath(std::wstring_view(entry->FileName, entry->FileNameLength / sizeof(wchar_t)));
        // セーブや索引キャッシュの生成では再索引を起動せず、素材側の通知があるときだけ変更として返す。
        changed = !IsRuntimeOrCachePath(relativePath);
        if (entry->NextEntryOffset == 0) {
            break;
        }
        if (entry->NextEntryOffset < headerSize || entry->NextEntryOffset >= remaining ||
            entry->NextEntryOffset % alignof(DWORD) != 0) {
            changed = true;
            break;
        }
        offset += entry->NextEntryOffset;
    }

    // 通知内容を取り出し終えてからバッファを監視へ返し、次の通知を待つ。
    if (!watcher.Arm()) {
        RestartFilesystemWatcher();
        return true;
    }
    return changed;
}

std::vector<fs::path> AssetDatabase::CollectSourcePaths() {
    // 手動更新では全パスを集め、metaと一時ファイルを索引対象から除く。
    std::vector<fs::path> sourcePaths;
    std::error_code error;
    fs::recursive_directory_iterator iterator(
        resourcesRoot_,
        fs::directory_options::skip_permission_denied,
        error);
    const fs::recursive_directory_iterator end;

    while (!error && iterator != end) {
        const fs::directory_entry entry = *iterator;

        if (entry.is_directory(error) && ShouldSkipPath(entry.path())) {
            iterator.disable_recursion_pending();
        }
        else if (entry.is_regular_file(error) && !ShouldSkipPath(entry.path())) {
            const std::string extension = ToLowerAscii(PathToUtf8(entry.path().extension()));

            if (extension != ".meta" && extension != ".tmp") {
                sourcePaths.push_back(entry.path());
            }
        }

        iterator.increment(error);
    }

    if (error) {
        AddIssue(
            AssetDatabaseIssueSeverity::Error,
            resourcesRootPath_,
            "Resources走査中にエラーが発生しました: " + error.message());
    }

    // 走査順に依存しない索引と重複GUID判定にするため順序を固定する。
    std::sort(sourcePaths.begin(), sourcePaths.end(), [](const fs::path& left, const fs::path& right) {
        return PathToUtf8(left) < PathToUtf8(right);
    });
    return sourcePaths;
}

void AssetDatabase::BeginInitialIndexBuild(bool createMissingMeta, bool buildDdsCacheAfterCompletion) {
    // 初回索引の作業状態を公開中の索引とは別に用意します。
    StopFilesystemWatcher();
    assets_.clear();
    issues_.clear();
    assetIndexByGuid_.clear();
    assetIndexByPath_.clear();
    assetsByDirectory_.clear();
    subdirectoriesByDirectory_.clear();
    pendingGuidOwners_.clear();
    pendingPersistentAssetsByPath_.clear();
    pendingRefreshResult_ = {};
    pendingCreateMissingMeta_ = createMissingMeta;
    pendingSourcePaths_.clear();
    pendingSourcePathIndex_ = 0;
    buildDdsCacheAfterIndex_ = buildDdsCacheAfterIndex_ || buildDdsCacheAfterCompletion;
    initialIndexBuildInProgress_ = true;
    initialDirectoryScanInProgress_ = true;

    if (!persistentIndexCacheInitialized_) {
        // 永続索引は初回だけ読み込み、次のフレームから再利用できる候補にする。
        persistentIndexCacheInitialized_ = true;
        LoadPersistentIndexCache();
    }

    pendingRefreshResult_.loadedPersistentCache = persistentIndexCacheAvailable_;

    std::error_code error;
    pendingDirectoryIterator_ = fs::recursive_directory_iterator(
        resourcesRoot_,
        fs::directory_options::skip_permission_denied,
        error);
    if (error) {
        AddIssue(
            AssetDatabaseIssueSeverity::Error,
            resourcesRootPath_,
            "Resources走査を開始できません: " + error.message());
        initialDirectoryScanInProgress_ = false;
        CompleteInitialIndexBuild();
    }
}

bool AssetDatabase::ProcessInitialIndexBuild() {
    // 一フレームの走査量を制限し、エディター操作を止めないようにします。
    // 時間と件数の両方で上限を設け、残りの位置は次フレームへ引き継ぐ。
    const auto frameDeadline = std::chrono::steady_clock::now() + kInitialIndexFrameBudget;

    if (initialDirectoryScanInProgress_) {
        // 第1段階はファイル列挙だけを行い、meta読み込みとは別の予算で分割する。
        const fs::recursive_directory_iterator end;
        std::size_t scannedThisFrame = 0;
        std::error_code error;
        while (pendingDirectoryIterator_ != end &&
               scannedThisFrame < kInitialDirectoryMaxEntriesPerFrame) {
            const fs::directory_entry entry = *pendingDirectoryIterator_;

            if (entry.is_directory(error) && ShouldSkipPath(entry.path())) {
                pendingDirectoryIterator_.disable_recursion_pending();
            }
            else if (entry.is_regular_file(error) && !ShouldSkipPath(entry.path())) {
                const std::string extension = ToLowerAscii(PathToUtf8(entry.path().extension()));

                if (extension != ".meta" && extension != ".tmp") {
                    pendingSourcePaths_.push_back(entry.path());
                }
            }

            error.clear();
            pendingDirectoryIterator_.increment(error);
            ++scannedThisFrame;

            if (error) {
                AddIssue(
                    AssetDatabaseIssueSeverity::Error,
                    resourcesRootPath_,
                    "Resources走査中にエラーが発生しました: " + error.message());
                pendingDirectoryIterator_ = end;
                break;
            }

            if (std::chrono::steady_clock::now() >= frameDeadline) {
                break;
            }
        }

        if (pendingDirectoryIterator_ != end) {
            return true;
        }

        // 列挙完了後に安定した順序へ並べ替え、第2段階のレコード生成へ移る。
        initialDirectoryScanInProgress_ = false;
        std::sort(pendingSourcePaths_.begin(), pendingSourcePaths_.end(), [](const fs::path& left, const fs::path& right) {
            return PathToUtf8(left) < PathToUtf8(right);
        });
        assets_.reserve(pendingSourcePaths_.size());

        if (pendingSourcePaths_.empty()) {
            CompleteInitialIndexBuild();
        }

        return true;
    }

    std::size_t processedThisFrame = 0;

    // 第2段階は永続キャッシュを優先し、変更されたファイルだけmetaを読み直す。
    while (pendingSourcePathIndex_ < pendingSourcePaths_.size() &&
           processedThisFrame < kInitialIndexMaxAssetsPerFrame) {
        const fs::path& sourcePath = pendingSourcePaths_[pendingSourcePathIndex_];
        EditorAssetRecord reusedRecord;

        if (TryReusePersistentAsset(sourcePath, pendingGuidOwners_, &reusedRecord)) {
            ++pendingRefreshResult_.reusedAssetCount;
            assets_.push_back(std::move(reusedRecord));
        }
        else {
            MetaLoadResult meta = LoadOrCreateMeta(
                sourcePath,
                pendingCreateMissingMeta_,
                pendingGuidOwners_);
            ++pendingRefreshResult_.reparsedAssetCount;

            if (meta.success) {
                if (meta.created) {
                    ++pendingRefreshResult_.createdMetaCount;
                }

                if (meta.updated) {
                    ++pendingRefreshResult_.updatedMetaCount;
                }

                StorePendingPersistentAsset(meta.record);
                assets_.push_back(std::move(meta.record));
            }
        }

        ++pendingSourcePathIndex_;
        ++processedThisFrame;

        if (std::chrono::steady_clock::now() >= frameDeadline) {
            break;
        }
    }

    // フレーム内の上限に達した場合は、公開用索引を完成扱いにせず次フレームに続ける。
    if (pendingSourcePathIndex_ < pendingSourcePaths_.size()) {
        return true;
    }

    CompleteInitialIndexBuild();
    return true;
}

void AssetDatabase::CompleteInitialIndexBuild() {
    // 全件処理後に検索用テーブルと永続索引をまとめて更新します。
    std::sort(assets_.begin(), assets_.end(), [](const EditorAssetRecord& left, const EditorAssetRecord& right) {
        return left.sourcePath < right.sourcePath;
    });
    RebuildLookupTables();

    // 完成した結果を永続索引へ反映してから、変更通知を再開する。
    persistentAssetsByPath_ = std::move(pendingPersistentAssetsByPath_);
    persistentIndexCacheAvailable_ = true;
    SavePersistentIndexCache();

    pendingRefreshResult_.assetCount = assets_.size();
    pendingRefreshResult_.errorCount = static_cast<std::size_t>(std::count_if(
        issues_.begin(),
        issues_.end(),
        [](const AssetDatabaseIssue& issue) {
            return issue.severity == AssetDatabaseIssueSeverity::Error;
        }));
    lastRefreshResult_ = pendingRefreshResult_;
    initialIndexBuildInProgress_ = false;
    initialDirectoryScanInProgress_ = false;
    pendingDirectoryIterator_ = fs::recursive_directory_iterator();
    pendingGuidOwners_.clear();
    ++generation_;
    StartFilesystemWatcher();

    if (buildDdsCacheAfterIndex_) {
        buildDdsCacheAfterIndex_ = false;
        RequestDdsCacheBuild();
    }
}

void AssetDatabase::RebuildLookupTables() {
    // パスとGUIDの直接検索、親ディレクトリからの一覧検索を同時に再構築する。
    std::unordered_map<std::string, std::set<std::string>> directorySets;

    for (std::size_t index = 0; index < assets_.size(); ++index) {
        const EditorAssetRecord& asset = assets_[index];
        assetIndexByPath_[ToLowerAscii(asset.sourcePath)] = index;

        if (!asset.guid.empty()) {
            assetIndexByGuid_[ToLowerAscii(asset.guid)] = index;
        }

        const std::string directory = TrimTrailingSlash(PathToUtf8(PathFromUtf8(asset.sourcePath).parent_path()));
        assetsByDirectory_[ToLowerAscii(directory)].push_back(index);

        // 各Assetの親をResourcesまでたどり、空ディレクトリを除いた階層を作る。
        fs::path current = PathFromUtf8(directory);

        while (!current.empty()) {
            const std::string currentPath = TrimTrailingSlash(PathToUtf8(current));

            if (!StartsWithPath(ToLowerAscii(currentPath), ToLowerAscii(resourcesRootPath_))) {
                break;
            }

            const fs::path parentPath = current.parent_path();
            const std::string parent = TrimTrailingSlash(PathToUtf8(parentPath));

            if (!parent.empty() && currentPath != resourcesRootPath_) {
                directorySets[ToLowerAscii(parent)].insert(currentPath);
            }

            if (currentPath == resourcesRootPath_) {
                break;
            }

            current = parentPath;
        }
    }

    for (auto& [directory, children] : directorySets) {
        std::vector<std::string>& destination = subdirectoriesByDirectory_[directory];
        destination.assign(children.begin(), children.end());
    }
}

void AssetDatabase::AddIssue(
    AssetDatabaseIssueSeverity severity,
    std::string path,
    std::string message) {
    issues_.push_back({ severity, std::move(path), std::move(message) });
}
