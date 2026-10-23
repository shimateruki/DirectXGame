#pragma once
#include <string>
class BaseScene;

// 基本値の編集・保存と通常Sceneへの適用をゲーム側で担当します。
namespace CharacterStatusEditor {
void Register();
// 空のtypeはPlayer、"*"は全種類。Editor内部のダミーや複製には適用しません。
int Apply(BaseScene* scene, const std::string& type);
}
