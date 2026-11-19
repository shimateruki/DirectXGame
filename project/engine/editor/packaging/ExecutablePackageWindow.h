#pragma once

#include "IEditable.h"

#include <future>
#include <string>

class DebugEditor;

// 担当：ビルド済みEXEと必要資源を、配布用フォルダーへまとめる。
// 関連：DebugEditor（表示）、PackageRequest（作成条件）、PackageResult（非同期結果）
// 主な入口：Initialize / DrawImGui / IsTaskRunning
class ExecutablePackageWindow : public IEditable {
public:
    /// <summary>エディターとの参照関係を初期化します。</summary>
    /// <param name="editor">このウィンドウを所有するエディター。</param>
    void Initialize(DebugEditor* editor);

    /// <summary>作成条件と非同期処理の状態を表示します。</summary>
    void DrawImGui() override;

    std::string GetName() override { return "実行ファイルセット (Executable Package)"; }

private:
    /// UI スレッドで確定した作成条件。非同期タスクには値で渡します。
    // 関連：ExecutablePackageWindow（この型を使う機能の本体）
    struct PackageRequest {
        std::string packageName;
        std::string configuration;
        bool buildBeforePackage = false;
        bool analyzeOnly = false; // コピーせず、最終的な選別結果のレポートだけを保存します。
        int textureMode = 0;
        bool createZip = true;
        bool includeProject = true;
        bool includeReadMe = true;
        bool excludeUnusedResources = true;
        std::string additionalKeep; // 動的に指定する素材・フォルダーを改行で列挙します。
    };

    /// パッケージ作成タスクの成功可否と表示メッセージを返す結果情報。
    // 関連：ExecutablePackageWindow（この型を使う機能の本体）
    struct PackageResult {
        bool success = false;
        std::string message;
        std::string reportPath;
    };

    /// <summary>作成タスクが実行中か確認します。</summary>
    bool IsTaskRunning();

    /// <summary>完了したタスクの結果を回収して画面表示へ反映します。</summary>
    void PollTask();

    /// <summary>必要ならビルドしてから、配布ファイルの収集を開始します。</summary>
    /// <param name="buildBeforePackage">作成直前に選択した構成をビルドする場合は true。</param>
    /// <param name="analyzeOnly">trueなら素材の依存解析とレポート保存までを実行します。</param>
    void StartPackageTask(bool buildBeforePackage, bool analyzeOnly = false);

    /// <summary>検証、収集、任意の ZIP 化をワーカースレッドで実行します。</summary>
    /// <param name="request">UI で確定した作成条件。</param>
    /// <returns>成功可否と利用者向けの結果メッセージ。</returns>
    static PackageResult RunPackageTask(const PackageRequest& request);

    // 入力・表示状態は UI スレッドが所有し、ワーカーには PackageRequest のコピーだけを渡します。
    DebugEditor* editor_ = nullptr;
    char packageNameBuffer_[128] = "GE3_Playable";
    int configurationIndex_ = 2;
    int textureModeIndex_ = 0;
    bool createZip_ = true;
    bool includeProject_ = true;
    bool includeReadMe_ = true;
    bool excludeUnusedResources_ = true;
    char additionalKeepBuffer_[2048] = {}; // 自動解析に加えて残すResources相対パス。
    std::string reportPath_; // 最後に成功した解析・配布結果のレポート。
    std::future<PackageResult> task_; // 配布フォルダー作成処理の完了待ち。
    std::string statusText_ = "設定を選んで作成してください。";
};
