#pragma once

// 担当：Editorで共有するImGuiとギズモ関連のインクルードをまとめる。
// 関連：各Editorの実装、ImGui / ImGuizmo（UI・操作）

// エディタ全体で使う共通の列挙型
enum class SaveMode {
    All,
    Player,
    Enemy,
    Object,
    Camera
};

#ifdef USE_IMGUI
#include "engine/utility/math/Math.h"
namespace EditorCommon {
// worldを位置・ラジアン回転・スケールへ分解する。出力はすべてワールド基準。
void DecomposeWorldPose(const engine::Matrix4x4& world, engine::Vector3& position,
    engine::Vector3& rotation, engine::Vector3& scale);
}
#endif
