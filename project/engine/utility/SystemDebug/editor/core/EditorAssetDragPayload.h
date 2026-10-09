#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

/// ImGuiのAsset Drag & DropでPathと永続GUIDを同時に渡します。
/// Payload名は既存のMODEL_ASSETを維持し、受信側は旧Path文字列も読めます。
struct EditorAssetDragPayload {
    static constexpr std::uint32_t kMagic = 0x43474144u; // CGAD
    std::uint32_t magic = kMagic;
    char guid[33]{};
    char path[512]{};
};

// 固定長バッファに収まらない文字列は末尾を切り詰め、必ず NUL 終端します。
inline EditorAssetDragPayload MakeEditorAssetDragPayload(
    const std::string& guid,
    const std::string& path) {
    EditorAssetDragPayload payload;
    const std::size_t guidLength = (std::min)(guid.size(), sizeof(payload.guid) - 1);
    const std::size_t pathLength = (std::min)(path.size(), sizeof(payload.path) - 1);
    std::memcpy(payload.guid, guid.data(), guidLength);
    std::memcpy(payload.path, path.data(), pathLength);
    payload.guid[guidLength] = '\0';
    payload.path[pathLength] = '\0';
    return payload;
}

// サイズと識別子が一致する新形式だけを受け付けます。戻り値は入力バッファ内を指します。
inline const EditorAssetDragPayload* TryReadEditorAssetDragPayload(
    const void* data,
    int dataSize) {
    if (!data || dataSize != static_cast<int>(sizeof(EditorAssetDragPayload))) {
        return nullptr;
    }
    const auto* payload = static_cast<const EditorAssetDragPayload*>(data);
    return payload->magic == EditorAssetDragPayload::kMagic ? payload : nullptr;
}

// 旧形式のパス文字列も受け付けます。NUL がなければ payload のバイト数を上限に読みます。
inline std::string ReadEditorAssetDragPath(const void* data, int dataSize) {
    if (const EditorAssetDragPayload* payload = TryReadEditorAssetDragPayload(data, dataSize)) {
        return payload->path;
    }
    if (!data || dataSize <= 0) {
        return {};
    }
    const char* legacyPath = static_cast<const char*>(data);
    const std::size_t maxLength = static_cast<std::size_t>(dataSize);
    const void* terminator = std::memchr(legacyPath, '\0', maxLength);
    const std::size_t length = terminator
        ? static_cast<const char*>(terminator) - legacyPath
        : maxLength;
    return std::string(legacyPath, length);
}

// 旧形式は GUID を持たないため空文字列を返します。
inline std::string ReadEditorAssetDragGuid(const void* data, int dataSize) {
    if (const EditorAssetDragPayload* payload = TryReadEditorAssetDragPayload(data, dataSize)) {
        return payload->guid;
    }
    return {};
}
