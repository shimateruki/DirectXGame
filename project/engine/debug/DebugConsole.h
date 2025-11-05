#pragma once
#include <vector>
#include <string>
#include <mutex> // ログ追加の排他制御用

/// <summary>
/// ImGuiベースのインゲーム・コンソール
/// </summary>
class DebugConsole {
public:
    static DebugConsole* GetInstance();

    void Initialize();
    void Finalize();

    /// <summary>
    /// コンソールにログ文字列を追加する
    /// </summary>
    void AddLog(const std::string& log);

    /// <summary>
    /// ImGuiウィンドウの中身を描画する
    /// </summary>
    void DrawImGui();

private:
    DebugConsole() = default;
    ~DebugConsole() = default;
    DebugConsole(const DebugConsole&) = delete;
    DebugConsole& operator=(const DebugConsole&) = delete;

    std::vector<std::string> logs_;
    std::mutex logMutex_;
    bool scrollToBottom_ = false; // 自動スクロール用
};