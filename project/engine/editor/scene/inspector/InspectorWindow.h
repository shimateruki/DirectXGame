#pragma once
#include <cstdint>

class DebugEditor;
namespace engine { class Object3D; }

// 担当：選択Objectの共通・型別設定を表示し、編集処理へ接続する。
// 関連：DebugEditor（選択）、EditorPropertyRegistry（共通項目）、InspectorExtensionRegistry（固有UI）
// 主な入口：Initialize / Draw
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
    // 材質固有の数値UIのみを担当。変更通知・保存・複数選択への反映は呼び出し元に残す。
    void DrawEffectMaterialSettings(engine::Object3D* selectedObject, int currentMatType, bool& isGraphicsChanged);

    // Inspector専用のUI描画ヘルパー。
    /// スポナーObject固有の出現対象や間隔などを編集する。
    void DrawSpawnerSettings();

    /// Enemy分類の詳細タイプを切り替え、必要な追加設定UIへ誘導する。
    void DrawEnemyTypeSelector();

    // ギミックの種類選択と型変更のUIを描画する。
    void DrawGimmickTypeSelector();

    /// Ghost Recorderで作成した移動パスの割り当てと試験再生を描画する。
    void DrawPathMoverSection(engine::Object3D* selectedObject);

    /// Event・Class・ステータス・タイプ固有値をまとめたゲームプレイ設定を描画する。
    void DrawGameplayDataSection(engine::Object3D* selectedObject);

    /// 衝突属性やマスクなどのビットフラグを、人間が見やすい形で編集する。
    void DrawAttributeSelector(const char* label, uint32_t* attribute);

    // DebugEditor本体への参照。InspectorWindowは所有しない。
    DebugEditor* editor_ = nullptr;
};
