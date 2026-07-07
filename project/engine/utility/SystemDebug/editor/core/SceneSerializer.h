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

private:
    nlohmann::json SerializeObject(Object3d* obj);
    void SaveToFile(const std::string& path, const nlohmann::json& data);

    // DebugEditor本体への参照。SceneSerializerは所有しない。
    DebugEditor* editor_ = nullptr;
};
