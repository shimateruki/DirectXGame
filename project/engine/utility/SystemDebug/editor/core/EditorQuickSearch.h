#pragma once

#ifdef USE_IMGUI

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class DebugEditor;
class SceneManager;

/// Editor 操作、Scene、Asset を Ctrl+K から横断検索するコマンドパレット。
class EditorQuickSearch {
public:
    /// シーン・エディターの参照と、非表示のパネルを開くコールバックを登録します。
    void Initialize(
        SceneManager* sceneManager,
        DebugEditor* editor,
        std::function<void()> ensureEditorPanelsVisible);
    /// お気に入りと履歴を保存して参照を解除します。
    void Finalize();

    /// 入力欄へフォーカスしてパレットを開きます。
    void Open();

    /// Ctrl+K を処理します。テキスト入力中はショートカットを消費しません。
    void HandleShortcut();

    /// 候補の検索・選択・実行を描画します。
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

    /// 検索・順位付け・実行に必要な一件分の候補。
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

    // 資産・シーンなどの世代が変わったときだけ候補を再収集します。
    void RefreshResults(bool force = false);
    void CollectCommands(std::vector<SearchItem>& items) const;
    void CollectObjects(std::vector<SearchItem>& items) const;
    void CollectAssets(std::vector<SearchItem>& items) const;
    void CollectSceneAssets(std::vector<SearchItem>& items) const;
    void CollectPresets(std::vector<SearchItem>& items) const;
    void CollectEditorWindows(std::vector<SearchItem>& items) const;
    // 検索語の一致度と利用履歴を使って表示順位を決めます。
    int ScoreItem(const SearchItem& item, const std::string& normalizedQuery) const;
    void ExecuteItem(std::size_t index);
    void ToggleFavorite(const std::string& id);
    void TouchRecent(const std::string& id);
    bool IsFavorite(const std::string& id) const;
    int FindRecentRank(const std::string& id) const;
    // 利用者固有のお気に入り・履歴だけをディスクへ永続化します。
    void LoadState();
    void SaveState() const;

private:
    // 外部の管理者は所有せず、候補と UI 状態のみこのクラスが保持します。
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
