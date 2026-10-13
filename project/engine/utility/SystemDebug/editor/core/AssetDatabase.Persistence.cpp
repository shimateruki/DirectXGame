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

AssetDatabase::MetaLoadResult AssetDatabase::LoadOrCreateMeta(
    const fs::path& sourcePath,
    bool createMissingMeta,
    std::unordered_map<std::string, std::string>& guidOwners) {
    // 既存.metaを優先し、不正なGUIDは元ファイルを保護して索引だけPath参照に落とします。
    MetaLoadResult result;
    result.success = true;
    result.record.sourcePath = NormalizeProjectPath(sourcePath);
    fs::path metaPath = sourcePath;
    metaPath += ".meta";
    result.record.metaPath = NormalizeProjectPath(metaPath);
    result.record.type = DetectAssetType(sourcePath);
    result.record.importer = GetDefaultImporter(result.record.type);
    result.record.importSettings = GetDefaultImportSettings(result.record.type);

    // 元ファイルの印を記録し、次回の永続索引が流用できるか判断する材料にします。
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
        // 既存のImport設定は既定値より優先し、壊れた.metaは自動上書きしません。
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
        // 初回索引作成で許可された場合だけ新しいGUIDを発行します。
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

    // 同一走査でGUIDが重複した場合、通常の索引更新では元.metaを保護します。
    // 新規作成モードでだけ新GUIDを書き、既存参照との衝突を解消します。
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

    // GUID以外のMeta情報が古いときは、既存Import設定を残したまま形式だけ更新します。
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
    // 途中失敗で既存.metaを壊さないよう、一時ファイルへ書いてから置き換えます。
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

    // Metaファイルと同じ場所の一時ファイルを確定後に置き換えます。
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

    // Windows側で既存ファイルを置き換え、書き込み完了まで要求します。
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

bool AssetDatabase::LoadPersistentIndexCache() {
    // キャッシュは高速化のための補助情報です。欠損や形式違いは再走査で回復します。
    persistentAssetsByPath_.clear();
    persistentIndexCacheAvailable_ = false;

    const fs::path cachePath = resourcesRoot_ / kPersistentIndexCachePath;
    std::error_code error;
    if (!fs::exists(cachePath, error) || error) {
        return false;
    }

    try {
        std::ifstream input(cachePath, std::ios::binary);
        if (!input.is_open()) {
            throw std::runtime_error("キャッシュファイルを開けません");
        }

        // Cacheの版だけでなく対象ResourcesのRootも照合し、別プロジェクトの情報を使いません。
        json document;
        input >> document;
        if (!document.is_object() ||
            document.value("version", 0) != kPersistentIndexCacheVersion ||
            document.value("assetMetaVersion", 0) != kAssetMetaVersion ||
            document.value("resourcesRoot", std::string()) != resourcesRootPath_ ||
            !document.contains("assets") ||
            !document["assets"].is_array()) {
            throw std::runtime_error("キャッシュ形式または対象Resourcesが一致しません");
        }

        // 全項目を一時Mapに検証してから採用し、途中で不正データが出たら一件も流用しません。
        std::unordered_map<std::string, PersistentAssetEntry> loadedAssets;
        loadedAssets.reserve(document["assets"].size());
        for (const json& item : document["assets"]) {
            if (!item.is_object()) {
                throw std::runtime_error("Asset項目がObjectではありません");
            }

            PersistentAssetEntry entry;
            entry.record.guid = ToLowerAscii(item.value("guid", std::string()));
            entry.record.sourcePath = item.value("sourcePath", std::string());
            entry.record.metaPath = item.value("metaPath", std::string());
            entry.record.importer = item.value("importer", std::string());
            entry.record.fileSize = item.value("fileSize", std::uintmax_t{0});
            entry.sourceWriteTimeTicks = item.value("sourceWriteTimeTicks", std::int64_t{0});
            entry.metaFileSize = item.value("metaFileSize", std::uintmax_t{0});
            entry.metaWriteTimeTicks = item.value("metaWriteTimeTicks", std::int64_t{0});
            entry.metaExists = item.value("metaExists", false);
            entry.record.importSettings =
                item.contains("importSettings") && item["importSettings"].is_object()
                ? item["importSettings"]
                : json::object();

            const int assetType = item.value("assetType", -1);
            if (assetType < static_cast<int>(EditorAssetType::Unknown) ||
                assetType > static_cast<int>(EditorAssetType::Binary)) {
                throw std::runtime_error("Asset種別が範囲外です");
            }
            entry.record.type = static_cast<EditorAssetType>(assetType);

            if (entry.record.sourcePath.empty() ||
                entry.record.metaPath.empty() ||
                (!entry.record.guid.empty() && !IsGuidValid(entry.record.guid))) {
                throw std::runtime_error("Asset項目の必須情報が不正です");
            }

            // 相対PathがResources外へ抜けないことと、Metaが元Assetに対応することを確認します。
            const fs::path sourceAbsolute = ResolveAbsolutePath(entry.record.sourcePath);
            fs::path expectedMetaAbsolute = sourceAbsolute;
            expectedMetaAbsolute += ".meta";
            if (!IsPathInsideResources(sourceAbsolute) ||
                entry.record.sourcePath != NormalizeProjectPath(sourceAbsolute) ||
                entry.record.metaPath != NormalizeProjectPath(expectedMetaAbsolute)) {
                throw std::runtime_error("Resources外または不正なAssetパスが含まれています");
            }

            const std::string key = ToLowerAscii(entry.record.sourcePath);
            if (!loadedAssets.emplace(key, std::move(entry)).second) {
                throw std::runtime_error("同じAssetパスが重複しています");
            }
        }

        persistentAssetsByPath_ = std::move(loadedAssets);
        persistentIndexCacheAvailable_ = true;
        return true;
    }
    catch (const std::exception& exception) {
        persistentAssetsByPath_.clear();
        AddIssue(
            AssetDatabaseIssueSeverity::Warning,
            NormalizeProjectPath(cachePath),
            std::string("永続索引を読み込めないため安全な全解析へ切り替えました: ") + exception.what());
        return false;
    }
}

bool AssetDatabase::SavePersistentIndexCache() {
    // 次回起動時に変更のない資産を再利用できるよう、今回の結果を保存します。
    const fs::path cachePath = resourcesRoot_ / kPersistentIndexCachePath;
    fs::path temporaryPath = cachePath;
    temporaryPath += ".tmp";

    // Mapの列挙順に依存しない安定したJSONを作り、差分と再現性を保ちます。
    std::vector<const PersistentAssetEntry*> sortedEntries;
    sortedEntries.reserve(persistentAssetsByPath_.size());
    for (const auto& pair : persistentAssetsByPath_) {
        sortedEntries.push_back(&pair.second);
    }
    std::sort(
        sortedEntries.begin(),
        sortedEntries.end(),
        [](const PersistentAssetEntry* left, const PersistentAssetEntry* right) {
            return left->record.sourcePath < right->record.sourcePath;
        });

    json document;
    document["version"] = kPersistentIndexCacheVersion;
    document["assetMetaVersion"] = kAssetMetaVersion;
    document["resourcesRoot"] = resourcesRootPath_;
    document["assets"] = json::array();
    for (const PersistentAssetEntry* entry : sortedEntries) {
        json item;
        item["guid"] = entry->record.guid;
        item["sourcePath"] = entry->record.sourcePath;
        item["metaPath"] = entry->record.metaPath;
        item["assetType"] = static_cast<int>(entry->record.type);
        item["importer"] = entry->record.importer;
        item["fileSize"] = entry->record.fileSize;
        item["sourceWriteTimeTicks"] = entry->sourceWriteTimeTicks;
        item["metaFileSize"] = entry->metaFileSize;
        item["metaWriteTimeTicks"] = entry->metaWriteTimeTicks;
        item["metaExists"] = entry->metaExists;
        item["importSettings"] = entry->record.importSettings.is_object()
            ? entry->record.importSettings
            : json::object();
        document["assets"].push_back(std::move(item));
    }

    std::error_code error;
    fs::create_directories(cachePath.parent_path(), error);
    if (error) {
        AddIssue(
            AssetDatabaseIssueSeverity::Warning,
            NormalizeProjectPath(cachePath),
            "永続索引フォルダを作成できません: " + error.message());
        return false;
    }

    {
        std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            AddIssue(
                AssetDatabaseIssueSeverity::Warning,
                NormalizeProjectPath(cachePath),
                "永続索引の一時ファイルを開けません。");
            return false;
        }
        output << document.dump() << '\n';
        output.flush();
        if (!output.good()) {
            AddIssue(
                AssetDatabaseIssueSeverity::Warning,
                NormalizeProjectPath(cachePath),
                "永続索引の書き込みに失敗しました。");
            return false;
        }
    }

    // Cacheも一時ファイルを完成させてから差し替え、途中失敗時は次回の全解析に任せます。
    if (!MoveFileExW(
        temporaryPath.c_str(),
        cachePath.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD windowsError = GetLastError();
        fs::remove(temporaryPath, error);
        AddIssue(
            AssetDatabaseIssueSeverity::Warning,
            NormalizeProjectPath(cachePath),
            "永続索引を確定できません。Windows Error: " + std::to_string(windowsError));
        return false;
    }
    return true;
}

// ソースと .meta のサイズ・更新時刻が一致した場合のみ、前回の解析結果を使います。
bool AssetDatabase::TryReusePersistentAsset(
    const fs::path& sourcePath,
    std::unordered_map<std::string, std::string>& guidOwners,
    EditorAssetRecord* reusedRecord) {
    // ソースと.metaの印がすべて一致したときだけ永続キャッシュを再利用します。
    if (!reusedRecord || !persistentIndexCacheAvailable_) {
        return false;
    }

    const std::string normalizedSource = NormalizeProjectPath(sourcePath);
    const std::string key = ToLowerAscii(normalizedSource);
    const auto cached = persistentAssetsByPath_.find(key);
    if (cached == persistentAssetsByPath_.end()) {
        return false;
    }

    const PersistentAssetEntry& entry = cached->second;
    std::uintmax_t sourceFileSize = 0;
    fs::file_time_type sourceWriteTime{};
    if (!ReadFileStamp(sourcePath, &sourceFileSize, &sourceWriteTime) ||
        entry.record.sourcePath != normalizedSource ||
        entry.record.fileSize != sourceFileSize ||
        entry.sourceWriteTimeTicks != FileTimeToTicks(sourceWriteTime) ||
        entry.record.type != DetectAssetType(sourcePath) ||
        entry.record.importer != GetDefaultImporter(entry.record.type)) {
        return false;
    }

    fs::path metaPath = sourcePath;
    metaPath += ".meta";
    std::error_code error;
    const bool metaExists = fs::exists(metaPath, error) && !error;
    if (error || entry.metaExists != metaExists ||
        (pendingCreateMissingMeta_ && !metaExists)) {
        return false;
    }

    if (metaExists) {
        std::uintmax_t metaFileSize = 0;
        fs::file_time_type metaWriteTime{};
        if (!ReadFileStamp(metaPath, &metaFileSize, &metaWriteTime) ||
            entry.metaFileSize != metaFileSize ||
            entry.metaWriteTimeTicks != FileTimeToTicks(metaWriteTime) ||
            !IsGuidValid(entry.record.guid)) {
            return false;
        }
    }
    else if (!entry.record.guid.empty()) {
        return false;
    }

    const auto owner = guidOwners.find(entry.record.guid);
    if (!entry.record.guid.empty() && owner != guidOwners.end()) {
        return false;
    }

    *reusedRecord = entry.record;
    reusedRecord->fileSize = sourceFileSize;
    reusedRecord->lastWriteTime = sourceWriteTime;
    if (!reusedRecord->guid.empty()) {
        guidOwners[reusedRecord->guid] = reusedRecord->sourcePath;
    }

    PersistentAssetEntry refreshedEntry = entry;
    refreshedEntry.record = *reusedRecord;
    pendingPersistentAssetsByPath_[key] = std::move(refreshedEntry);
    return true;
}

void AssetDatabase::StorePendingPersistentAsset(const EditorAssetRecord& record) {
    // 今回の走査で確定したファイル印を次回起動用の索引候補へ積みます。
    const fs::path sourcePath = ResolveAbsolutePath(record.sourcePath);
    std::uintmax_t sourceFileSize = 0;
    fs::file_time_type sourceWriteTime{};
    if (!ReadFileStamp(sourcePath, &sourceFileSize, &sourceWriteTime)) {
        return;
    }

    PersistentAssetEntry entry;
    entry.record = record;
    entry.record.fileSize = sourceFileSize;
    entry.record.lastWriteTime = sourceWriteTime;
    entry.sourceWriteTimeTicks = FileTimeToTicks(sourceWriteTime);

    const fs::path metaPath = ResolveAbsolutePath(record.metaPath);
    std::error_code error;
    entry.metaExists = fs::exists(metaPath, error) && !error;
    if (error) {
        return;
    }
    if (entry.metaExists) {
        fs::file_time_type metaWriteTime{};
        if (!ReadFileStamp(metaPath, &entry.metaFileSize, &metaWriteTime)) {
            return;
        }
        entry.metaWriteTimeTicks = FileTimeToTicks(metaWriteTime);
    }

    pendingPersistentAssetsByPath_[ToLowerAscii(record.sourcePath)] = std::move(entry);
}

EditorAssetType AssetDatabase::DetectAssetType(const fs::path& path) {
    // 拡張子からImporterの初期値に使う型だけを推定します。
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
    // .metaが未作成でも型ごとに同じImporter名へ収束させます。
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
    // 型別の既定設定は新規.metaにだけ入れ、既存の編集値は読込側で優先します。
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
    // 複数の索引要求から呼ばれるため、乱数生成器をmutexで直列化します。
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
