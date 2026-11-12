#pragma once

namespace engine { class Object3DCommon; }

// ゲーム固有の組み込み生成プリセットをエディターへ登録します。
// 関連：Editorの生成一覧に、ゲーム固有の配置プリセットを初回登録する。
// 主な入口：EnsureRegistered
class BuiltInCreatePresetRegistry {
  public:
    // 未登録の場合だけ組み込みプリセットをPresetManagerへ追加します。
    static void EnsureRegistered(engine::Object3DCommon* common);
};
