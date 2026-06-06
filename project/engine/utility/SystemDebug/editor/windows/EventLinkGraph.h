#pragma once

#include "IEditable.h"
#include <string>
#include <vector>

class DebugEditor;
class Object3d;
class SceneManager;

class EventLinkGraph : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "Event Link Graph"; }

private:
    struct NodeInfo {
        Object3d* object = nullptr;
        std::string name;
        int eventID = -1;
        int targetID = -1;
        bool hasMissingTarget = false;
        bool hasDuplicateID = false;
    };

    void CollectNodes();
    bool HasEventID(int id, Object3d* ignoreObject) const;
    int CountEventID(int id) const;

    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;
    std::vector<NodeInfo> nodes_;
    bool showOnlyLinked_ = false;
};
