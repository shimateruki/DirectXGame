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
class Object3d;
class SceneManager;

// Sceneデータを変更しない一時表示と、シーンごとの編集地点ブックマークを管理する。
// SceneManagerとDebugEditorは借用し、非表示状態はObjectのEditor専用フラグへ反映する。
class SceneWorkspace {
public:
    // SceneManager と DebugEditor を借用し、編集用ブックマークを読み込む。
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void Finalize();
    void Update();
    // Play 中は編集地点の記録・復帰を行わない。
    void HandleHotkeys();
    void DrawHierarchyPanel();

    // 選択階層以外を一時的に隠す。選択階層のレイヤー・手動非表示は解除する。
    void IsolateSelection();
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
    struct CameraBookmark {
        bool valid = false;
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };
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
    void CaptureBookmark(std::size_t index);
    void RecallBookmark(std::size_t index);
    void ClearBookmark(std::size_t index);
    void LoadBookmarks();
    void SaveBookmarks() const;
    std::vector<std::string> CollectSceneLayers() const;
    void CollectHierarchy(Object3d* root, std::unordered_set<Object3d*>& objects) const;
    void ResetSceneRuntimeState();

private:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;
    std::uint64_t observedSceneGeneration_ = 0;
    bool isolationActive_ = false;
    std::unordered_set<std::string> hiddenLayers_;
    std::unordered_map<std::string, std::array<CameraBookmark, kBookmarkCount>> bookmarksByScene_;
    std::string bookmarkStatePath_ = "output/editor_state/scene_workspace_bookmarks.json";
};

#endif
