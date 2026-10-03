#pragma once

#include "EditorPropertyRegistry.h"

namespace EditorPropertyBuiltIn {

// 複数編集とPrefab差分の共通フラグです。アニメーション対応だけ個別に追加します。
inline EditorPropertyFlags CommonFlags(bool animatable = false) {
    EditorPropertyFlags flags = EditorPropertyFlags::MultiEdit | EditorPropertyFlags::PrefabOverride;
    if (animatable) {
        flags = flags | EditorPropertyFlags::Animatable;
    }
    return flags;
}

} // namespace EditorPropertyBuiltIn
