#pragma once

#ifdef USE_IMGUI

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class DebugEditor;
class SceneManager;

// EditorQuickSearchは、Editor操作とScene/AssetをCtrl+Kから横断検索します。
class EditorQuickSearch {
public:
    void Initialize(
        SceneManager* sceneManager,
        DebugEditor* editor,
        std::function<void()> ensureEditorPanelsVisible);
    void Finalize();
    void Open();
    void HandleShortcut();
    void Draw();

private:
    enum class ItemKind {
        Command,
        Object,
        Asset,
        Scene,
        Preset,
        Prefab,
        Window,
    };

    struct SearchItem {
        std::string id;
        std::string label;
        std::string category;
        std::string detail;
        std::string searchableText;
        ItemKind kind = ItemKind::Command;
        int score = 0;
        bool enabled = true;
        bool favorite = false;
        int recentRank = -1;
        std::function<void()> execute;
    };

    void RefreshResults(bool force = false);
    void CollectCommands(std::vector<SearchItem>& items) const;
    void CollectObjects(std::vector<SearchItem>& items) const;
    void CollectAssets(std::vector<SearchItem>& items) const;
    void CollectSceneAssets(std::vector<SearchItem>& items) const;
    void CollectPresets(std::vector<SearchItem>& items) const;
    void CollectEditorWindows(std::vector<SearchItem>& items) const;
    int ScoreItem(const SearchItem& item, const std::string& normalizedQuery) const;
    void ExecuteItem(std::size_t index);
    void ToggleFavorite(const std::string& id);
    void TouchRecent(const std::string& id);
    bool IsFavorite(const std::string& id) const;
    int FindRecentRank(const std::string& id) const;
    void LoadState();
    void SaveState() const;

private:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* editor_ = nullptr;
    std::function<void()> ensureEditorPanelsVisible_;
    std::vector<SearchItem> results_;
    std::vector<std::string> favoriteIds_;
    std::vector<std::string> recentIds_;
    char query_[192] = {};
    std::string lastQuery_;
    std::uint64_t lastCommandRevision_ = 0;
    std::uint64_t lastSceneGeneration_ = 0;
    std::uint64_t lastAssetGeneration_ = 0;
    std::size_t lastPresetCount_ = 0;
    std::size_t lastPrefabCount_ = 0;
    int selectedIndex_ = 0;
    bool openRequested_ = false;
    bool focusQueryRequested_ = false;
    std::string statePath_ = "output/editor_state/editor_quick_search.json";
};

#endif
