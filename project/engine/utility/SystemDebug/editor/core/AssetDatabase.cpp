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

AssetDatabase* AssetDatabase::GetInstance() {
    static AssetDatabase instance;
    return &instance;
}

AssetDatabase::~AssetDatabase() {
    StopFilesystemWatcher();
    CloseDDSCacheProcessHandle();
}

bool AssetDatabase::Initialize(const std::string& resourcesRoot, bool createMissingMeta) {
    StopFilesystemWatcher();
    std::error_code error;
    projectRoot_ = fs::current_path(error);
    if (error) {
        return false;
    }

    resourcesRoot_ = fs::absolute(fs::path(resourcesRoot), error).lexically_normal();
    if (error || !fs::exists(resourcesRoot_, error) || !fs::is_directory(resourcesRoot_, error)) {
        return false;
    }

    resourcesRootPath_ = NormalizeProjectPath(resourcesRoot_);
    createMissingMeta_ = createMissingMeta;
    persistentAssetsByPath_.clear();
    pendingPersistentAssetsByPath_.clear();
    persistentIndexCacheInitialized_ = false;
    persistentIndexCacheAvailable_ = false;
    initialized_ = true;
    // 起動時は永続索引を利用した差分確認だけを行います。
    // DDS全走査はProject Windowの明示操作や更新要求時に限定します。
    BeginInitialIndexBuild(createMissingMeta_, false);
    return true;
}

AssetDatabaseRefreshResult AssetDatabase::Refresh(bool createMissingMeta) {
    // 明示的な同期更新でも、既存の .meta と永続索引を優先して再利用します。
    AssetDatabaseRefreshResult result;
    initialIndexBuildInProgress_ = false;
    initialDirectoryScanInProgress_ = false;
    pendingDirectoryIterator_ = fs::recursive_directory_iterator();
    pendingSourcePaths_.clear();
    pendingSourcePathIndex_ = 0;
    pendingGuidOwners_.clear();
    pendingPersistentAssetsByPath_.clear();
    assets_.clear();
    issues_.clear();
    assetIndexByGuid_.clear();
    assetIndexByPath_.clear();
    assetsByDirectory_.clear();
    subdirectoriesByDirectory_.clear();

    if (!initialized_) {
        AddIssue(AssetDatabaseIssueSeverity::Error, resourcesRootPath_, "Asset Databaseが初期化されていません。");
        result.errorCount = 1;
        lastRefreshResult_ = result;
        return result;
    }

    std::error_code error;
    if (!fs::exists(resourcesRoot_, error) || !fs::is_directory(resourcesRoot_, error)) {
        AddIssue(AssetDatabaseIssueSeverity::Error, resourcesRootPath_, "Resourcesフォルダが見つかりません。");
        result.errorCount = 1;
        lastRefreshResult_ = result;
        return result;
    }

    const std::vector<fs::path> sourcePaths = CollectSourcePaths();

    std::unordered_map<std::string, std::string> guidOwners;
    assets_.reserve(sourcePaths.size());
    for (const fs::path& sourcePath : sourcePaths) {
        MetaLoadResult meta = LoadOrCreateMeta(sourcePath, createMissingMeta, guidOwners);
        ++result.reparsedAssetCount;
        if (!meta.success) {
            continue;
        }
        if (meta.created) {
            ++result.createdMetaCount;
        }
        if (meta.updated) {
            ++result.updatedMetaCount;
        }
        StorePendingPersistentAsset(meta.record);
        assets_.push_back(std::move(meta.record));
    }

    std::sort(assets_.begin(), assets_.end(), [](const EditorAssetRecord& left, const EditorAssetRecord& right) {
        return left.sourcePath < right.sourcePath;
    });
    RebuildLookupTables();

    result.assetCount = assets_.size();
    result.errorCount = static_cast<std::size_t>(std::count_if(
        issues_.begin(),
        issues_.end(),
        [](const AssetDatabaseIssue& issue) {
            return issue.severity == AssetDatabaseIssueSeverity::Error;
        }));
    ++generation_;
    lastRefreshResult_ = result;
    persistentAssetsByPath_ = std::move(pendingPersistentAssetsByPath_);
    persistentIndexCacheAvailable_ = true;
    SavePersistentIndexCache();
    if (!changeNotificationHandles_.empty()) {
        RestartFilesystemWatcher();
    }
    RequestDDSCacheBuild();
    return result;
}

