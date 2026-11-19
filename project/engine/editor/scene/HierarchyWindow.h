#pragma once

#include "SceneAssetPanel.h"

#include <string>
#include <vector>

class DebugEditor;
class BaseScene;
namespace engine { class Object3D; }

// 担当：シーンの親子階層・選択・Object生成を表示し、Scene Asset操作はSceneAssetPanelに渡す。
// 関連：DebugEditor（選択・操作）、InspectorWindow（詳細表示）、SceneManager（配置）
// 主な入口：Initialize / Draw / DrawCreateContextMenu
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

    // メニュー経由も通常のScene切り替え確認経路へ渡します。
    void OpenSceneAssetFromMenu(const std::string& filename);

private:
    // Object一個とその子階層を表示する。
    void DrawHierarchyNode(engine::Object3D* obj);

    bool HasMatchingCategory(engine::Object3D* obj);

    // DebugEditor本体への参照。HierarchyWindowは所有しない。
    DebugEditor* editor_ = nullptr;

    // 分類は表示上の仮想フォルダ。Object3dの親子関係や保存データは変更しない。
    bool groupHierarchy_ = true;

    // 選択変更時だけ展開・スクロールし、通常の一覧操作を妨げない。
    engine::Object3D* lastHierarchySelection_ = nullptr;
    BaseScene* lastHierarchyScene_ = nullptr;
    std::vector<engine::Object3D*> revealPath_;
    int revealFrames_ = 0;

    // 0: All, 1: Player, 2: Enemy, 3: Object, 4: Camera
    int currentCategoryFilter_ = 0;
    int currentLayerFilter_ = 0;

    SceneAssetPanel sceneAssetPanel_;
};
