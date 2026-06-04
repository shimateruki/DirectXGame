#pragma once
#include <string>
#include <vector>
#include "json.hpp"
#include "EditorCommon.h"

class DebugEditor;
class Object3d;

class SceneSerializer {
public:
    SceneSerializer() = default;
    ~SceneSerializer() = default;

    struct SaveTarget {
        std::string label;
        std::string path;
        nlohmann::json data;
        bool isMetadata = false;
    };

    void Initialize(DebugEditor* editor);

    std::vector<SaveTarget> BuildSceneSaveTargets(const std::string& currentFilename, SaveMode mode);
    std::vector<SaveTarget> BuildSingleObjectSaveTargets(Object3d* object, const std::string& filename);
    std::string SaveTargets(const std::vector<SaveTarget>& targets);

    std::string SaveScene(const std::string& currentFilename, SaveMode mode);
    void UpdateObjectInSceneJSON(Object3d* object, const std::string& filename);

private:
    nlohmann::json SerializeObject(Object3d* obj);
    void SaveToFile(const std::string& path, const nlohmann::json& data);

    DebugEditor* editor_ = nullptr;
}; 
