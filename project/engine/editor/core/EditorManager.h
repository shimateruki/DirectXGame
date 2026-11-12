#pragma once
#include "IEditable.h"

// 担当：選択対象を共有し、Inspector描画を仲介する。
// 関連：IEditable（選択対象の共通契約）。対象を所有せず、編集UIの呼出しを仲介する。
// 主な入口：SetSelectedObject / GetSelectedObject / ClearSelection / DrawInspector
class EditorManager {
private:
    EditorManager() = default;

    ~EditorManager() = default;

    EditorManager(const EditorManager&) = delete;

    EditorManager& operator=(const EditorManager&) = delete;

    // 現在Inspectorで編集対象になっているオブジェクト。
    // 編集対象は借用する。対象を破棄する側で ClearSelection し、解放済み参照を残さない。
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

    // 借用している編集対象の参照を解除する。
    void ClearSelection() { selectedObject_ = nullptr; }

    /// <summary>
    /// 選択中オブジェクトのInspectorを描画する。
    /// </summary>
        // 選択中オブジェクトの編集UIを描画します。
void DrawInspector();
};
