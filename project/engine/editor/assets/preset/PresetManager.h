#pragma once
#include "Object3D.h"
#include "json.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace engine { class Object3DCommon; }
class BaseScene;

// 担当：単体Preset・複数ObjectのPrefab・Variantを保存し、生成と差分適用を行う。
// 関連：Object3D（設定対象）、EditorPropertyRegistry（項目）、PresetEditor（UI）
// 主な入口：Initialize / LoadPresets / CreateObjectsFromPreset / CreateObjectsFromPrefab / SaveAll
class PresetManager {
public:
    // 元 Prefab と配置 Instance の同じプロパティの値。安定した propertyPath で比較・適用する。
    // 関連：PresetManager（この型を使う機能の本体）
    struct PrefabPropertyOverride {
        std::string propertyPath;
        std::string displayName;
        json sourceValue;
        json instanceValue;
    };

    // 元 Prefab と配置 Instance の Component 有無の差。値の差分とは分けて扱う。
    // 関連：PresetManager（この型を使う機能の本体）
    struct PrefabComponentOverride {
        std::string componentTypeId;
        std::string displayName;
        bool sourcePresent = false;
        bool instancePresent = false;
    };

    // 基底 Prefab と Variant の同一プロパティを比較した値の組。propertyPath が適用先を識別する。
    // 関連：PresetManager（この型を使う機能の本体）
    struct PrefabVariantOverride {
        std::string propertyPath;
        std::string displayName;
        json baseValue;
        json variantValue;
    };

    // 基底と Variant の Component 有無の差。追加・削除をプロパティ値の変更と区別する。
    // 関連：PresetManager（この型を使う機能の本体）
    struct PrefabVariantComponentOverride {
        std::string componentTypeId;
        std::string displayName;
        bool basePresent = false;
        bool variantPresent = false;
    };

    // 追加・削除・親変更など構造差分の集計。個々の差分本体を保持するデータではない。
    // 関連：PresetManager（この型を使う機能の本体）
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
    void AddPresetFromObject(const std::string& presetName, engine::Object3D* obj);

    /// <summary>
    /// 指定ObjectへPreset設定を適用する。
    /// </summary>
    void ApplyPresetToObject(const std::string& presetName, engine::Object3D* obj);

    /// <summary>
    /// PresetからObject群を生成する。
    /// </summary>
    std::vector<std::unique_ptr<engine::Object3D>> CreateObjectsFromPreset(const std::string& presetName, engine::Object3DCommon* common) const;

    // 指定名のPresetを管理一覧から取り除く。
    void RemovePreset(const std::string& presetName);

    /// 指定した複数のPresetを削除し、変更があった場合だけ一度保存する。
    void RemovePresets(const std::vector<std::string>& presetNames);

    // 名前衝突による上書きを防ぎ、キーだけを変更して内容を保持する。
    void RenamePreset(const std::string& oldName, const std::string& newName);

    const std::map<std::string, json>& GetPresets() const { return presets_; }

    // 未登録名なら空の定義を新規作成する。存在確認だけならHasPresetを使う。
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
    void AddPrefabFromObject(const std::string& prefabName, engine::Object3D* obj);

    /// Prefab Modeで編集した階層をAssetへ保存する。Variantでは基底との差分へ変換する。
    bool UpdatePrefabFromObject(const std::string& prefabName, engine::Object3D* obj);

    /// Prefab Asset更新前後を比較し、Scene内の未変更Instanceだけを新しいSourceへ同期する。
    int SynchronizePrefabInstances(
        const std::map<std::string, json>& beforePrefabs,
        BaseScene& scene,
        engine::Object3DCommon* common);

    /// コピー型Presetを、リンク型Prefab Assetへ明示的に変換する。
    bool CreatePrefabFromPreset(const std::string& presetName, const std::string& prefabName);

    /// 基底Prefabとの差分だけを保持するVariantを作成する。
    bool CreatePrefabVariant(const std::string& basePrefabName, const std::string& variantName);

    bool IsPrefabVariant(const std::string& prefabName) const;

    std::string GetPrefabBaseName(const std::string& prefabName) const;

    // 基底PrefabとVariantの継承結果を更新する。
    void RefreshPrefabInheritance();

    /// <summary>
    /// PrefabからObject群を生成する。
    /// </summary>
    std::vector<std::unique_ptr<engine::Object3D>> CreateObjectsFromPrefab(const std::string& prefabName, engine::Object3DCommon* common) const;

