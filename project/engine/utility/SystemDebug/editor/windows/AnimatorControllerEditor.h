#pragma once

#include "IEditable.h"
#include "engine/animation/AnimatorController.h"

#include <string>
#include <vector>

class DebugEditor;
class Object3d;
class SceneManager;

// Animator ControllerのState、Transition、Parameterを編集します。
class AnimatorControllerEditor : public IEditable {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void SetPreviewTarget(Object3d* target);
    void DrawImGui() override;
    std::string GetName() override { return "Animator Controller"; }

private:
    void RefreshFiles();
    void NewController();
    void LoadController(const std::string& fileName);
    void SaveController();
    std::string GetAssetPath() const;
    Object3d* ResolvePreviewTarget() const;
    void DrawAssetControls();
    void DrawPreviewTargetControls();
    void DrawStateEditor();
    void DrawParameterEditor();
    void DrawTransitionEditor();
    void RenameState(const std::string& oldName, const std::string& newName);

    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;
    AnimatorControllerAsset controller_;
    std::vector<std::string> files_;
    char fileNameBuffer_[128] = "new_animator";
    std::string previewTargetName_;
    int selectedStateIndex_ = -1;
    int selectedParameterIndex_ = -1;
    int selectedTransitionIndex_ = -1;
};