void AssetDatabase::RequestRefresh(bool createMissingMeta) {
    if (!initialized_) {
        return;
    }
    BeginInitialIndexBuild(createMissingMeta, true);
}

void AssetDatabase::RequestDDSCacheBuild() {
    if (!initialized_ || ddsCacheBuildState_ == DDSCacheBuildState::Running) {
        return;
    }
    ddsCacheBuildState_ = DDSCacheBuildState::Queued;
    ddsCacheBuildMessage_ = "Asset Database更新後のDDS変換を待機しています。";
}

bool AssetDatabase::Update() {
    // 重い初回走査はフレームごとに分割し、変更通知と DDS 作成結果もここで統合します。
    if (!initialized_) {
        return false;
    }

    const bool ddsStateChanged = PollDDSCacheBuild();
    if (initialIndexBuildInProgress_) {
        return ProcessInitialIndexBuild() || ddsStateChanged;
    }

    if (refreshAfterDDSCacheBuild_) {
        refreshAfterDDSCacheBuild_ = false;
        BeginInitialIndexBuild(createMissingMeta_, false);
        return true;
    }

    if (ddsCacheBuildState_ == DDSCacheBuildState::Queued) {
        StartDDSCacheBuild();
        return true;
    }

    // DDS生成中に作られるファイル通知は完了後の一括更新で取り込むため、ここでは索引を開始しません。
    if (ddsCacheBuildState_ == DDSCacheBuildState::Running) {
        return ddsStateChanged;
    }
    if (changeNotificationHandles_.empty()) {
        return ddsStateChanged;
    }

    const auto now = std::chrono::steady_clock::now();
    bool hasFilesystemChange = false;
    bool watcherFailed = false;
    for (void* rawHandle : changeNotificationHandles_) {
        const DWORD waitResult = WaitForSingleObject(static_cast<HANDLE>(rawHandle), 0);
        if (waitResult == WAIT_OBJECT_0) {
            hasFilesystemChange = true;
        }
        else if (waitResult == WAIT_FAILED) {
            watcherFailed = true;
            break;
        }
    }

    if (hasFilesystemChange && !filesystemChangePending_) {
        filesystemChangePending_ = true;
        filesystemChangeReadyAt_ = now + kFilesystemChangeDebounce;
    }
    if (watcherFailed) {
        RestartFilesystemWatcher();
        return ddsStateChanged;
    }

    if (!filesystemChangePending_ || now < filesystemChangeReadyAt_) {
        return ddsStateChanged;
    }

    BeginInitialIndexBuild(createMissingMeta_, false);
    pendingRefreshResult_.filesystemChanged = true;
    return true;
}

const EditorAssetRecord* AssetDatabase::FindByGuid(const std::string& guid) const {
    const auto iterator = assetIndexByGuid_.find(ToLowerAscii(guid));
    return iterator == assetIndexByGuid_.end() ? nullptr : &assets_[iterator->second];
}

const EditorAssetRecord* AssetDatabase::FindByPath(const std::string& sourcePath) const {
    const std::string normalized = NormalizeProjectPath(fs::path(sourcePath));
    const auto iterator = assetIndexByPath_.find(ToLowerAscii(normalized));
    return iterator == assetIndexByPath_.end() ? nullptr : &assets_[iterator->second];
}

std::string AssetDatabase::ResolveAssetPath(const std::string& pathOrGuidReference) const {
    std::string guid = pathOrGuidReference;
    if (guid.rfind("asset:", 0) == 0) {
        guid.erase(0, 6);
    }
    else if (guid.rfind("guid:", 0) == 0) {
        guid.erase(0, 5);
    }

    if (IsGuidValid(guid)) {
        const EditorAssetRecord* asset = FindByGuid(guid);
        return asset ? asset->sourcePath : std::string();
    }

    const EditorAssetRecord* asset = FindByPath(pathOrGuidReference);
    return asset ? asset->sourcePath : pathOrGuidReference;
}

std::string AssetDatabase::ResolveAssetGuid(const std::string& sourcePath) const {
    const EditorAssetRecord* asset = FindByPath(sourcePath);
    return asset ? asset->guid : std::string();
}

