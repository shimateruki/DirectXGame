#pragma once

#include <string>

class DebugEditor;

// Hierarchy内のScene Asset一覧と操作ダイアログを表示する。
// ファイル操作・シーン切替はDebugEditorに委譲し、選択と入力内容を保持する。
// 主な入口はDrawList、DrawDialogs、RequestOpen。
class SceneAssetPanel {
public:
    void Initialize(DebugEditor* editor);
    void DrawList();
    void DrawDialogs();

    // 次のDrawDialogsで新規作成ダイアログを開く。
    void OpenCreateSceneDialog();

    // filenameで指定したScene Assetを開く。未保存の変更があれば確認を待つ。
    void RequestOpen(const std::string& filename);

private:
    DebugEditor* editor_ = nullptr;

    std::string selectedSceneAssetFilename_;
    std::string pendingOpenSceneAssetFilename_;

    std::string sceneAssetStatusMessage_;
    bool sceneAssetStatusIsError_ = false;

    bool requestCreateScenePopup_ = false;
    bool requestDuplicateScenePopup_ = false;
    bool requestRenameScenePopup_ = false;
    bool requestDeleteScenePopup_ = false;
    bool requestUnsavedOpenPopup_ = false;
    bool requestRuntimeSettingsPopup_ = false;

    char createSceneId_[96] = "new_scene";
    char createSceneDisplayName_[128] = "New Scene";

    char duplicateSceneId_[96] = "";
    char duplicateSceneDisplayName_[128] = "";

    char renameSceneId_[96] = "";
    char renameSceneDisplayName_[128] = "";

    int createSceneTemplate_ = 0;
    int createRuntimeSceneIndex_ = 0;

    int runtimeSettingsControllerIndex_ = 0;

    char runtimeBgmPath_[260] = "";
    char runtimeLightPath_[260] = "";
    char runtimeCameraPath_[260] = "";
    char runtimeSkyboxPath_[260] = "";
};
