#pragma once
#include <string>
#include <map>
#include "json.hpp"
#include "Object3d.h"

using json = nlohmann::json;

class PresetManager {
public:
    static PresetManager* GetInstance();

    // ファイルから全プリセットを読み込む
    void LoadPresets(const std::string& filename = "Resources/json/preset/presets.json");

    // 全プリセットをファイルに保存する
    void SavePresets(const std::string& filename = "Resources/json/preset/presets.json");

    // 指定したオブジェクトの設定を、新しいプリセットとして登録
    void AddPresetFromObject(const std::string& presetName, Object3d* obj);

    // 指定したオブジェクトに、プリセットの設定を適用する
    void ApplyPresetToObject(const std::string& presetName, Object3d* obj);

    // プリセットの削除
    void RemovePreset(const std::string& presetName);

    // プリセットの名前変更
    void RenamePreset(const std::string& oldName, const std::string& newName);

    // プリセット名の一覧を取得（ImGui用）
    const std::map<std::string, json>& GetPresets() const { return presets_; }

private:
    std::map<std::string, json> presets_;
};