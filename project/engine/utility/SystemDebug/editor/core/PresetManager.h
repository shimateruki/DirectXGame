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
/// Object3dの設定をPreset/Prefabとして保存し、再生成するための管理クラス。
/// </summary>
// PresetManagerは、単体プリセットと複数オブジェクトPrefabの読み書き、生成を管理します。
class PresetManager {
public:
    static PresetManager* GetInstance();

    /// <summary>
    /// PresetとPrefabの定義ファイルを読み込んで初期化する。
    /// </summary>
    void Initialize();

    /// <summary>
    /// 指定ファイルからPreset一覧を読み込む。
    /// </summary>
    void LoadPresets(const std::string& filename = "Resources/json/preset/presets.json");

    /// <summary>
    /// Preset一覧を指定ファイルへ保存する。
    /// </summary>
    void SavePresets(const std::string& filename = "Resources/json/preset/presets.json");

    /// <summary>
    /// 指定Objectの設定を新しいPresetとして登録する。
    /// </summary>
    void AddPresetFromObject(const std::string& presetName, Object3d* obj);

    /// <summary>
    /// 指定ObjectへPreset設定を適用する。
    /// </summary>
    void ApplyPresetToObject(const std::string& presetName, Object3d* obj);

    /// <summary>
    /// PresetからObject群を生成する。
    /// </summary>
    std::vector<std::unique_ptr<Object3d>> CreateObjectsFromPreset(const std::string& presetName, Object3dCommon* common) const;

    void RemovePreset(const std::string& presetName);
    void RenamePreset(const std::string& oldName, const std::string& newName);

    const std::map<std::string, json>& GetPresets() const { return presets_; }
    json& GetPreset(const std::string& name) { return presets_[name]; }
    bool HasPreset(const std::string& name) const { return presets_.find(name) != presets_.end(); }

    /// <summary>
    /// Prefab v1定義を読み込む。Presetとは別ファイルで管理する。
    /// </summary>
    void LoadPrefabs(const std::string& filename = "Resources/json/prefab/prefabs.json");

    /// <summary>
    /// Prefab v1定義を保存する。
    /// </summary>
    void SavePrefabs(const std::string& filename = "Resources/json/prefab/prefabs.json");

    /// <summary>
    /// 選択Object階層をPrefabとして登録する。
    /// </summary>
    void AddPrefabFromObject(const std::string& prefabName, Object3d* obj);

    /// <summary>
    /// PrefabからObject群を生成する。
    /// </summary>
    std::vector<std::unique_ptr<Object3d>> CreateObjectsFromPrefab(const std::string& prefabName, Object3dCommon* common) const;

    void RemovePrefab(const std::string& prefabName);
    void RenamePrefab(const std::string& oldName, const std::string& newName);

    const std::map<std::string, json>& GetPrefabs() const { return prefabs_; }
    bool HasPrefab(const std::string& name) const { return prefabs_.find(name) != prefabs_.end(); }

    /// <summary>
    /// Preset/Prefabをまとめて保存する。
    /// </summary>
    void SaveAll();

private:
    std::map<std::string, json> presets_;
    std::map<std::string, json> prefabs_;
};