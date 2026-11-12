#pragma once

#include "IEditable.h"
#include "engine/animation/AnimatorController.h"

#include <string>
#include <vector>

class DebugEditor;
namespace engine { class Object3D; }
class SceneManager;

// 担当：アニメーション状態・パラメータ・遷移の定義を編集して保存する。
// 関連：AnimatorControllerAsset（定義）、Object3D（再生確認対象）
// 主な入口：Initialize / SetPreviewTarget / DrawImGui
class AnimatorControllerEditor : public IEditable {
public:
    // 新規Controllerを初期値にし、保存済みAssetの一覧を編集開始時に集めます。
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);

    // プレビュー対象の名前を記録します。シーンが変わった後の生ポインタは保持しません。
    void SetPreviewTarget(engine::Object3D* target);

    // このツールの設定・操作UIをImGuiで描画する。
    void DrawImGui() override;

    std::string GetName() override { return "Animator Controller"; }

private:
    // Controllerと体アニメーションのJSONを別一覧にし、誤ったAssetを選ばせません。
    void RefreshFiles();

    // 遷移先がない新規Assetでも再生開始できるよう、Idleを入口Stateにします。
    void NewController();

    // 読込失敗を通知して処理を止め、成功したAssetだけ選択状態へ反映します。
    void LoadController(const std::string& fileName);

    // 保存成功後だけPreview対象へ再読込し、編集中の値とSceneの実行状態を揃える。
    void SaveController();

    std::string GetAssetPath() const;

    // 現在シーンから対象を再検索します。削除済みなら nullptr を返します。
    engine::Object3D* ResolvePreviewTarget() const;

    // ファイルの新規作成・選択・保存を扱い、下の State／Parameter／Transition 編集へつなぐ。
    void DrawAssetControls();

    // アニメーションの再生確認対象を選ぶUIを描画する。
    void DrawPreviewTargetControls();

    // 状態の名前変更や削除は参照元の遷移にも影響するため、専用操作に集約します。
    void DrawStateEditor();

    // Animatorのパラメータ定義を編集する。
    void DrawParameterEditor();

    // Animatorの遷移条件・補間設定を編集する。
    void DrawTransitionEditor();

    // 遷移と現在の開始状態が古い名前を参照し続けないよう、関連箇所を更新します。
    void RenameState(const std::string& oldName, const std::string& newName);

    SceneManager* sceneManager_ = nullptr; // 現在シーンの取得元。借用参照。
    DebugEditor* editor_ = nullptr;
    AnimatorControllerAsset controller_;
    std::vector<std::string> files_;
    std::vector<std::string> bodyAnimationFiles_;
    char fileNameBuffer_[128] = "new_animator";
    std::string previewTargetName_;
    int selectedStateIndex_ = -1; // 選択状態。-1は未選択。
    int selectedParameterIndex_ = -1; // 選択パラメータ。-1は未選択。
    int selectedTransitionIndex_ = -1; // 選択遷移。-1は未選択。
};
