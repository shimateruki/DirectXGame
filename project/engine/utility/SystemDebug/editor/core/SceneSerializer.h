#pragma once
#include "EditorCommon.h"
#include "json.hpp"
#include <string>
#include <vector>

class DebugEditor;
namespace engine { class Object3d; }

/// <summary>Editor の Object3d をカテゴリ別 JSON と Scene Asset メタデータへ変換する。</summary>
class SceneSerializer {
public:
    enum class SceneAssetTemplate {
        Empty,
        CurrentScene
    };

    // Scene Asset の識別情報と分割ファイルへの参照。読込済みシーンの Object を所有するものではない。
    struct SceneAssetInfo {
        std::string id;
        std::string filename;
        std::string displayName;
        std::string runtimeScene = "SCENE_EDITOR";
        std::string controllerName = "DEFAULT";
        std::string bgmPath;
        std::string lightPath;
        std::string cameraPath;
        std::string skyboxPath;
        std::string objectLayoutPath;
        std::string spriteLayoutPath;
        bool usesSplitFiles = false;
        bool hasSpriteLayout = false;
        bool usesLegacyMetadata = false;
    };

    // 読み込み定義の診断結果。警告のみなら IsValid は true で、実ゲームの動作保証とは別。
    struct SceneAssetValidationResult {
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        bool IsValid() const { return errors.empty(); }
    };

    SceneSerializer() = default;
    ~SceneSerializer() = default;

    /// <summary>保存前に確認できる、書き込み先とシリアライズ済み JSON の組。</summary>
    struct SaveTarget {
        std::string label;
        std::string path;
        nlohmann::json data;
        bool isMetadata = false;
    };

    /// <summary>
    /// 親となるDebugEditorを登録する。
    /// </summary>
    void Initialize(DebugEditor* editor);

    // 現在のシーンから保存候補を作る。この時点で永続 GUID の重複は解消される。
    std::vector<SaveTarget> BuildSceneSaveTargets(const std::string& currentFilename, SaveMode mode);
    // GUID が一致する既存行を更新し、旧データで GUID がなければ名前で照合する。
    std::vector<SaveTarget> BuildSingleObjectSaveTargets(engine::Object3d* object, const std::string& filename);
    // 返り値は保存対象ラベルの一覧であり、書き込み成功の保証ではない。
    std::string SaveTargets(const std::vector<SaveTarget>& targets);

    // 現在のシーンをシリアライズして保存する。戻り値は保存対象ラベルで、成功判定には使わない。
    std::string SaveScene(const std::string& currentFilename, SaveMode mode);
    // 同じ GUID の行を優先して既存 JSON 内の単一 Object を更新する。
    void UpdateObjectInSceneJSON(engine::Object3d* object, const std::string& filename);

    // Scene Asset は分割 Object JSON と Sprite JSON を一つの単位として扱う。
    std::vector<SceneAssetInfo> DiscoverSceneAssets() const;
    bool CreateSceneAsset(
        const std::string& sceneId,
        const std::string& displayName,
        const std::string& runtimeScene,
        SceneAssetTemplate sceneTemplate,
        std::string& createdFilename,
        std::string& errorMessage);
    bool DuplicateSceneAsset(
        const std::string& sourceFilename,
        const std::string& newSceneId,
        const std::string& displayName,
        std::string& createdFilename,
        std::string& errorMessage);
    // 実行時に参照される Scene ID は変更しない。失敗時は移動済みファイルを戻す。
    bool RenameSceneAsset(
        const std::string& sourceFilename,
        const std::string& newSceneId,
        const std::string& displayName,
        std::string& renamedFilename,
        std::string& errorMessage);
    // 実行時に参照される Scene Asset の削除は拒否する。
    bool DeleteSceneAsset(const std::string& filename, std::string& errorMessage);
    bool SetSceneAssetRuntimeScene(
        const std::string& filename,
        const std::string& runtimeScene,
        std::string& errorMessage);
    bool SetSceneAssetRuntimeSettings(
        const std::string& filename,
        const std::string& controllerName,
        const std::string& bgmPath,
        const std::string& lightPath,
        const std::string& cameraPath,
        const std::string& skyboxPath,
        std::string& errorMessage);
    // errors が空なら利用可能。warnings は継続可能な設定不足を示す。
    SceneAssetValidationResult ValidateSceneAsset(const std::string& filename) const;
    // Scene Asset のメタデータから分割レイアウトの保存先を解決する。
    std::string ResolveSceneAssetObjectPath(const std::string& filename) const;
    std::string ResolveSceneAssetSpritePath(const std::string& filename) const;

private:
    nlohmann::json SerializeObject(engine::Object3d* obj);
    void SaveToFile(const std::string& path, const nlohmann::json& data);

    // DebugEditor本体への参照。SceneSerializerは所有しない。
    DebugEditor* editor_ = nullptr;
};
