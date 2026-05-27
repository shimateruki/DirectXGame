#pragma once

#include "IEditable.h"
#include <string>
#include <vector>

class SceneManager;
class DebugEditor;

class MaterialPreviewBoard : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "Material Preview Board"; }

private:
    struct MaterialPreviewEntry {
        int materialType = 0;
        const char* label = "";
        const char* shortLabel = "";
    };

private:
    void CreateBoard();
    void RemoveBoard();
    int CountBoardObjects() const;
    std::vector<MaterialPreviewEntry> GetEntries() const;

private:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;
    char modelNameBuffer_[128] = "Primitives/sphere";
    float spacing_ = 3.0f;
    int columns_ = 4;
    bool placeNearSelected_ = true;
    bool useEffectPreviewStage_ = true;
};
