#define NOMINMAX
#include "AssetDatabase.h"
#include "AssetDatabase.Internal.h"

#include <Windows.h>

#include <algorithm>
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

bool AssetDatabase::StartFilesystemWatcher() {
    // Resources 配下の変更を通知で受け、頻繁な全走査を避けます。
    if (!initialized_ || resourcesRoot_.empty()) {
        return false;
    }

    StopFilesystemWatcher();
    const auto addWatcher = [this](const fs::path& directory, BOOL watchSubtree, DWORD filter) {
        const HANDLE notification = FindFirstChangeNotificationW(
            directory.c_str(),
            watchSubtree,
            filter);
        if (notification != INVALID_HANDLE_VALUE) {
            changeNotificationHandles_.push_back(notification);
        }
    };

    // Resources直下は名前の追加・削除だけを監視し、除外フォルダ内部の更新を拾わないようにします。
    addWatcher(
        resourcesRoot_,
        FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);

    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(resourcesRoot_, error)) {
        if (error) {
            break;
        }
        if (!entry.is_directory(error) || ShouldSkipPath(entry.path())) {
            error.clear();
            continue;
        }
        addWatcher(
            entry.path(),
            TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME |
                FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_CREATION);
    }

    filesystemChangePending_ = false;
    return !changeNotificationHandles_.empty();
}

void AssetDatabase::StopFilesystemWatcher() {
    for (void* rawHandle : changeNotificationHandles_) {
        FindCloseChangeNotification(static_cast<HANDLE>(rawHandle));
    }
    changeNotificationHandles_.clear();
    filesystemChangePending_ = false;
}

void AssetDatabase::RestartFilesystemWatcher() {
    StopFilesystemWatcher();
    StartFilesystemWatcher();
}

std::vector<fs::path> AssetDatabase::CollectSourcePaths() {
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
            const std::string extension = ToLowerAscii(entry.path().extension().string());
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

    std::sort(sourcePaths.begin(), sourcePaths.end(), [](const fs::path& left, const fs::path& right) {
        return left.generic_string() < right.generic_string();
    });
    return sourcePaths;
}

void AssetDatabase::BeginInitialIndexBuild(bool createMissingMeta, bool buildDDSCacheAfterCompletion) {
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
    buildDDSCacheAfterIndex_ = buildDDSCacheAfterIndex_ || buildDDSCacheAfterCompletion;
    initialIndexBuildInProgress_ = true;
    initialDirectoryScanInProgress_ = true;

    if (!persistentIndexCacheInitialized_) {
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
    const auto frameDeadline = std::chrono::steady_clock::now() + kInitialIndexFrameBudget;
    if (initialDirectoryScanInProgress_) {
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
                const std::string extension = ToLowerAscii(entry.path().extension().string());
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

        initialDirectoryScanInProgress_ = false;
        std::sort(pendingSourcePaths_.begin(), pendingSourcePaths_.end(), [](const fs::path& left, const fs::path& right) {
            return left.generic_string() < right.generic_string();
        });
        assets_.reserve(pendingSourcePaths_.size());
        if (pendingSourcePaths_.empty()) {
            CompleteInitialIndexBuild();
        }
        return true;
    }

    std::size_t processedThisFrame = 0;
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

    if (buildDDSCacheAfterIndex_) {
        buildDDSCacheAfterIndex_ = false;
        RequestDDSCacheBuild();
    }
}

void AssetDatabase::RebuildLookupTables() {
    std::unordered_map<std::string, std::set<std::string>> directorySets;
    for (std::size_t index = 0; index < assets_.size(); ++index) {
        const EditorAssetRecord& asset = assets_[index];
        assetIndexByPath_[ToLowerAscii(asset.sourcePath)] = index;
        if (!asset.guid.empty()) {
            assetIndexByGuid_[ToLowerAscii(asset.guid)] = index;
        }

        const std::string directory = TrimTrailingSlash(fs::path(asset.sourcePath).parent_path().generic_string());
        assetsByDirectory_[ToLowerAscii(directory)].push_back(index);

        fs::path current = fs::path(directory);
        while (!current.empty()) {
            const std::string currentPath = TrimTrailingSlash(current.generic_string());
            if (!StartsWithPath(ToLowerAscii(currentPath), ToLowerAscii(resourcesRootPath_))) {
                break;
            }
            const fs::path parentPath = current.parent_path();
            const std::string parent = TrimTrailingSlash(parentPath.generic_string());
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
