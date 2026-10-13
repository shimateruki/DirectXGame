#pragma once

class Object3dCommon;

// ゲーム固有の組み込み生成プリセットをエディターへ登録します。
class BuiltInCreatePresetRegistry {
  public:
    // 未登録の場合だけ組み込みプリセットをPresetManagerへ追加します。
    static void EnsureRegistered(Object3dCommon* common);
};
