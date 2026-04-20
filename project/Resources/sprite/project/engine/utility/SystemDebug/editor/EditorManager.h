#pragma once
#include "IEditable.h"

class EditorManager {
private:
    EditorManager() = default;
    ~EditorManager() = default;
    EditorManager(const EditorManager&) = delete;
    EditorManager& operator=(const EditorManager&) = delete;

    // ★ 現在選択されているオブジェクトのポインタ
    IEditable* selectedObject_ = nullptr;

public:
    // どこからでも呼べるようにシングルトン化
    static EditorManager* GetInstance() {
        static EditorManager instance;
        return &instance;
    }

    // 選択状態のセット＆取得
    void SetSelectedObject(IEditable* object) { selectedObject_ = object; }
    IEditable* GetSelectedObject() const { return selectedObject_; }
    void ClearSelection() { selectedObject_ = nullptr; }

    // ★ Inspectorウィンドウを描画する関数
    void DrawInspector();
};