    // Instanceの配置差分やComponent構造差分を除き、SourceとのProperty差分を返す。
    std::vector<PrefabPropertyOverride> GetPrefabOverrides(const engine::Object3D* object) const;

    std::vector<PrefabComponentOverride> GetPrefabComponentOverrides(const engine::Object3D* object) const;

    std::vector<PrefabVariantOverride> GetPrefabVariantOverrides(const engine::Object3D* object) const;

    std::vector<PrefabVariantComponentOverride> GetPrefabVariantComponentOverrides(const engine::Object3D* object) const;

    PrefabStructureOverrideSummary GetPrefabStructureOverrideSummary(const std::string& prefabName) const;

    bool HasValidPrefabSource(const engine::Object3D* object) const;

    // Instanceの値をSourceへ反映し、Source値のままの別Instanceだけ更新する。
    // 名前と配置専用PropertyはSourceへ反映しない。
    bool ApplyPrefabProperty(engine::Object3D* object, const std::string& propertyPath, const std::vector<engine::Object3D*>& sceneObjects);

    // インスタンス固有の名前・配置値を避け、元ノードの値だけ復元する。
    bool RevertPrefabProperty(engine::Object3D* object, const std::string& propertyPath);

    // Component の追加・削除を構造差分へ保存し、必要なら内部 Property も維持する。
    bool ApplyPrefabComponent(engine::Object3D* object, const std::string& componentTypeId, const std::vector<engine::Object3D*>& sceneObjects);

    // 配置個体の Component 有無を、解決済みの Prefab 元ノードへ戻す。
    bool RevertPrefabComponent(engine::Object3D* object, const std::string& componentTypeId);

    // 派生側の明示的な Property 上書きを外し、元 Prefab の有効値を再継承する。
    bool RevertPrefabVariantProperty(
        engine::Object3D* object,
        const std::string& propertyPath,
        const std::vector<engine::Object3D*>& sceneObjects);

    // 派生 Component の存在差分と内部 Property 差分を消し、元構造へ戻す。
    bool RevertPrefabVariantComponent(
        engine::Object3D* object,
        const std::string& componentTypeId,
        const std::vector<engine::Object3D*>& sceneObjects);

    // 同じinstanceIdの階層全体へ適用し、個別操作をひとつのAsset履歴にまとめる。
    int ApplyAllPrefabOverrides(engine::Object3D* object, const std::vector<engine::Object3D*>& sceneObjects);

    // 同じ instanceId の階層を回り、構造差分と Property 差分を元へ戻す。
    int RevertAllPrefabOverrides(engine::Object3D* object, const std::vector<engine::Object3D*>& sceneObjects);

    // 同じinstanceIdの階層からPrefabへの紐付けを外す。戻り値は解除したObject数。
    int UnpackPrefabInstance(engine::Object3D* object, const std::vector<engine::Object3D*>& sceneObjects);

    // 同じ旧 instanceId を持つ複製群には同じ新 ID を割り当て、群のまとまりを保つ。
    void AssignNewPrefabInstanceId(const std::vector<engine::Object3D*>& objects) const;

    // 未登録名ではディスクに触れず、削除が成立したときだけ再保存する。
    void RemovePrefab(const std::string& prefabName);

    // 派生関係は assetId で維持し、表示名の変更だけを保存する。
    void RenamePrefab(const std::string& oldName, const std::string& newName);

    const std::map<std::string, json>& GetPrefabs() const { return prefabs_; }

    // 未登録名なら空の定義を新規作成する。存在確認だけならHasPrefabを使う。
    json& GetPrefab(const std::string& name) { return prefabs_[name]; }

    bool HasPrefab(const std::string& name) const { return prefabs_.find(name) != prefabs_.end(); }

    /// <summary>
    /// Preset/Prefabをまとめて保存する。
    /// </summary>
    void SaveAll();

private:
    // Instanceの元IDに対応するPrefabノードを返す。なければnullptr。
    json* FindPrefabSourceNode(const engine::Object3D::PrefabInstanceInfo& info);

    // Instanceの元IDに対応するPrefabノードを返す。なければnullptr。
    const json* FindPrefabSourceNode(const engine::Object3D::PrefabInstanceInfo& info) const;

    std::map<std::string, json> presets_;
    std::map<std::string, json> prefabs_;
    bool suppressPrefabAssetTransaction_ = false;
};
