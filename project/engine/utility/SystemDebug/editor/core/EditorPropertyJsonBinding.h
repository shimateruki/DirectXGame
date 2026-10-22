#pragma once
#include "EditorPropertyRegistry.h"
#include "Object3d.h"

namespace EditorPropertyJsonBinding {
// 省略されたparamへ前のPresetの値を持ち越さず、生成時の既定値から読み直します。
inline void Restore(engine::Object3d& object, const nlohmann::json& defaults, const nlohmann::json& source) {
    object.param_.reset();
    object.ImportFromJson(defaults);
    object.ImportFromJson(source);
}

// 実クラスで変更を適用し、変わった保存値だけをPresetへ反映します。
// Export全体で置き換えないため、未知項目・children・Prefab情報を保持できます。
inline bool Apply(engine::Object3d& object, const EditorPropertyDescriptor& property,
                  nlohmann::json& source, const nlohmann::json& value) {
    const auto before = object.ExportToJson();
    if (!EditorPropertyRegistry::GetInstance()->SetValue(&object, property.path, value)) return false;
    const auto changes = nlohmann::json::diff(before, object.ExportToJson());
    for (const auto& change : changes) {
        const nlohmann::json::json_pointer path(change.at("path").get<std::string>());
        if (change.at("op") != "remove") {
            // 新しい親Objectを追加する差分でも、Preset側にある未知の子キーは保持します。
            const auto& incoming = change.at("value");
            if (change.at("op") == "add" && incoming.is_object() && source.contains(path) && source[path].is_object())
                source[path].update(incoming, true);
            else
                source[path] = incoming;
        } else if (source.contains(path.parent_pointer())) {
            auto& parent = source[path.parent_pointer()];
            if (parent.is_object()) parent.erase(path.back());
            else if (parent.is_array()) {
                const auto index = static_cast<std::size_t>(std::stoull(path.back()));
                if (index < parent.size()) parent.erase(index);
            }
        }
    }
    return !changes.empty();
}
} // namespace EditorPropertyJsonBinding
