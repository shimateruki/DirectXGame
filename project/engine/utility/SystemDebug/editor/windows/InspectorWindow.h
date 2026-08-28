#pragma once
#include <cstdint>

class DebugEditor;
class Object3d;

/// <summary>
/// 右ペインのInspectorを描画し、選択中オブジェクトの詳細設定を編集する。
/// </summary>
/// 選択中Objectの基本情報、タイプ別設定、属性、ステータスなどを編集するInspectorパネル。
class InspectorWindow {
public:
    InspectorWindow() = default;
    ~InspectorWindow() = default;

    /// <summary>
    /// 親となるDebugEditorを登録する。
    /// </summary>
    /// 選択Objectや編集操作へアクセスするため、DebugEditor参照を保持する。
    void Initialize(DebugEditor* editor);

    /// <summary>
    /// 選択中オブジェクトに応じたInspector UIを描画する。
    /// </summary>
    /// 選択状態に応じて、共通設定とタイプ別設定のInspector UIを描画する。
    void Draw();

private:
    // Inspector専用のUI描画ヘルパー。
    /// スポナーObject固有の出現対象や間隔などを編集する。
    void DrawSpawnerSettings();
    /// Enemy分類の詳細タイプを切り替え、必要な追加設定UIへ誘導する。
    void DrawEnemyTypeSelector();
    void DrawGimmickTypeSelector();
    void DrawItemTypeSelector();
    /// Ghost Recorderで作成した移動パスの割り当てと試験再生を描画する。
    void DrawPathMoverSection(Object3d* selectedObject);
    /// Event・Class・ステータス・タイプ固有値をまとめたゲームプレイ設定を描画する。
    void DrawGameplayDataSection(Object3d* selectedObject);
    /// 衝突属性やマスクなどのビットフラグを、人間が見やすい形で編集する。
    void DrawAttributeSelector(const char* label, uint32_t* attribute);

    // DebugEditor本体への参照。InspectorWindowは所有しない。
    DebugEditor* editor_ = nullptr;
};
