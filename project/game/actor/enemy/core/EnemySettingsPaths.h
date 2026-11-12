#pragma once

#include <string>
#include <string_view>

// 敵タイプから設定フォルダーと互換読込用の名前を決める。JSONの読み書きは各設定クラスが担当する。
namespace EnemySettingsPaths {

// ファイル名として扱える敵タイプだけを変換する。例: FalseKingSlime → false_king_slime。
inline std::string FolderName(std::string_view enemyType) {
    std::string name;
    for (char character : enemyType) {
        if (character >= 'A' && character <= 'Z') {
            if (!name.empty()) {
                name += '_';
            }
            name += static_cast<char>(character - 'A' + 'a');
        } else if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')) {
            name += character;
        } else {
            return {};
        }
    }
    return name;
}

inline std::string Directory(std::string_view enemyType) {
    const std::string name = FolderName(enemyType);
    return name.empty() ? std::string{} : "Resources/json/enemy/" + name + "/";
}

} // namespace EnemySettingsPaths
