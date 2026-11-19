#include "engine/utility/JsonFile.h"
#include "json.hpp"

#include <fstream>
#include <string>
#include <system_error>

bool JsonFile::UpdateObject(const std::filesystem::path& path, const nlohmann::json& values,
    std::initializer_list<const char*> removedKeys) {
    // キー単位の更新はオブジェクト同士に限ります。条件を満たさない場合はファイルを開く前に終了します。
    if (!values.is_object()) {
        return false;
    }

    nlohmann::json root = nlohmann::json::object();
    {
        std::ifstream input(path);
        if (input.is_open()) {
            try {
                input >> root;
            } catch (const nlohmann::json::parse_error&) {
                // 構文を読めない既存内容は空のオブジェクトとして組み立て直します。
                root = nlohmann::json::object();
            }
            if (input.bad()) {
                return false;
            }
        } else {
            // 既存ファイルの読込失敗を、新規ファイルと誤認して上書きしない。
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            if (exists || error) {
                return false;
            }
        }
    }

    if (!root.is_object()) {
        root = nlohmann::json::object();
    }
    // 指定キーだけを更新して他のキーを保持します。削除指定を後に処理するため、両方にあるキーは削除されます。
    root.update(values);
    for (const char* key : removedKeys) {
        root.erase(key);
    }

    // 文字列の不正などによる変換失敗は、ファイルを開く前に検出する。
    std::string text;
    try {
        text = root.dump(4);
    } catch (const nlohmann::json::exception&) {
        return false;
    }

    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }
    std::ofstream output(path);
    if (!output.is_open()) {
        return false;
    }
    output << text;
    // 書き込み後のフラッシュとクローズも済ませてからストリーム状態を返します。
    output.flush();
    output.close();
    return output.good();
}
