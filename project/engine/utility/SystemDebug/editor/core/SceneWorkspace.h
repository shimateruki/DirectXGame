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

// SceneWorkspaceは、Sceneデータを変更しない一時表示と編集地点ブックマークを管理します。
class SceneWorkspace {
public:
    void Initialize(SceneManager* sceneManager, DebugEditor* editor);
    void Finalize();
    void Update();
    void HandleHotkeys();
    void DrawHierarchyPanel();

    void IsolateSelection();
    void HideSelectionTemporarily();
    void ShowSelectionTemporarily();
    void RestoreAllTemporaryVisibility();
    void SetLayerVisible(const std::string& layer, bool visible);

    bool IsIsolationActive() const { return isolationActive_; }
    std::size_t GetTemporaryHiddenCount() const;

private:
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
