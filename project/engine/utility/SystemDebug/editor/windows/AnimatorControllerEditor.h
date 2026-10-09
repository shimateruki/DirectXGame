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
    // プレビュー対象の名前を記録します。シーンが変わった後の生ポインタは保持しません。
    void SetPreviewTarget(Object3d* target);
    void DrawImGui() override;
    std::string GetName() override { return "Animator Controller"; }

private:
    void RefreshFiles();
    void NewController();
    void LoadController(const std::string& fileName);
    void SaveController();
    std::string GetAssetPath() const;
    // 現在シーンから対象を再検索します。削除済みなら nullptr を返します。
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
    std::vector<std::string> bodyAnimationFiles_;
    char fileNameBuffer_[128] = "new_animator";
    std::string previewTargetName_;
    int selectedStateIndex_ = -1;
    int selectedParameterIndex_ = -1;
    int selectedTransitionIndex_ = -1;
};
