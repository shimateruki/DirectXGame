#pragma once

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

private:
    void DrawHierarchyNode(Object3d* obj);
    bool HasMatchingCategory(Object3d* obj);

    // DebugEditor本体への参照。HierarchyWindowは所有しない。
    DebugEditor* editor_ = nullptr;

    // 0: All, 1: Player, 2: Enemy, 3: Object
    int currentCategoryFilter_ = 0;
};
