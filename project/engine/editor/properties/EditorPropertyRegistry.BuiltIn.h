#pragma once

// 担当：組込Property登録の共通フラグと補助定義を提供する。
// 関連：EditorPropertyRegistryの登録用分割.cpp

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
