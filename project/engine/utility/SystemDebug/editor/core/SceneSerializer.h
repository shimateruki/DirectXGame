#pragma once
#include "EditorCommon.h"
#include "json.hpp"
#include <string>
#include <vector>

class DebugEditor;
class Object3d;

/// <summary>
/// Editor上のシーンや単体オブジェクトをJSONへ保存する処理をまとめる。
/// </summary>
// SceneSerializerは、エディタ上のObject3dをシーンJSONへ保存、更新するための変換処理を担当します。
class SceneSerializer {
public:
    enum class SceneAssetTemplate {
        Empty,
        CurrentScene
    };

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

    struct SceneAssetValidationResult {
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        bool IsValid() const { return errors.empty(); }
    };

    SceneSerializer() = default;
    ~SceneSerializer() = default;

    /// <summary>
    /// 保存対象となるJSONデータと出力先パスの組。
    /// </summary>
        // 保存先ファイルと保存対象オブジェクトをひとまとまりにした情報です。
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

    std::vector<SaveTarget> BuildSceneSaveTargets(const std::string& currentFilename, SaveMode mode);
    std::vector<SaveTarget> BuildSingleObjectSaveTargets(Object3d* object, const std::string& filename);
    std::string SaveTargets(const std::vector<SaveTarget>& targets);

    std::string SaveScene(const std::string& currentFilename, SaveMode mode);
    void UpdateObjectInSceneJSON(Object3d* object, const std::string& filename);

    // Scene Assetは、分割Object JSONとSprite JSONを1つのシーンとして扱います。
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
    bool RenameSceneAsset(
        const std::string& sourceFilename,
        const std::string& newSceneId,
        const std::string& displayName,
        std::string& renamedFilename,
        std::string& errorMessage);
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
    SceneAssetValidationResult ValidateSceneAsset(const std::string& filename) const;
    std::string ResolveSceneAssetObjectPath(const std::string& filename) const;
    std::string ResolveSceneAssetSpritePath(const std::string& filename) const;

private:
    nlohmann::json SerializeObject(Object3d* obj);
    void SaveToFile(const std::string& path, const nlohmann::json& data);

    // DebugEditor本体への参照。SceneSerializerは所有しない。
    DebugEditor* editor_ = nullptr;
};
