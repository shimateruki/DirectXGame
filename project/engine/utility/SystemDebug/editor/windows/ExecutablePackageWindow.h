#pragma once

#include "IEditable.h"

#include <future>
#include <string>

class DebugEditor;

/// ビルド済み実行ファイルと必要リソースを配布用フォルダへまとめるパッケージ作成ウィンドウ。
class ExecutablePackageWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "実行ファイルセット (Executable Package)"; }

private:
    /// パッケージ名、構成、事前ビルド有無など、非同期タスクへ渡す入力情報。
    struct PackageRequest {
        std::string packageName;
        std::string configuration;
        bool buildBeforePackage = false;
        int textureMode = 0;
        bool createZip = true;
        bool includeProject = true;
        bool includeReadMe = true;
    };

    /// パッケージ作成タスクの成功可否と表示メッセージを返す結果情報。
    struct PackageResult {
        bool success = false;
        std::string message;
    };

    /// 非同期パッケージ作成タスクがまだ実行中か確認する。
    bool IsTaskRunning();
    void PollTask();
    /// 必要ならビルドを行ってから、配布用ファイルの収集を非同期で開始する。
    void StartPackageTask(bool buildBeforePackage);

    static PackageResult RunPackageTask(const PackageRequest& request);

    DebugEditor* editor_ = nullptr;
    char packageNameBuffer_[128] = "GE3_Playable";
    int configurationIndex_ = 2;
    int textureModeIndex_ = 0;
    bool createZip_ = true;
    bool includeProject_ = true;
    bool includeReadMe_ = true;
    std::future<PackageResult> task_;
    std::string statusText_ = "設定を選んで作成してください。";
};
