#include "engine/utility/SystemDebug/editor/windows/replay/ReplayDebugger.Internal.h"

#ifdef USE_IMGUI
using namespace ReplayDebuggerImplementation;

// 記録済み入力を使った回帰確認と結果出力。
bool ReplayDebugger::StartRegression() {
    if (!loadedArchiveReadOnly_ || frames_.size() < 2 || inputSamples_.empty() || !GetValidatedActiveScene()) {
        statusMessage_ = "自動回帰テストに必要な初期状態または入力列がありません。";
        return false;
    }

    ClearTransientRuntime();
    ApplyFrame(0);
    regressionResult_ = {};
    regressionResult_.available = true;
    regressionResult_.running = true;
    regressionResult_.archiveName = loadedArchiveName_;
    regressionResult_.expectedGoal = IsGoalReached(frames_.back().sceneState);
    regressionInputIndex_ = 0;
    regressionExpectedFrameIndex_ = 1;
    regressionNextScreenshotIndex_ = 0;
    regressionStepPrepared_ = false;
    regressionReportPending_ = false;
    regressionStepDeltaTime_ = 0.0f;
    regressionElapsedTime_ = 0.0;
    regressionCpuTotalMs_ = 0.0;
    regressionGpuTotalMs_ = 0.0;
    regressionPerformanceSamples_ = 0;
    regressionErrorBaseline_ = DebugConsole::GetInstance()->GetErrorCount();
    pendingRegressionScreenshotPaths_.clear();
    regressionScreenshotTimes_.clear();

    const std::filesystem::path archivePath(loadedArchiveName_);
    const std::string outputName = archivePath.stem().string() + "_" + MakeLocalTimestamp("%Y%m%d_%H%M%S");
    const std::filesystem::path outputDirectory =
        std::filesystem::path("Saved") / "ReplayRegression" / outputName;
    std::error_code fileError;
    std::filesystem::create_directories(outputDirectory, fileError);
    if (fileError) {
        statusMessage_ = "回帰テスト出力フォルダーを作成できません: " + fileError.message();
        regressionResult_.running = false;
        return false;
    }
    regressionOutputDirectory_ = outputDirectory.string();

    const double duration = inputSamples_.back().time;
    const int screenshotCount = (std::clamp)(regressionSettings_.screenshotCount, 0, 8);
    for (int index = 0; index < screenshotCount && duration > 0.0; ++index) {
        regressionScreenshotTimes_.push_back(
            duration * static_cast<double>(index + 1) / static_cast<double>(screenshotCount));
    }

    cursor_ = 0;
    mode_ = Mode::Regression;
    statusMessage_ = "入力付きReplayを使った自動回帰テストを開始しました。";
    return true;
}

void ReplayDebugger::UpdateRegressionBeforeSimulation() {
    regressionStepPrepared_ = false;
    if (!regressionResult_.running) {
        return;
    }
    if (!GetValidatedActiveScene()) {
        regressionResult_.failures.push_back("テスト中に対象シーンが失われました。");
        FinishRegression();
        return;
    }
    if (regressionInputIndex_ >= inputSamples_.size()) {
        FinishRegression();
        return;
    }

    const InputSample& sample = inputSamples_[regressionInputIndex_];
    regressionStepDeltaTime_ = (std::clamp)(sample.deltaTime, 0.0001f, 0.25f);
    InputManager::GetInstance()->ApplyReplayState(sample.state);
    regressionStepPrepared_ = true;
}

