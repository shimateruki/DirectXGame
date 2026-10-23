#pragma once

namespace engine { class Object3d; }

namespace EditorObjectDefaults {
// 通常作成・プリセット作成で共通の地形初期値。明示済みの設定は変更しない。
void ApplyGroundCollision(engine::Object3d* object);
}
