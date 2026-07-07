#pragma once
#include "IEditable.h"

/// <summary>
/// エディタ全体で共有する選択状態とInspector描画を管理する。
/// </summary>
// EditorManagerは、選択中オブジェクトとInspector描画をシンプルに仲介する管理クラスです。
class EditorManager {
private:
    EditorManager() = default;
    ~EditorManager() = default;
    EditorManager(const EditorManager&) = delete;
    EditorManager& operator=(const EditorManager&) = delete;

    // 現在Inspectorで編集対象になっているオブジェクト。
    IEditable* selectedObject_ = nullptr;

public:
    /// <summary>
    /// シングルトンインスタンスを取得する。
    /// </summary>
    static EditorManager* GetInstance() {
        static EditorManager instance;
        return &instance;
    }

    // 選択状態の設定、取得、解除。
    void SetSelectedObject(IEditable* object) { selectedObject_ = object; }
    IEditable* GetSelectedObject() const { return selectedObject_; }
    void ClearSelection() { selectedObject_ = nullptr; }

    /// <summary>
    /// 選択中オブジェクトのInspectorを描画する。
    /// </summary>
        // 選択中オブジェクトの編集UIを描画します。
void DrawInspector();
};
