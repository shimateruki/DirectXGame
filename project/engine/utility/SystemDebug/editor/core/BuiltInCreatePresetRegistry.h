#pragma once

class Object3dCommon;

// BuiltInCreatePresetRegistryは、エディタで最初から使える生成プリセットを登録する補助クラスです。
class BuiltInCreatePresetRegistry {
public:
        // 未登録の場合だけ組み込みプリセットをPresetManagerへ追加します。
static void EnsureRegistered(Object3dCommon* common);
};
