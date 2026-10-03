#pragma once

#include "IEditable.h"

#include <future>
#include <string>

class DebugEditor;

/// ビルド済み実行ファイルと必要リソースを配布用フォルダへまとめるパッケージ作成ウィンドウ。
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
    struct PackageRequest {
        std::string packageName;
        std::string configuration;
        bool buildBeforePackage = false;
        int textureMode = 0;
        bool createZip = true;
        bool includeProject = true;
        bool includeReadMe = true;
        bool excludeUnusedResources = false;
    };

    /// パッケージ作成タスクの成功可否と表示メッセージを返す結果情報。
    struct PackageResult {
        bool success = false;
        std::string message;
    };

    /// <summary>作成タスクが実行中か確認します。</summary>
    bool IsTaskRunning();

    /// <summary>完了したタスクの結果を回収して画面表示へ反映します。</summary>
    void PollTask();

    /// <summary>必要ならビルドしてから、配布ファイルの収集を開始します。</summary>
    /// <param name="buildBeforePackage">作成直前に選択した構成をビルドする場合は true。</param>
    void StartPackageTask(bool buildBeforePackage);

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
    bool excludeUnusedResources_ = false;
    std::future<PackageResult> task_;
    std::string statusText_ = "設定を選んで作成してください。";
};
