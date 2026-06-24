#pragma once
#include "Object3d.h"
#include "json.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

using json = nlohmann::json;

class Object3dCommon;

/// <summary>
/// Object3dの設定をプリセットとして保存、読み込み、生成に使う管理クラス。
/// </summary>
class PresetManager {
public:
    static PresetManager* GetInstance();

    /// <summary>
    /// プリセットファイルを読み込んで初期化する。
    /// </summary>
    void Initialize();

    /// <summary>
    /// 指定ファイルからプリセット一覧を読み込む。
    /// </summary>
    void LoadPresets(const std::string& filename = "Resources/json/preset/presets.json");

    /// <summary>
    /// プリセット一覧を指定ファイルへ保存する。
    /// </summary>
    void SavePresets(const std::string& filename = "Resources/json/preset/presets.json");

    /// <summary>
    /// 指定オブジェクトの設定を新しいプリセットとして登録する。
    /// </summary>
    void AddPresetFromObject(const std::string& presetName, Object3d* obj);

    /// <summary>
    /// 指定オブジェクトへプリセット設定を適用する。
    /// </summary>
    void ApplyPresetToObject(const std::string& presetName, Object3d* obj);

    /// <summary>
    /// プリセットからオブジェクト群を生成する。
    /// </summary>
    std::vector<std::unique_ptr<Object3d>> CreateObjectsFromPreset(const std::string& presetName, Object3dCommon* common) const;

    void RemovePreset(const std::string& presetName);
    void RenamePreset(const std::string& oldName, const std::string& newName);

    const std::map<std::string, json>& GetPresets() const { return presets_; }
    json& GetPreset(const std::string& name) { return presets_[name]; }
    bool HasPreset(const std::string& name) const { return presets_.find(name) != presets_.end(); }

    /// <summary>
    /// 種別ごとの適切なプリセットファイルへまとめて保存する。
    /// </summary>
    void SaveAll();

private:
    std::map<std::string, json> presets_;
};
