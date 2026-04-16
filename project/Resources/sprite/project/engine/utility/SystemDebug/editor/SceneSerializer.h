#pragma once
#include <string>
#include "json.hpp"
#include "EditorCommon.h"

class DebugEditor;
class Object3d;

class SceneSerializer {
public:
    SceneSerializer() = default;
    ~SceneSerializer() = default;

    void Initialize(DebugEditor* editor);

    std::string SaveScene(const std::string& currentFilename, SaveMode mode);
    void UpdateObjectInSceneJSON(Object3d* object, const std::string& filename);

private:
    nlohmann::json SerializeObject(Object3d* obj);
    void SaveToFile(const std::string& path, const nlohmann::json& data);

    DebugEditor* editor_ = nullptr;
}; 