void ReplayDebugger::CaptureRegressionAfterSimulation() {
    if (!regressionResult_.running || !regressionStepPrepared_) {
        return;
    }

    regressionElapsedTime_ += regressionStepDeltaTime_;
    ++regressionResult_.simulatedFrames;

    const float cpuMs = ProfilerManager::GetInstance()->GetLatestCpuTime("更新処理");
    const float gpuMs = ProfilerManager::GetInstance()->GetLatestGpuTime("Total");
    regressionCpuTotalMs_ += (std::max)(0.0f, cpuMs);
    regressionGpuTotalMs_ += (std::max)(0.0f, gpuMs);
    regressionResult_.maximumCpuMs = (std::max)(regressionResult_.maximumCpuMs, cpuMs);
    regressionResult_.maximumGpuMs = (std::max)(regressionResult_.maximumGpuMs, gpuMs);
    ++regressionPerformanceSamples_;

    while (regressionExpectedFrameIndex_ < frames_.size() &&
        frames_[regressionExpectedFrameIndex_].time <= regressionElapsedTime_ + 0.0001) {
        CompareRegressionFrame(frames_[regressionExpectedFrameIndex_]);
        ++regressionExpectedFrameIndex_;
    }
    QueueRegressionScreenshots();

    ++regressionInputIndex_;
    regressionStepPrepared_ = false;
    if (regressionInputIndex_ >= inputSamples_.size()) {
        FinishRegression();
    }
}

const ReplayDebugger::ObjectSnapshot* ReplayDebugger::FindPlayerSnapshot(const FrameSnapshot& frame) const {
    const auto found = std::find_if(frame.objects.begin(), frame.objects.end(), [](const ObjectSnapshot& snapshot) {
        return snapshot.className == "Player" || snapshot.name == "player" || snapshot.name == "Player";
    });
    return found != frame.objects.end() ? &*found : nullptr;
}

void ReplayDebugger::CompareRegressionFrame(const FrameSnapshot& expected) {
    BaseScene* scene = GetValidatedActiveScene();
    const ObjectSnapshot* expectedPlayer = FindPlayerSnapshot(expected);
    if (!scene || !expectedPlayer) {
        if (std::find(regressionResult_.failures.begin(), regressionResult_.failures.end(),
            "比較対象のプレイヤーをReplayから取得できません。") == regressionResult_.failures.end()) {
            regressionResult_.failures.push_back("比較対象のプレイヤーをReplayから取得できません。");
        }
        return;
    }

    Object3d* currentPlayer = nullptr;
    for (const auto& object : scene->GetObjects()) {
        if (!object || object->IsReplayRemoved()) {
            continue;
        }
        if (object->EnsureReplayId() == expectedPlayer->replayId ||
            (!expectedPlayer->persistentGuid.empty() && object->GetPersistentGuid() == expectedPlayer->persistentGuid) ||
            (object->GetClassName() == expectedPlayer->className && object->GetName() == expectedPlayer->name)) {
            currentPlayer = object.get();
            break;
        }
    }
    if (!currentPlayer) {
        if (std::find(regressionResult_.failures.begin(), regressionResult_.failures.end(),
            "実行中シーンのプレイヤーを取得できません。") == regressionResult_.failures.end()) {
            regressionResult_.failures.push_back("実行中シーンのプレイヤーを取得できません。");
        }
        return;
    }

    const Vector3 actual = currentPlayer->CaptureReplayState().translation;
    const Vector3 reference = expectedPlayer->state.translation;
    const float dx = actual.x - reference.x;
    const float dy = actual.y - reference.y;
    const float dz = actual.z - reference.z;
    const float error = std::sqrt(dx * dx + dy * dy + dz * dz);
    regressionResult_.maxPositionError = (std::max)(regressionResult_.maxPositionError, error);
    ++regressionResult_.comparedFrames;

    const float limit = (std::max)(1.0f, regressionSettings_.worldPositionLimit);
    if (!std::isfinite(actual.x) || !std::isfinite(actual.y) || !std::isfinite(actual.z) ||
        std::abs(actual.x) > limit || std::abs(actual.y) > limit || std::abs(actual.z) > limit) {
        const std::string message = "プレイヤー座標が許容ワールド範囲を外れました。";
        if (std::find(regressionResult_.failures.begin(), regressionResult_.failures.end(), message) ==
            regressionResult_.failures.end()) {
            regressionResult_.failures.push_back(message);
        }
    }
}