std::string AssetDatabase::MakeGuidReference(const std::string& sourcePath) const {
    const std::string guid = ResolveAssetGuid(sourcePath);
    return guid.empty() ? sourcePath : "asset:" + guid;
}

std::vector<const EditorAssetRecord*> AssetDatabase::GetAssetsInDirectory(
    const std::string& directory,
    bool recursive) const {
    std::vector<const EditorAssetRecord*> result;
    const std::string normalizedDirectory = TrimTrailingSlash(NormalizeProjectPath(fs::path(directory)));
    if (!recursive) {
        const auto iterator = assetsByDirectory_.find(ToLowerAscii(normalizedDirectory));
        if (iterator == assetsByDirectory_.end()) {
            return result;
        }
        result.reserve(iterator->second.size());
        for (std::size_t index : iterator->second) {
            result.push_back(&assets_[index]);
        }
        return result;
    }

    for (const EditorAssetRecord& asset : assets_) {
        const std::string parent = TrimTrailingSlash(fs::path(asset.sourcePath).parent_path().generic_string());
        if (StartsWithPath(ToLowerAscii(parent), ToLowerAscii(normalizedDirectory))) {
            result.push_back(&asset);
        }
    }
    return result;
}

std::vector<std::string> AssetDatabase::GetSubdirectories(const std::string& directory) const {
    const std::string normalizedDirectory = ToLowerAscii(
        TrimTrailingSlash(NormalizeProjectPath(fs::path(directory))));
    const auto iterator = subdirectoriesByDirectory_.find(normalizedDirectory);
    return iterator == subdirectoriesByDirectory_.end() ? std::vector<std::string>() : iterator->second;
}

bool AssetDatabase::RenameAsset(
    const std::string& sourcePath,
    const std::string& newFileName,
    std::string* errorMessage) {
    const fs::path requestedName(newFileName);
    if (newFileName.empty() || requestedName.has_parent_path() || requestedName.filename() != requestedName) {
        if (errorMessage) {
            *errorMessage = "新しい名前にはファイル名だけを指定してください。";
        }
        return false;
    }
    const EditorAssetRecord* asset = FindByPath(sourcePath);
    if (!asset) {
        if (errorMessage) {
            *errorMessage = "Asset Databaseに登録されていないAssetです。";
        }
        return false;
    }
    const fs::path destination = fs::path(asset->sourcePath).parent_path() / requestedName;
    return MoveAsset(asset->sourcePath, destination.generic_string(), errorMessage);
}

bool AssetDatabase::MoveAsset(
    const std::string& sourcePath,
    const std::string& destinationPath,
    std::string* errorMessage) {
    const EditorAssetRecord* asset = FindByPath(sourcePath);
    if (!asset) {
        if (errorMessage) {
            *errorMessage = "Asset Databaseに登録されていないAssetです。";
        }
        return false;
    }

    const std::string sourceProjectPath = asset->sourcePath;
    const std::string sourceMetaProjectPath = asset->metaPath;
    const fs::path sourceAbsolute = ResolveAbsolutePath(sourceProjectPath);
    const fs::path sourceMetaAbsolute = ResolveAbsolutePath(sourceMetaProjectPath);
    const fs::path destinationAbsolute = ResolveAbsolutePath(destinationPath).lexically_normal();
    if (!IsPathInsideResources(destinationAbsolute) || destinationAbsolute.extension() == ".meta") {
        if (errorMessage) {
            *errorMessage = "移動先はResources配下の通常Assetを指定してください。";
        }
        return false;
    }

    std::error_code error;
    if (fs::exists(destinationAbsolute, error)) {
        if (errorMessage) {
            *errorMessage = "移動先に同名Assetが存在します。";
        }
        return false;
    }
    fs::create_directories(destinationAbsolute.parent_path(), error);
    if (error) {
        if (errorMessage) {
            *errorMessage = "移動先フォルダを作成できません: " + error.message();
        }
        return false;
    }

    fs::rename(sourceAbsolute, destinationAbsolute, error);
    if (error) {
        if (errorMessage) {
            *errorMessage = "Assetを移動できません: " + error.message();
        }
        return false;
    }

    fs::path destinationMetaAbsolute = destinationAbsolute;
    destinationMetaAbsolute += ".meta";
    if (fs::exists(sourceMetaAbsolute, error)) {
        error.clear();
        fs::rename(sourceMetaAbsolute, destinationMetaAbsolute, error);
        if (error) {
            std::error_code rollbackError;
            fs::rename(destinationAbsolute, sourceAbsolute, rollbackError);
            if (errorMessage) {
                *errorMessage = "Assetは移動しましたが.metaを移動できないためロールバックしました: " + error.message();
            }
            return false;
        }
    }

    RequestRefresh(createMissingMeta_);
    return true;
}

