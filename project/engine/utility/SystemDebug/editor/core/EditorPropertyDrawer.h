#pragma once

#include "EditorPropertyRegistry.h"

/// InspectorとProperty Matrixで共有するProperty描画設定です。
struct EditorPropertyDrawOptions {
    bool compact = false;
    bool mixed = false;
    float width = 0.0f;
};

namespace EditorPropertyDrawer {

/// Registryの型情報とUI Hintから、1つのImGui入力欄を描画します。
/// 戻り値がtrueの場合、valueには編集後のJSON値が入ります。
bool DrawValue(
    const EditorPropertyDescriptor& property,
    nlohmann::json& value,
    const char* id,
    const EditorPropertyDrawOptions& options = {});

}
