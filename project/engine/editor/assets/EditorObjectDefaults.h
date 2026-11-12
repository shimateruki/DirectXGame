#pragma once

// 担当：Editorで生成するObjectへ共通の初期設定を適用する。
// 関連：Object3D / Collider（設定先）、DebugEditor（生成）
// 主な入口：ApplyGroundCollision

namespace engine { class Object3D; }

namespace EditorObjectDefaults {
// 通常作成・プリセット作成で共通の地形初期値。明示済みの設定は変更しない。
void ApplyGroundCollision(engine::Object3D* object);
}