bool AssetDatabase::MoveAssetToTrash(
    const std::string& sourcePath,
    std::string* recoveredPath,
    std::string* errorMessage) {
    const EditorAssetRecord* asset = FindByPath(sourcePath);
    if (!asset) {
        if (errorMessage) {
            *errorMessage = "Asset Databaseに登録されていないAssetです。";
        }
        return false;
    }

    fs::path relativeToResources;
    std::error_code error;
    relativeToResources = fs::relative(ResolveAbsolutePath(asset->sourcePath), resourcesRoot_, error);
    if (error || relativeToResources.empty()) {
        if (errorMessage) {
            *errorMessage = "Resourcesからの相対パスを作成できません。";
        }
        return false;
    }

    fs::path destination = resourcesRoot_ / ".trash" / "asset_database" / MakeTimestamp() / relativeToResources;
    if (fs::exists(destination, error)) {
        destination = destination.parent_path() /
            (destination.stem().string() + "_" + asset->guid.substr(0, 8) + destination.extension().string());
    }
    const std::string destinationProjectPath = NormalizeProjectPath(destination);
    if (!MoveAsset(asset->sourcePath, destinationProjectPath, errorMessage)) {
        return false;
    }
    if (recoveredPath) {
        *recoveredPath = destinationProjectPath;
    }
    return true;
}

bool AssetDatabase::IsGuidValid(const std::string& guid) {
    if (guid.size() != 32) {
        return false;
    }
    return std::all_of(guid.begin(), guid.end(), [](unsigned char value) {
        return std::isxdigit(value) != 0;
    });
}

const char* AssetDatabase::GetAssetTypeName(EditorAssetType type) {
    switch (type) {
    case EditorAssetType::Model: return "Model";
    case EditorAssetType::Texture: return "Texture";
    case EditorAssetType::Json: return "JSON";
    case EditorAssetType::Audio: return "Audio";
    case EditorAssetType::Shader: return "Shader";
    case EditorAssetType::Font: return "Font";
    case EditorAssetType::Binary: return "Binary";
    case EditorAssetType::Unknown:
    default: return "Unknown";
    }
}

std::string AssetDatabase::NormalizeProjectPath(const fs::path& path) const {
    std::error_code error;
    fs::path absolute = path.is_absolute() ? path : projectRoot_ / path;
    absolute = fs::absolute(absolute, error).lexically_normal();
    if (error) {
        return path.lexically_normal().generic_string();
    }
    const fs::path relative = fs::relative(absolute, projectRoot_, error);
    if (!error && !relative.empty() && !relative.is_absolute()) {
        const std::string normalized = relative.lexically_normal().generic_string();
        if (normalized != ".." && normalized.rfind("../", 0) != 0) {
            return normalized;
        }
    }
    return absolute.generic_string();
}

fs::path AssetDatabase::ResolveAbsolutePath(const std::string& projectPath) const {
    const fs::path path(projectPath);
    return (path.is_absolute() ? path : projectRoot_ / path).lexically_normal();
}

bool AssetDatabase::IsPathInsideResources(const fs::path& absolutePath) const {
    std::error_code error;
    const fs::path relative = fs::relative(absolutePath.lexically_normal(), resourcesRoot_, error);
    if (error || relative.empty() || relative.is_absolute()) {
        return false;
    }
    const std::string normalized = relative.generic_string();
    return normalized != ".." && normalized.rfind("../", 0) != 0;
}

bool AssetDatabase::ShouldSkipPath(const fs::path& path) const {
    std::error_code error;
    const fs::path relative = fs::relative(path, resourcesRoot_, error);
    if (error) {
        return true;
    }
    for (const fs::path& component : relative) {
        const std::string name = ToLowerAscii(component.string());
        if (name == ".cache" || name == ".backup" || name == ".trash" || name == "tools") {
            return true;
        }
    }
    return false;
}
