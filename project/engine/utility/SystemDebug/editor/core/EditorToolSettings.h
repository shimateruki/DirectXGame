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
    void AddTool(const std::string& id, const std::string& name, const std::string& category,
        std::function<void()> open, std::function<bool()> available);
    void AddVisibility(const std::string& id, const std::string& name, bool* value);
    void AddVisibility(const std::string& id, const std::string& name);
    // 描画時にも確認し、別パネルからの変更で一時非表示が解除されるのを防ぎます。
    bool IsVisible(const std::string& id) const;
    bool IsTemporarilyHidden() const { return hidden_; }
    void Load();
    void Save();
    void Open() { open_ = true; }
    void DrawWindow();
    void DrawToolsMenu();
    void DrawVisibilityMenu();
    void HandleShortcuts(bool editing);
private:
    struct Tool {
        std::string id, name, category;
        int key = 0;
        bool ctrl = false, shift = false, alt = false;
    };
    struct Visibility { std::string id, name; bool* value; bool beforeHide = false; };
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
