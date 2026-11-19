#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// JSON編集ツールで共有するドッキング可能なファイル画面。各ツールはデータの読込・保存・初期化を担当する。
class JsonAssetBrowser {
public:
    // ワークスペースが作ったGame View下段へ全ファイル画面を固定する。
    static void SetBottomDock(unsigned int dockId);

    using Path = std::filesystem::path;
    // 素材選択欄と実行時の名前を合わせる。拡張子なしのルート相対名を返す。
    static std::vector<std::string> ListKeys(const Path& root);
    void RequestSaveAs(const std::string& suggestedName);
    const Path& GetDocument() const { return opened_; }

    using Read = std::function<bool(const Path&)>;
    using Write = std::function<bool(const Path&, const std::string&)>;

    /// <summary>指定ルートを表示する。保存コールバックには一時ファイルと確定後の表示名を渡す。</summary>
    void Draw(const char* title, const Path& root, const Read& read, const Write& write,
        const std::function<void()>& reset, const std::function<void(const Path&)>& onSaved = {}, bool unsaved = false);
    /// <summary>他の編集ツールから読み込んだ場合も保存対象を同期する。空なら新規文書。</summary>
    void SetDocument(const Path& relative);

private:
    Path folder_; // ルートからの相対パス。
    Path selected_;
    Path opened_;
    Path pending_;
    bool saveAsRequested_ = false;
    bool archive_ = false;
    char name_[256] = {};
    char folderName_[256] = {};
    std::string status_;
};
