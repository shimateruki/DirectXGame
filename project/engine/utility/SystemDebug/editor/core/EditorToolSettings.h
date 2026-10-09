#pragma once
#ifdef USE_IMGUI
#include <functional>
#include <map>
#include <string>
#include <vector>

// ゲーム用KeyConfigと保存先を分け、エディター操作だけを管理します。
class EditorToolSettings {
public:
    static EditorToolSettings& Get();
    void Clear();
    // ツールを表示・ショートカット対象として登録する。コールバックの所有権は移す。
    void AddTool(const std::string& id, const std::string& name, const std::string& category,
        std::function<void()> open, std::function<bool()> available);
    // value は登録中ずっと有効である必要がある。所有先が短命ならポインタなし版を使う。
    void AddVisibility(const std::string& id, const std::string& name, bool* value);
    // 可視性フラグをこの設定オブジェクト内で所有する。
    void AddVisibility(const std::string& id, const std::string& name);
    // 描画時にも確認し、別パネルからの変更で一時非表示が解除されるのを防ぎます。
    bool IsVisible(const std::string& id) const;
    bool IsTemporarilyHidden() const { return hidden_; }
    // 保存済み設定を読み込む。保存ファイルがない場合は現在の登録値を使う。
    void Load();
    // キー競合を検査してから一時ファイル経由で保存する。
    void Save();
    void Open() { open_ = true; }
    void DrawWindow();
    void DrawToolsMenu();
    void DrawVisibilityMenu();
    // 編集可能な状態だけで登録キーを処理する。文字入力・ポップアップ中は実行しない。
    void HandleShortcuts(bool editing);
private:
    // コマンド識別子と編集用ショートカット。ゲーム操作の KeyConfig とは別に保存する。
    struct Tool {
        std::string id, name, category;
        int key = 0;
        bool ctrl = false, shift = false, alt = false;
    };
    // 外部の表示フラグを借用し、一括非表示の解除時に beforeHide へ戻す。
    struct Visibility {
        std::string id, name;
        bool* value;
        bool beforeHide = false;
    };
    std::string Label(const Tool& tool) const;
    std::string Conflict(const Tool& tool) const;
    void RefreshCommand(const Tool& tool);
    std::string Snapshot() const;
    void RestoreHidden();
    void SetAllVisible(bool visible);
    std::vector<Tool> tools_;
    std::vector<Visibility> visibility_;
    std::map<std::string, bool> ownedVisibility_;
    bool open_ = false, hidden_ = false;
    char filter_[128] = {};
    std::string status_, saved_;
};
#endif