bool ReplayDebugger::IsGoalReached(const json& sceneState) {
    const auto goal = sceneState.find("goal");
    if (goal == sceneState.end() || !goal->is_object()) {
        return false;
    }
    return goal->value("active", false) || goal->value("wasStageCleared", false) || goal->value("state", 0) > 0;
}

void ReplayDebugger::QueueRegressionScreenshots() {
    while (regressionNextScreenshotIndex_ < regressionScreenshotTimes_.size() &&
        regressionElapsedTime_ + 0.0001 >= regressionScreenshotTimes_[regressionNextScreenshotIndex_]) {
        const std::string fileName = "checkpoint_" +
            std::to_string(regressionNextScreenshotIndex_ + 1) + ".png";
        pendingRegressionScreenshotPaths_.push_back(
            (std::filesystem::path(regressionOutputDirectory_) / fileName).string());
        ++regressionNextScreenshotIndex_;
    }
}

void ReplayDebugger::FinishRegression() {
    if (!regressionResult_.running) {
        return;
    }

    if (regressionExpectedFrameIndex_ < frames_.size()) {
        CompareRegressionFrame(frames_.back());
    }
    json currentSceneState = json::object();
    if (BaseScene* scene = GetValidatedActiveScene()) {
        scene->CaptureReplaySceneState(currentSceneState);
    }
    regressionResult_.observedGoal = IsGoalReached(currentSceneState);
    regressionResult_.durationSeconds = regressionElapsedTime_;
    regressionResult_.newErrorLogs =
        DebugConsole::GetInstance()->GetErrorCount() - regressionErrorBaseline_;
    if (regressionPerformanceSamples_ > 0) {
        regressionResult_.averageCpuMs = static_cast<float>(
            regressionCpuTotalMs_ / static_cast<double>(regressionPerformanceSamples_));
        regressionResult_.averageGpuMs = static_cast<float>(
            regressionGpuTotalMs_ / static_cast<double>(regressionPerformanceSamples_));
    }

    if (regressionResult_.comparedFrames == 0) {
        regressionResult_.failures.push_back("プレイヤー軌跡を比較できるReplayフレームがありませんでした。");
    }
    if (regressionResult_.maxPositionError > regressionSettings_.maxPositionError) {
        std::ostringstream message;
        message << "プレイヤー軌跡の最大誤差 " << std::fixed << std::setprecision(3)
            << regressionResult_.maxPositionError << " が許容値 "
            << regressionSettings_.maxPositionError << " を超えました。";
        regressionResult_.failures.push_back(message.str());
    }
    if (regressionResult_.expectedGoal && !regressionResult_.observedGoal) {
        regressionResult_.failures.push_back("基準Replayではゴール済みですが、再実行ではゴールへ到達しませんでした。");
    }
    if (regressionResult_.newErrorLogs > 0) {
        regressionResult_.failures.push_back(
            "テスト中にErrorログが " + std::to_string(regressionResult_.newErrorLogs) + " 件発生しました。");
    }
    if (regressionSettings_.failOnPerformanceBudget) {
        if (regressionResult_.averageCpuMs > regressionSettings_.cpuBudgetMs) {
            regressionResult_.failures.push_back("平均CPU時間が設定Budgetを超えました。");
        }
        if (regressionResult_.averageGpuMs > regressionSettings_.gpuBudgetMs) {
            regressionResult_.failures.push_back("平均GPU時間が設定Budgetを超えました。");
        }
    }

    regressionResult_.running = false;
    regressionResult_.passed = regressionResult_.failures.empty();
    regressionStepPrepared_ = false;
    mode_ = Mode::Paused;
    regressionReportPending_ = true;
    if (pendingRegressionScreenshotPaths_.empty()) {
        FinalizeRegressionReport();
    } else {
        statusMessage_ = "自動回帰テストの判定が完了しました。Game View画像を保存しています。";
    }
}

