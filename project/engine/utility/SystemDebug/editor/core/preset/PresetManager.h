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
    struct PrefabPropertyOverride {
        std::string propertyPath;
        std::string displayName;
        json sourceValue;
        json instanceValue;
    };

    struct PrefabComponentOverride {
        std::string componentTypeId;
        std::string displayName;
        bool sourcePresent = false;
        bool instancePresent = false;
    };

    struct PrefabVariantOverride {
        std::string propertyPath;
        std::string displayName;
        json baseValue;
        json variantValue;
    };

    struct PrefabVariantComponentOverride {
        std::string componentTypeId;
        std::string displayName;
        bool basePresent = false;
        bool variantPresent = false;
    };

    struct PrefabStructureOverrideSummary {
        int addedObjects = 0;
        int removedObjects = 0;
        int reparentedObjects = 0;
        int rawNodeOverrides = 0;
        int componentOverrides = 0;

        bool HasOverrides() const {
            return addedObjects > 0 || removedObjects > 0 ||
                reparentedObjects > 0 || rawNodeOverrides > 0 || componentOverrides > 0;
        }
    };

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
    /// Prefab定義を読み込む。v1はメモリ上でv2へ移行する。
    /// </summary>
    void LoadPrefabs(const std::string& filename = "Resources/json/prefab/prefabs.json");

    /// <summary>
    /// Prefab v3定義を保存する。
    /// </summary>
    void SavePrefabs(const std::string& filename = "Resources/json/prefab/prefabs.json");

    /// <summary>
    /// 選択Object階層をPrefabとして登録する。
    /// </summary>
    void AddPrefabFromObject(const std::string& prefabName, Object3d* obj);

    /// Prefab Modeで編集した階層をAssetへ保存する。Variantでは基底との差分へ変換する。
    bool UpdatePrefabFromObject(const std::string& prefabName, Object3d* obj);

    /// Prefab Asset更新前後を比較し、Scene内の未変更Instanceだけを新しいSourceへ同期する。
    int SynchronizePrefabInstances(
        const std::map<std::string, json>& beforePrefabs,
        std::vector<std::unique_ptr<Object3d>>& sceneObjects,
        Object3dCommon* common);

    /// コピー型Presetを、リンク型Prefab Assetへ明示的に変換する。
    bool CreatePrefabFromPreset(const std::string& presetName, const std::string& prefabName);

    /// 基底Prefabとの差分だけを保持するVariantを作成する。
    bool CreatePrefabVariant(const std::string& basePrefabName, const std::string& variantName);
    bool IsPrefabVariant(const std::string& prefabName) const;
    std::string GetPrefabBaseName(const std::string& prefabName) const;
    void RefreshPrefabInheritance();

    /// <summary>
    /// PrefabからObject群を生成する。
    /// </summary>
    std::vector<std::unique_ptr<Object3d>> CreateObjectsFromPrefab(const std::string& prefabName, Object3dCommon* common) const;

    std::vector<PrefabPropertyOverride> GetPrefabOverrides(const Object3d* object) const;
    std::vector<PrefabComponentOverride> GetPrefabComponentOverrides(const Object3d* object) const;
    std::vector<PrefabVariantOverride> GetPrefabVariantOverrides(const Object3d* object) const;
    std::vector<PrefabVariantComponentOverride> GetPrefabVariantComponentOverrides(const Object3d* object) const;
    PrefabStructureOverrideSummary GetPrefabStructureOverrideSummary(const std::string& prefabName) const;
    bool HasValidPrefabSource(const Object3d* object) const;
    bool ApplyPrefabProperty(Object3d* object, const std::string& propertyPath, const std::vector<Object3d*>& sceneObjects);
    bool RevertPrefabProperty(Object3d* object, const std::string& propertyPath);
    bool ApplyPrefabComponent(Object3d* object, const std::string& componentTypeId, const std::vector<Object3d*>& sceneObjects);
    bool RevertPrefabComponent(Object3d* object, const std::string& componentTypeId);
    bool RevertPrefabVariantProperty(
        Object3d* object,
        const std::string& propertyPath,
        const std::vector<Object3d*>& sceneObjects);
    bool RevertPrefabVariantComponent(
        Object3d* object,
        const std::string& componentTypeId,
        const std::vector<Object3d*>& sceneObjects);
    int ApplyAllPrefabOverrides(Object3d* object, const std::vector<Object3d*>& sceneObjects);
    int RevertAllPrefabOverrides(Object3d* object, const std::vector<Object3d*>& sceneObjects);
    int UnpackPrefabInstance(Object3d* object, const std::vector<Object3d*>& sceneObjects);
    void AssignNewPrefabInstanceId(const std::vector<Object3d*>& objects) const;

    void RemovePrefab(const std::string& prefabName);
    void RenamePrefab(const std::string& oldName, const std::string& newName);

    const std::map<std::string, json>& GetPrefabs() const { return prefabs_; }
    json& GetPrefab(const std::string& name) { return prefabs_[name]; }
    bool HasPrefab(const std::string& name) const { return prefabs_.find(name) != prefabs_.end(); }

    /// <summary>
    /// Preset/Prefabをまとめて保存する。
    /// </summary>
    void SaveAll();

private:
    json* FindPrefabSourceNode(const Object3d::PrefabInstanceInfo& info);
    const json* FindPrefabSourceNode(const Object3d::PrefabInstanceInfo& info) const;

    std::map<std::string, json> presets_;
    std::map<std::string, json> prefabs_;
    bool suppressPrefabAssetTransaction_ = false;
};
