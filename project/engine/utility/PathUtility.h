#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace cg2::path {

inline std::filesystem::path FromUtf8(const std::string& path) {
#if defined(__cpp_char8_t)
    std::u8string utf8Path;
    utf8Path.reserve(path.size());
    for (unsigned char c : path) {
        utf8Path.push_back(static_cast<char8_t>(c));
    }
    return std::filesystem::path(utf8Path);
#else
    return std::filesystem::path(path);
#endif
}

inline std::filesystem::path FromUtf8(const char* path) {
    return FromUtf8(std::string(path ? path : ""));
}

inline std::string ToUtf8String(const std::filesystem::path& path) {
#if defined(__cpp_char8_t)
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
#else
    return path.u8string();
#endif
}

inline std::string ToUtf8NativeString(const std::filesystem::path& path) {
    return ToUtf8String(path);
}

inline std::string NormalizeSlash(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

inline std::string ToGamePath(const std::filesystem::path& path) {
    return NormalizeSlash(ToUtf8String(path));
}

inline std::string ExtensionLower(const std::filesystem::path& path) {
    std::string extension = ToUtf8String(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension;
}

inline std::string ExtensionLower(const std::filesystem::directory_entry& entry) {
    return ExtensionLower(entry.path());
}

inline bool Exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

inline bool IsDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

inline bool CreateDirectories(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::create_directories(path, ec)) {
        return true;
    }
    return !ec;
}

inline bool IsRegularFile(const std::filesystem::directory_entry& entry) {
    std::error_code ec;
    return entry.is_regular_file(ec);
}

inline bool IsDirectory(const std::filesystem::directory_entry& entry) {
    std::error_code ec;
    return entry.is_directory(ec);
}

inline std::filesystem::path RelativePath(const std::filesystem::path& path, const std::filesystem::path& base) {
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(path, base, ec);
    return ec ? path : relative;
}

inline std::filesystem::directory_options SafeDirectoryOptions() {
    return std::filesystem::directory_options::skip_permission_denied;
}

} // namespace cg2::path