void ReplayDebugger::CapturePendingRegressionScreenshot(CaptureToolWindow* captureTool) {
    if (pendingRegressionScreenshotPaths_.empty()) {
        if (regressionReportPending_) {
            FinalizeRegressionReport();
        }
        return;
    }

    const std::string outputPath = pendingRegressionScreenshotPaths_.front();
    pendingRegressionScreenshotPaths_.pop_front();
    std::error_code fileError;
    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path(), fileError);
    const bool captured = !fileError && captureTool &&
        captureTool->CaptureGameViewToFile(std::filesystem::path(outputPath));
    if (captured) {
        regressionResult_.screenshotPaths.push_back(outputPath);
    } else {
        regressionResult_.failures.push_back("Game Viewスクリーンショットを保存できませんでした: " + outputPath);
        regressionResult_.passed = false;
    }

    if (pendingRegressionScreenshotPaths_.empty() && regressionReportPending_) {
        FinalizeRegressionReport();
    }
}

void ReplayDebugger::FinalizeRegressionReport() {
    if (!regressionReportPending_) {
        return;
    }
    regressionReportPending_ = false;
    regressionResult_.passed = regressionResult_.failures.empty();

    const json report = {
        { "schemaVersion", 1 },
        { "createdAt", MakeLocalTimestamp("%Y-%m-%d %H:%M:%S") },
        { "archive", regressionResult_.archiveName },
        { "status", regressionResult_.passed ? "passed" : "failed" },
        { "durationSeconds", regressionResult_.durationSeconds },
        { "simulatedFrames", regressionResult_.simulatedFrames },
        { "comparedFrames", regressionResult_.comparedFrames },
        { "maxPositionError", regressionResult_.maxPositionError },
        { "expectedGoal", regressionResult_.expectedGoal },
        { "observedGoal", regressionResult_.observedGoal },
        { "newErrorLogs", regressionResult_.newErrorLogs },
        { "performance", {
            { "averageCpuMs", regressionResult_.averageCpuMs },
            { "maximumCpuMs", regressionResult_.maximumCpuMs },
            { "averageGpuMs", regressionResult_.averageGpuMs },
            { "maximumGpuMs", regressionResult_.maximumGpuMs }
        } },
        { "settings", {
            { "maxPositionError", regressionSettings_.maxPositionError },
            { "worldPositionLimit", regressionSettings_.worldPositionLimit },
            { "cpuBudgetMs", regressionSettings_.cpuBudgetMs },
            { "gpuBudgetMs", regressionSettings_.gpuBudgetMs },
            { "failOnPerformanceBudget", regressionSettings_.failOnPerformanceBudget }
        } },
        { "failures", regressionResult_.failures },
        { "screenshots", regressionResult_.screenshotPaths }
    };

    const std::filesystem::path reportPath =
        std::filesystem::path(regressionOutputDirectory_) / "report.json";
    const std::filesystem::path temporaryPath = reportPath.string() + ".tmp";
    std::error_code fileError;
    try {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("レポートファイルを開けません。");
        }
        stream << report.dump(2);
        stream.close();
        if (!stream) {
            throw std::runtime_error("レポートを書き込めません。");
        }
        std::filesystem::rename(temporaryPath, reportPath, fileError);
        if (fileError) {
            throw std::runtime_error(fileError.message());
        }
        regressionResult_.reportPath = reportPath.string();
        statusMessage_ = regressionResult_.passed
            ? "自動回帰テストに成功しました: " + regressionResult_.reportPath
            : "自動回帰テストで差異を検出しました: " + regressionResult_.reportPath;
    } catch (const std::exception& exception) {
        std::filesystem::remove(temporaryPath, fileError);
        regressionResult_.passed = false;
        regressionResult_.failures.push_back(std::string("回帰テストレポート保存失敗: ") + exception.what());
        statusMessage_ = regressionResult_.failures.back();
    }
}

#endif
