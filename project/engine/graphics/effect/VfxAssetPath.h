#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace VfxAssetPath {

// 旧アセット名は同じ素材種別内で末尾名が一意な場合だけ解決する。
template <class Map>
auto FindPreset(Map& presets, const std::string& key) {
    const auto exact = presets.find(key);
    if (exact != presets.end() || key.find_first_of("/\\") != std::string::npos) return exact;
    auto found = presets.end();
    for (auto it = presets.begin(); it != presets.end(); ++it) {
        const std::string_view name(it->first);
        const auto slash = name.find_last_of('/');
        if (slash == name.npos || name.substr(slash + 1) != key) continue;
        if (found != presets.end()) return presets.end();
        found = it;
    }
    return found;
}

inline std::filesystem::path Resolve(const std::filesystem::path& root, const std::filesystem::path& requested) {
    namespace fs = std::filesystem;
    const auto relative = requested.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty() || relative.has_parent_path() || relative.extension() != ".json") return requested;
    std::error_code error;
    if (fs::is_regular_file(requested, error)) return requested;

    fs::path found;
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (it->path().filename() != relative || !it->is_regular_file(error)) continue;
        if (!found.empty()) return requested;
        found = it->path();
    }
    return error || found.empty() ? requested : found;
}

// シーンの先読みでも同じ旧パス解決を使う。
inline std::filesystem::path ResolveDependency(const std::filesystem::path& path) {
    const auto root = path.parent_path();
    if (root.parent_path() != std::filesystem::path("Resources/json")) return path;
    const auto kind = root.filename();
    if (kind == "gpu_particles" || kind == "particle" || kind == "effect" || kind == "debris" ||
        kind == "vfx_sequence" || kind == "trail_emitter") return Resolve(root, path);
    return path;
}

} // namespace VfxAssetPath
