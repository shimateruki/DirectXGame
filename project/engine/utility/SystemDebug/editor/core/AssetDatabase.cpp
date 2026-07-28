#define NOMINMAX
#include "AssetDatabase.h"

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

namespace {

constexpr int kAssetMetaVersion = 1;
constexpr auto kFilesystemChangeDebounce = std::chrono::milliseconds(250);
constexpr auto kInitialIndexFrameBudget = std::chrono::milliseconds(2);
constexpr std::size_t kInitialDirectoryMaxEntriesPerFrame = 128;
constexpr std::size_t kInitialIndexMaxAssetsPerFrame = 32;

std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

std::string TrimTrailingSlash(std::string path) {
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) {
        path.pop_back();
    }
    return path;
}

bool StartsWithPath(const std::string& path, const std::string& directory) {
    if (path == directory) {
        return true;
    }
    return path.size() > directory.size() && path.compare(0, directory.size(), directory) == 0 &&
        path[directory.size()] == '/';
}

std::string MakeTimestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime{};
    localtime_s(&localTime, &now);
    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y%m%d_%H%M%S");
    return stream.str();
}

} // namespace

AssetDatabase* AssetDatabase::GetInstance() {
    static AssetDatabase instance;
    return &instance;
}

AssetDatabase::~AssetDatabase() {
    StopFilesystemWatcher();
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
    initialized_ = true;
    BeginInitialIndexBuild(createMissingMeta_);
    return true;
}

AssetDatabaseRefreshResult AssetDatabase::Refresh(bool createMissingMeta) {
    AssetDatabaseRefreshResult result;
    initialIndexBuildInProgress_ = false;
    initialDirectoryScanInProgress_ = false;
    pendingDirectoryIterator_ = fs::recursive_directory_iterator();
    pendingSourcePaths_.clear();
    pendingSourcePathIndex_ = 0;
    pendingGuidOwners_.clear();
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
        if (!meta.success) {
            continue;
        }
        if (meta.created) {
            ++result.createdMetaCount;
        }
        if (meta.updated) {
            ++result.updatedMetaCount;
        }
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
    if (!changeNotificationHandles_.empty()) {
        RestartFilesystemWatcher();
    }
    return result;
}

void AssetDatabase::RequestRefresh(bool createMissingMeta) {
    if (!initialized_) {
        return;
    }
    BeginInitialIndexBuild(createMissingMeta);
}

