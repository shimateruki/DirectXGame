#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>

namespace AssetDatabaseInternal {
namespace fs = std::filesystem;

// .meta と永続索引の形式を区別し、古い形式は安全に再構築します。
constexpr int kAssetMetaVersion = 1;
constexpr int kPersistentIndexCacheVersion = 1;
constexpr char kPersistentIndexCachePath[] = ".cache/asset_database_index.json";

// 監視通知をまとめ、初回索引作成で一フレームに使う時間と件数を制限します。
constexpr auto kFilesystemChangeDebounce = std::chrono::milliseconds(250);
constexpr auto kInitialIndexFrameBudget = std::chrono::milliseconds(4);
constexpr std::size_t kInitialDirectoryMaxEntriesPerFrame = 512;
constexpr std::size_t kInitialIndexMaxAssetsPerFrame = 128;

/// パスや拡張子の ASCII 部分だけを比較用に小文字化します。
inline std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return text;
}

/// ディレクトリの末尾区切りを除き、前方一致判定を安定させます。
inline std::string TrimTrailingSlash(std::string path) {
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) {
        path.pop_back();
    }
    return path;
}

/// ディレクトリ名が前方一致するだけの別パスを除外して包含を判定します。
inline bool StartsWithPath(const std::string& path, const std::string& directory) {
    if (path == directory) {
        return true;
    }
    return path.size() > directory.size() && path.compare(0, directory.size(), directory) == 0 &&
        path[directory.size()] == '/';
}

/// バックアップ名に使うローカル時刻の短い識別子を作ります。
inline std::string MakeTimestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime{};
    localtime_s(&localTime, &now);
    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y%m%d_%H%M%S");
    return stream.str();
}

/// 更新時刻を永続索引で比較できる整数へ変換します。
inline std::int64_t FileTimeToTicks(const fs::file_time_type& writeTime) {
    return static_cast<std::int64_t>(writeTime.time_since_epoch().count());
}

/// 通常ファイルのサイズと更新時刻を読み、再解析の要否判定に使用します。
inline bool ReadFileStamp(
    const fs::path& path,
    std::uintmax_t* fileSize,
    fs::file_time_type* writeTime) {
    if (!fileSize || !writeTime) {
        return false;
    }

    std::error_code error;
    if (!fs::exists(path, error) || error ||
        !fs::is_regular_file(path, error) || error) {
        return false;
    }

    *fileSize = fs::file_size(path, error);
    if (error) {
        return false;
    }
    *writeTime = fs::last_write_time(path, error);
    return !error;
}

} // namespace AssetDatabaseInternal
