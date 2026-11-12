#pragma once

#ifdef USE_IMGUI

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class DebugEditor;
class SceneManager;

// 担当：操作・Object・Scene・Assetを横断検索し、選んだ候補を実行する。
// 関連：EditorCommandRegistry（操作）、AssetDatabase（資産）、DebugEditor（選択・移動）
// 主な入口：Open / HandleShortcut / Draw / Finalize
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
    // 関連：EditorQuickSearch（この型を使う機能の本体）
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

    // 以下の Collect 群は候補を出力配列へ追加する。検索順位付けや実行は後段で行う。
    void CollectCommands(std::vector<SearchItem>& items) const;

    // 現在シーンのObjectを検索候補へ追加する。
    void CollectObjects(std::vector<SearchItem>& items) const;

    // Asset索引から検索候補を追加する。
    void CollectAssets(std::vector<SearchItem>& items) const;

    // Scene Assetを検索候補へ追加する。
    void CollectSceneAssets(std::vector<SearchItem>& items) const;

    // 登録Presetを検索候補へ追加する。
    void CollectPresets(std::vector<SearchItem>& items) const;

    // Editorウィンドウの表示操作を検索候補へ追加する。
    void CollectEditorWindows(std::vector<SearchItem>& items) const;

    // 検索語の一致度と利用履歴を使って表示順位を決めます。
    int ScoreItem(const SearchItem& item, const std::string& normalizedQuery) const;

    // 表示中の候補を実行し、お気に入りとは別に最近使った履歴へ記録する。
    void ExecuteItem(std::size_t index);

    // 候補IDのお気に入り登録を切り替える。
    void ToggleFavorite(const std::string& id);

    // 実行した候補IDを最近使った一覧へ反映する。
    void TouchRecent(const std::string& id);

    bool IsFavorite(const std::string& id) const;

    // 候補の最近使った一覧での順位を求める。
    int FindRecentRank(const std::string& id) const;

    // 利用者固有のお気に入り・履歴だけをディスクへ永続化します。
    void LoadState();

    // 検索のお気に入り・最近使った状態を保存する。
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
    std::uint64_t lastSceneGeneration_ = 0; // 候補作成時のシーン世代。
    std::uint64_t lastAssetGeneration_ = 0; // 候補作成時のAsset索引世代。
    std::size_t lastPresetCount_ = 0;
    std::size_t lastPrefabCount_ = 0;

    int selectedIndex_ = 0; // 現在選択している検索結果の位置。
    bool openRequested_ = false; // 次のUI描画で開く予約。
    bool focusQueryRequested_ = false; // 検索欄へ入力フォーカスを移す予約。

    std::string statePath_ = "output/editor_state/editor_quick_search.json";
};

#endif
