#pragma once
#include <cstdint>

class DebugEditor;
class Object3d;

/// <summary>
/// 右ペインのInspectorを描画し、選択中オブジェクトの詳細設定を編集する。
/// </summary>
class InspectorWindow {
public:
    InspectorWindow() = default;
    ~InspectorWindow() = default;

    /// <summary>
    /// 親となるDebugEditorを登録する。
    /// </summary>
    void Initialize(DebugEditor* editor);

    /// <summary>
    /// 選択中オブジェクトに応じたInspector UIを描画する。
    /// </summary>
    void Draw();

private:
    // Inspector専用のUI描画ヘルパー。
    void DrawSpawnerSettings();
    void DrawEnemyTypeSelector();
    void DrawGimmickTypeSelector();
    void DrawItemTypeSelector();
    void DrawAttributeSelector(const char* label, uint32_t* attribute);

    // DebugEditor本体への参照。InspectorWindowは所有しない。
    DebugEditor* editor_ = nullptr;
};