bool AssetDatabase::Update() {
    if (!initialized_) {
        return false;
    }
    if (initialIndexBuildInProgress_) {
        return ProcessInitialIndexBuild();
    }
    if (changeNotificationHandles_.empty()) {
        return false;
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
        return false;
    }

    if (!filesystemChangePending_ || now < filesystemChangeReadyAt_) {
        return false;
    }

    RequestRefresh(createMissingMeta_);
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

AssetDatabase::MetaLoadResult AssetDatabase::LoadOrCreateMeta(
    const fs::path& sourcePath,
    bool createMissingMeta,
    std::unordered_map<std::string, std::string>& guidOwners) {
    MetaLoadResult result;
    result.success = true;
    result.record.sourcePath = NormalizeProjectPath(sourcePath);
    fs::path metaPath = sourcePath;
    metaPath += ".meta";
    result.record.metaPath = NormalizeProjectPath(metaPath);
    result.record.type = DetectAssetType(sourcePath);
    result.record.importer = GetDefaultImporter(result.record.type);
    result.record.importSettings = GetDefaultImportSettings(result.record.type);

    std::error_code error;
    result.record.fileSize = fs::file_size(sourcePath, error);
    if (error) {
        result.record.fileSize = 0;
        error.clear();
    }
    result.record.lastWriteTime = fs::last_write_time(sourcePath, error);

    json metadata;
    bool metadataExists = fs::exists(metaPath, error);
    if (metadataExists) {
        try {
            std::ifstream input(metaPath, std::ios::binary);
            if (!input.is_open()) {
                throw std::runtime_error("ファイルを開けません");
            }
            input >> metadata;
        }
        catch (const std::exception& exception) {
            AddIssue(
                AssetDatabaseIssueSeverity::Error,
                result.record.metaPath,
                std::string(".metaを読み込めません: ") + exception.what());
            return result;
        }

        result.record.guid = ToLowerAscii(metadata.value("guid", std::string()));
        if (!IsGuidValid(result.record.guid)) {
            AddIssue(
                AssetDatabaseIssueSeverity::Error,
                result.record.metaPath,
                "有効な32桁GUIDがありません。元.metaは保護したまま索引へ登録します。");
            result.record.guid.clear();
            return result;
        }
        if (metadata.contains("importSettings") && metadata["importSettings"].is_object()) {
            result.record.importSettings = metadata["importSettings"];
        }
    }
    else if (createMissingMeta) {
        result.record.guid = GenerateGuid();
        std::string writeError;
        if (!WriteMeta(result.record, &writeError)) {
            AddIssue(AssetDatabaseIssueSeverity::Error, result.record.metaPath, writeError);
            result.record.guid.clear();
            return result;
        }
        result.created = true;
    }
    else {
        AddIssue(
            AssetDatabaseIssueSeverity::Warning,
            result.record.sourcePath,
            ".metaがないためPath参照だけで登録しました。");
        return result;
    }

    const auto owner = guidOwners.find(result.record.guid);
    if (!result.record.guid.empty() && owner != guidOwners.end()) {
        if (!createMissingMeta) {
            AddIssue(
                AssetDatabaseIssueSeverity::Error,
                result.record.metaPath,
                "GUIDが重複しています: " + owner->second);
            result.record.guid.clear();
            return result;
        }
        const std::string duplicateGuid = result.record.guid;
        result.record.guid = GenerateGuid();
        std::string writeError;
        if (!WriteMeta(result.record, &writeError)) {
            AddIssue(AssetDatabaseIssueSeverity::Error, result.record.metaPath, writeError);
            result.record.guid.clear();
            return result;
        }
        result.updated = true;
        AddIssue(
            AssetDatabaseIssueSeverity::Warning,
            result.record.metaPath,
            "重複GUID " + duplicateGuid + " を新しいGUIDへ更新しました。");
    }

    if (!result.record.guid.empty()) {
        guidOwners[result.record.guid] = result.record.sourcePath;
    }

    if (metadataExists) {
        const bool needsUpdate = metadata.value("version", 0) != kAssetMetaVersion ||
            metadata.value("source", std::string()) != result.record.sourcePath ||
            metadata.value("assetType", std::string()) != GetAssetTypeName(result.record.type) ||
            metadata.value("importer", std::string()) != result.record.importer;
        if (needsUpdate) {
            std::string writeError;
            if (WriteMeta(result.record, &writeError)) {
                result.updated = true;
            }
            else {
                AddIssue(AssetDatabaseIssueSeverity::Error, result.record.metaPath, writeError);
            }
        }
    }
    return result;
}

bool AssetDatabase::WriteMeta(const EditorAssetRecord& record, std::string* errorMessage) const {
    if (!IsGuidValid(record.guid)) {
        if (errorMessage) {
            *errorMessage = "無効なGUIDのため.metaを書き込めません。";
        }
        return false;
    }

    json metadata;
    metadata["version"] = kAssetMetaVersion;
    metadata["guid"] = ToLowerAscii(record.guid);
    metadata["source"] = record.sourcePath;
    metadata["assetType"] = GetAssetTypeName(record.type);
    metadata["importer"] = record.importer;
    metadata["importSettings"] = record.importSettings.is_object()
        ? record.importSettings
        : json::object();

    const fs::path metaPath = ResolveAbsolutePath(record.metaPath);
    fs::path temporaryPath = metaPath;
    temporaryPath += ".tmp";
    std::error_code error;
    fs::create_directories(metaPath.parent_path(), error);
    if (error) {
        if (errorMessage) {
            *errorMessage = ".metaフォルダを作成できません: " + error.message();
        }
        return false;
    }

    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (errorMessage) {
                *errorMessage = ".meta一時ファイルを開けません。";
            }
            return false;
        }
        output << metadata.dump(2) << '\n';
        output.flush();
        if (!output.good()) {
            if (errorMessage) {
                *errorMessage = ".meta一時ファイルの書き込みに失敗しました。";
            }
            return false;
        }
    }

    if (!MoveFileExW(
        temporaryPath.c_str(),
        metaPath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD windowsError = GetLastError();
        fs::remove(temporaryPath, error);
        if (errorMessage) {
            *errorMessage = ".metaを確定できません。Windows Error: " + std::to_string(windowsError);
        }
        return false;
    }
    return true;
}

