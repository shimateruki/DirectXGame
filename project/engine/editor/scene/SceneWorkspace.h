#pragma once

#ifdef USE_IMGUI

#include "engine/utility/math/Math.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class DebugEditor;
namespace engine { class Object3D; }
class SceneManager;

// 担当：Scene保存値とは別に、一時非表示と編集視点ブックマークを管理する。
// 関連：SceneManager（シーン識別）、DebugEditor（選択）、CameraEditor（編集視点）
// 主な入口：Initialize / Update / IsolateSelection / CaptureBookmark / Finalize
// 一時非表示はEditor専用フラグを使い、Sceneの保存値を変更しない。
class SceneWorkspace {
public:
    // SceneManager と DebugEditor を借用し、編集用ブックマークを読み込む。
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);

    // Editor専用の非表示状態を復元してからブックマークを保存します。
    void Finalize();

    // Scene切り替え時は旧Sceneに紐付く隔離・レイヤー表示状態を破棄します。
    void Update();

    // Play 中は編集地点の記録・復帰を行わない。
    void HandleHotkeys();

    // 選択状態と再生状態に応じ、隔離・非表示の操作可能範囲を制限します。
    void DrawHierarchyPanel();

    // 選択階層以外を一時的に隠す。選択階層のレイヤー・手動非表示は解除する。
    void IsolateSelection();

    // 保存データを変えず、Editorだけで選択Objectを隠します。
    void HideSelectionTemporarily();

    // 選択対象の隔離・レイヤー・手動非表示理由をすべて解除する。
    void ShowSelectionTemporarily();

    // 隔離・手動非表示・レイヤー非表示を解除する。保存済み可視性は変更しない。
    void RestoreAllTemporaryVisibility();

    // 指定レイヤーのEditor上の可視性だけを切り替える。Sceneの保存データは変更しない。
    void SetLayerVisible(const std::string& layer, bool visible);

    bool IsIsolationActive() const { return isolationActive_; }

    std::size_t GetTemporaryHiddenCount() const;

private:
    // 編集視点の位置・回転と注視対象の識別情報。valid が false の枠は未登録。
    // 関連：SceneWorkspace（この型を使う機能の本体）
    struct CameraBookmark {
        bool valid = false;
        engine::Vector3 position = engine::Vector3::ZERO;
        engine::Vector3 rotation = engine::Vector3::ZERO;
        std::string objectGuid;
        std::string objectName;
    };

    static constexpr std::uint32_t kIsolationReason = 1u << 0;
    static constexpr std::uint32_t kLayerReason = 1u << 1;
    static constexpr std::uint32_t kManualReason = 1u << 2;
    static constexpr std::size_t kBookmarkCount = 9;

    std::string GetCurrentSceneKey() const;

    std::array<CameraBookmark, kBookmarkCount>& GetCurrentBookmarks();

    const std::array<CameraBookmark, kBookmarkCount>* FindCurrentBookmarks() const;

    // Play中の視点を編集用の保存地点へ混ぜない。
    void CaptureBookmark(std::size_t index);

    // 指定枠に保存した編集視点と注視対象を呼び戻す。
    void RecallBookmark(std::size_t index);

    // 配列範囲を確認し、削除したブックマークを永続設定にも反映します。
    void ClearBookmark(std::size_t index);

    // シーンごとのカメラ記憶位置を読み込み、別シーンの記録と混ぜません。
    void LoadBookmarks();

    // Sceneごとのカメラ位置をEditor状態として保存し、ゲーム用JSONと分離します。
    void SaveBookmarks() const;

    // 内部補助Objectを除外し、空レイヤーはDefaultとして一覧化します。
    std::vector<std::string> CollectSceneLayers() const;

    // 集合への挿入結果で循環・重複を止めながら子階層をたどります。
    void CollectHierarchy(engine::Object3D* root, std::unordered_set<engine::Object3D*>& objects) const;

    // Scene世代が変わった後は、一時非表示の理由を新Sceneへ持ち越しません。
    void ResetSceneRuntimeState();

private:
    SceneManager* sceneManager_ = nullptr; // 現在シーンの取得元。借用参照。
    DebugEditor* editor_ = nullptr;
    std::uint64_t observedSceneGeneration_ = 0; // 一時表示状態が属するシーン世代。
    bool isolationActive_ = false;
    std::unordered_set<std::string> hiddenLayers_;
    std::unordered_map<std::string, std::array<CameraBookmark, kBookmarkCount>> bookmarksByScene_; // シーン識別子ごとの編集視点の保存枠。
    std::string bookmarkStatePath_ = "output/editor_state/scene_workspace_bookmarks.json"; // Scene JSONとは別のブックマーク保存先。
};

#endif
