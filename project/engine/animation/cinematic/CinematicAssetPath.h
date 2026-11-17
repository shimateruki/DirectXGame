#pragma once

#include <filesystem>

namespace CinematicAssetPath {

// 実在する指定先を優先し、旧形式のルート直下の名前だけをシーン別フォルダーから探す。
inline std::filesystem::path Resolve(const std::filesystem::path& root, const std::filesystem::path& requested) {
    namespace fs = std::filesystem;
    std::error_code error;
    if (fs::is_regular_file(requested, error)) return requested;
    const auto relative = requested.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty() || relative.has_parent_path() || relative.extension() != ".json") return requested;

    // 同名の候補が複数あれば誤った演出を再生しない。
    fs::path found;
    for (const auto* folder : {"stage1", "stage2", "stage3", "stage_select", "tutorial", "shared", "samples"}) {
        const auto candidate = root / folder / relative;
        if (!fs::is_regular_file(candidate, error)) continue;
        if (!found.empty()) return requested;
        found = candidate;
    }
    return found.empty() ? requested : found;
}

} // namespace CinematicAssetPath