bool AssetDatabase::StartFilesystemWatcher() {
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

void AssetDatabase::BeginInitialIndexBuild(bool createMissingMeta) {
    StopFilesystemWatcher();
    assets_.clear();
    issues_.clear();
    assetIndexByGuid_.clear();
    assetIndexByPath_.clear();
    assetsByDirectory_.clear();
    subdirectoriesByDirectory_.clear();
    pendingGuidOwners_.clear();
    pendingRefreshResult_ = {};
    pendingCreateMissingMeta_ = createMissingMeta;
    pendingSourcePaths_.clear();
    pendingSourcePathIndex_ = 0;
    initialIndexBuildInProgress_ = true;
    initialDirectoryScanInProgress_ = true;

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
        MetaLoadResult meta = LoadOrCreateMeta(
            pendingSourcePaths_[pendingSourcePathIndex_],
            pendingCreateMissingMeta_,
            pendingGuidOwners_);
        if (meta.success) {
            if (meta.created) {
                ++pendingRefreshResult_.createdMetaCount;
            }
            if (meta.updated) {
                ++pendingRefreshResult_.updatedMetaCount;
            }
            assets_.push_back(std::move(meta.record));
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
    std::sort(assets_.begin(), assets_.end(), [](const EditorAssetRecord& left, const EditorAssetRecord& right) {
        return left.sourcePath < right.sourcePath;
    });
    RebuildLookupTables();

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

EditorAssetType AssetDatabase::DetectAssetType(const fs::path& path) {
    const std::string extension = ToLowerAscii(path.extension().string());
    if (extension == ".obj" || extension == ".gltf" || extension == ".glb" || extension == ".fbx") {
        return EditorAssetType::Model;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".dds" ||
        extension == ".tga" || extension == ".bmp" || extension == ".hdr") {
        return EditorAssetType::Texture;
    }
    if (extension == ".json") {
        return EditorAssetType::Json;
    }
    if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac") {
        return EditorAssetType::Audio;
    }
    if (extension == ".hlsl" || extension == ".hlsli" || extension == ".cso") {
        return EditorAssetType::Shader;
    }
    if (extension == ".ttf" || extension == ".otf") {
        return EditorAssetType::Font;
    }
    if (!extension.empty()) {
        return EditorAssetType::Binary;
    }
    return EditorAssetType::Unknown;
}

std::string AssetDatabase::GetDefaultImporter(EditorAssetType type) {
    switch (type) {
    case EditorAssetType::Model: return "ModelImporter";
    case EditorAssetType::Texture: return "TextureImporter";
    case EditorAssetType::Json: return "JsonImporter";
    case EditorAssetType::Audio: return "AudioImporter";
    case EditorAssetType::Shader: return "ShaderImporter";
    case EditorAssetType::Font: return "FontImporter";
    case EditorAssetType::Binary: return "BinaryImporter";
    case EditorAssetType::Unknown:
    default: return "DefaultImporter";
    }
}

json AssetDatabase::GetDefaultImportSettings(EditorAssetType type) {
    switch (type) {
    case EditorAssetType::Model:
        return { { "scale", 1.0 }, { "generateTangents", true } };
    case EditorAssetType::Texture:
        return { { "colorSpace", "Auto" }, { "generateMipmaps", true } };
    case EditorAssetType::Audio:
        return { { "streaming", false } };
    default:
        return json::object();
    }
}

std::string AssetDatabase::GenerateGuid() {
    static std::mutex mutex;
    static std::mt19937_64 generator([] {
        std::random_device device;
        const auto ticks = static_cast<unsigned long long>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::seed_seq seed{
            device(), device(),
            static_cast<unsigned int>(ticks),
            static_cast<unsigned int>(ticks >> 32),
        };
        return std::mt19937_64(seed);
    }());

    std::lock_guard<std::mutex> lock(mutex);
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
           << std::setw(16) << generator()
           << std::setw(16) << generator();
    return ToLowerAscii(stream.str());
}
