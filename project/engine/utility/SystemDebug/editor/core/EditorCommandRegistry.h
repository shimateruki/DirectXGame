#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// EditorCommandは、メニュー・ショートカット・将来のQuick Searchが共有する操作定義です。
struct EditorCommand {
    std::string id;
    std::string displayName;
    std::string category;
    std::string shortcut;
    std::string description;
    std::vector<std::string> keywords;
    std::function<bool()> canExecute;
    std::function<void()> execute;
    const void* owner = nullptr;
};

// EditorCommandRegistryは、Editor操作の登録順と実行条件を一元管理します。
class EditorCommandRegistry {
public:
    static EditorCommandRegistry* GetInstance();

    // 同じOwnerが同じIDを登録した場合は内容を更新します。
    // 別Ownerが使用中のIDは上書きせずfalseを返します。
    bool Register(EditorCommand command);
    bool Unregister(std::string_view id, const void* owner = nullptr);
    void UnregisterOwner(const void* owner);
    void Clear();

    const EditorCommand* Find(std::string_view id) const;
    bool Contains(std::string_view id) const;
    bool CanExecute(std::string_view id) const;
    bool Execute(std::string_view id) const;

    // 登録順を保持した一覧です。Quick SearchやCommand一覧表示で利用します。
    std::vector<const EditorCommand*> GetCommands() const;
    uint64_t GetRevision() const { return revision_; }

private:
    std::unordered_map<std::string, EditorCommand> commands_;
    std::vector<std::string> registrationOrder_;
    uint64_t revision_ = 0;
};

// 主要Command IDを共有し、文字列の打ち間違いを防ぎます。
namespace EditorCommandId {
inline constexpr const char* Play = "game.play";
inline constexpr const char* Stop = "game.stop";
inline constexpr const char* ReplayPauseResume = "replay.pauseResume";
inline constexpr const char* SceneNew = "scene.new";
inline constexpr const char* SceneSave = "scene.save";
inline constexpr const char* EditUndo = "edit.undo";
inline constexpr const char* EditRedo = "edit.redo";
inline constexpr const char* EditDuplicate = "edit.duplicate";
inline constexpr const char* EditDelete = "edit.delete";
inline constexpr const char* ObjectDropToFloor = "object.dropToFloor";
inline constexpr const char* ViewEditorPanels = "view.editorPanels";
inline constexpr const char* ViewConsole = "view.console";
inline constexpr const char* ViewStatus = "view.status";
inline constexpr const char* ViewReplay = "view.replay";
inline constexpr const char* ViewBossDebug = "view.bossDebug";
inline constexpr const char* ViewPortfolio = "view.portfolio";
inline constexpr const char* HelpManual = "help.manual";
inline constexpr const char* HelpProfiler = "help.profiler";
}
