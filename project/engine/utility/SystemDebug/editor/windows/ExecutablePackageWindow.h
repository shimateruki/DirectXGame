#pragma once

#include "IEditable.h"

#include <future>
#include <string>

class DebugEditor;

class ExecutablePackageWindow : public IEditable {
public:
    void Initialize(DebugEditor* editor);
    void DrawImGui() override;
    std::string GetName() override { return "実行ファイルセット (Executable Package)"; }

private:
    struct PackageRequest {
        std::string packageName;
        std::string configuration;
        bool buildBeforePackage = false;
    };

    struct PackageResult {
        bool success = false;
        std::string message;
    };

    bool IsTaskRunning();
    void PollTask();
    void StartPackageTask(bool buildBeforePackage);

    static PackageResult RunPackageTask(const PackageRequest& request);

    DebugEditor* editor_ = nullptr;
    char packageNameBuffer_[128] = "GE3_Playable";
    int configurationIndex_ = 1;
    std::future<PackageResult> task_;
    std::string statusText_ = "設定を選んで作成してください。";
};
