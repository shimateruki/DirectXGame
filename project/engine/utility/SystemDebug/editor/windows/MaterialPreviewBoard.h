#pragma once

#include "IEditable.h"
#include <string>
#include <vector>

class SceneManager;
class DebugEditor;
class Object3d;

class MaterialPreviewBoard : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "Material Preview Board"; }

private:
    struct MaterialPreviewEntry {
        int materialType = 0;
        float effectType = 0.0f;
        std::string label;
        std::string shortLabel;
        std::string modeLabel;
    };

private:
    void CreateBoard();
    void RemoveBoard();
    int CountBoardObjects() const;
    std::vector<MaterialPreviewEntry> GetEntries() const;
    void RefreshModelCandidates();
    void SetPreviewModel(const std::string& modelName);
    void DrawModelSelector();
    void ApplyPreviewDefaults(Object3d* object, const MaterialPreviewEntry& entry) const;

private:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;
    char modelNameBuffer_[128] = "Primitives/sphere";
    float spacing_ = 3.0f;
    int columns_ = 4;
    bool placeNearSelected_ = true;
    bool useEffectPreviewStage_ = true;
    bool expandModeVariants_ = true;
    bool showOnlySpecialMaterials_ = false;
    std::vector<std::string> modelCandidates_;
    int selectedModelIndex_ = 0;
};
