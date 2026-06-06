#pragma once

class DebugEditor; // 前方宣言
class BaseScene;
class Object3d;

// 左パネル（Hierarchy）の描画だけを専門に担当するクラス
class HierarchyWindow {
public:
    HierarchyWindow() = default;
    ~HierarchyWindow() = default;

    // 起動時に DebugEditor のポインタを受け取っておく
    void Initialize(DebugEditor* editor);

    // 毎フレームの描画処理
    void Draw();
    void DrawCreateContextMenu(BaseScene* scene, bool useGameViewCursor);

private:
    // 階層の再帰描画処理
    void DrawHierarchyNode(Object3d* obj);
    bool HasMatchingCategory(Object3d* obj);
    DebugEditor* editor_ = nullptr; // 本体へのアクセス権
    int currentCategoryFilter_ = 0; // 0: All, 1: Player, 2: Enemy, 3: Object

};
