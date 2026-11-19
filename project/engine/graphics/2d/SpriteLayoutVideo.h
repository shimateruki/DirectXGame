#pragma once

#include "Sprite.h"
#include "TextureManager.h"
#include "VideoSpritePlayer.h"

#include <filesystem>

// レイアウト中の動画を既存の再生器につなぎ、映像の準備中は同じSpriteの静止画を使います。
namespace SpriteLayoutVideo {

inline std::filesystem::path Resolve(const std::string& value) {
    if (value.empty())
        return {};
    const std::filesystem::path input(std::u8string(value.begin(), value.end()));
    if (input.has_root_path() || input.extension() != L".mp4")
        return {};
    std::error_code error;
    const auto root = std::filesystem::canonical("Resources/video", error);
    if (error)
        return {};
    const auto file = std::filesystem::canonical(root / input, error);
    if (error || !std::filesystem::is_regular_file(file, error))
        return {};
    const auto relative = file.lexically_relative(root);
    return relative.empty() || relative.is_absolute() || *relative.begin() == L".." ? std::filesystem::path{} : file;
}

// 画像の切替で位置とサイズは維持し、UVだけを転送先の画像全体へ合わせます。
inline void SetTexture(engine::Sprite& sprite, uint32_t handle) {
    if (handle == 0)
        return;
    const auto& texture = TextureManager::GetInstance()->GetMetadata(handle);
    sprite.SetTextureHandle(handle);
    sprite.SetTextureRect({0, 0}, {static_cast<float>(texture.width), static_cast<float>(texture.height)});
}

} // namespace SpriteLayoutVideo
