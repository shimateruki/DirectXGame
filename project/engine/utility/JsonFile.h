#pragma once

#include "json_fwd.hpp"
#include <filesystem>
#include <initializer_list>

namespace JsonFile {
// 既存オブジェクトの指定キーだけを更新する。欠損・不正JSONは空から再作成する。
// 他のキーは保持し、読書きに失敗した場合はfalseを返す。
// valuesはObject型が必要。removedKeysは値の更新後に削除するので、同名キーは最終的に残らない。
bool UpdateObject(const std::filesystem::path& path, const nlohmann::json& values,
    std::initializer_list<const char*> removedKeys = {});
}
