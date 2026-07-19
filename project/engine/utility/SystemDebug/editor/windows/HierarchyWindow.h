#pragma once

#include <string>

class DebugEditor;
class BaseScene;
class Object3d;

/// <summary>
/// 左ペインのHierarchy表示と、オブジェクト生成メニューを担当する。
/// </summary>
class HierarchyWindow {
public:
    HierarchyWindow() = default;
    ~HierarchyWindow() = default;

    /// <summary>
    /// 親となるDebugEditorを登録する。
    /// </summary>
    void Initialize(DebugEditor* editor);

    /// <summary>
    /// Hierarchyウィンドウを描画する。
    /// </summary>
    void Draw();

    /// <summary>
    /// シーンまたはGameView上で使う生成コンテキストメニューを描画する。
    /// </summary>
    void DrawCreateContextMenu(BaseScene* scene, bool useGameViewCursor);

    // メインメニューからも同じ新規Sceneダイアログを開けるようにします。
    void OpenCreateSceneDialog();
    void OpenSceneAssetFromMenu(const std::string& filename);

private:
    void DrawHierarchyNode(Object3d* obj);
    bool HasMatchingCategory(Object3d* obj);
    void DrawSceneAssetManager();
    void DrawSceneAssetDialogs();
    void RequestOpenSceneAsset(const std::string& filename);

    // DebugEditor本体への参照。HierarchyWindowは所有しない。
    DebugEditor* editor_ = nullptr;

    // 0: All, 1: Player, 2: Enemy, 3: Object
    int currentCategoryFilter_ = 0;
    int currentLayerFilter_ = 0;

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
