#include "engine/utility/SystemDebug/editor/windows/replay/ReplayDebugger.Internal.h"

#ifdef USE_IMGUI
using namespace ReplayDebuggerImplementation;

// リプレイのシリアライズと保存・読込。
json ReplayDebugger::SerializeFrame(const FrameSnapshot& frame, double firstFrameTime) const {
    json objects = json::array();
    for (const ObjectSnapshot& snapshot : frame.objects) {
        objects.push_back({
            { "replayId", snapshot.replayId }, { "persistentGuid", snapshot.persistentGuid },
            { "name", snapshot.name }, { "className", snapshot.className },
            { "runtimeSpawned", snapshot.runtimeSpawned },
            { "saveCategory", snapshot.saveCategory },
            { "enemyType", snapshot.enemyType }, { "gimmickType", snapshot.gimmickType },
            { "itemType", snapshot.itemType },
            { "state", ObjectReplayStateToJson(snapshot.state) },
        });
    }

    json sprites = json::array();
    for (const SpriteSnapshot& snapshot : frame.sprites) {
        sprites.push_back({
            { "replayId", snapshot.replayId }, { "bindingKey", snapshot.bindingKey },
            { "textureName", snapshot.textureName }, { "name", snapshot.name },
            { "state", SpriteReplayStateToJson(snapshot.state) },
        });
    }

    const FrameDiagnostics& diagnostics = frame.diagnostics;
    return {
        { "time", (std::max)(0.0, frame.time - firstFrameTime) },
        { "objects", std::move(objects) }, { "sprites", std::move(sprites) },
        { "camera", {
            { "valid", frame.camera.valid }, { "eye", Vector3ToJson(frame.camera.eye) },
            { "target", Vector3ToJson(frame.camera.target) }, { "rotation", Vector3ToJson(frame.camera.rotation) },
            { "fovY", frame.camera.fovY }, { "nearClip", frame.camera.nearClip },
            { "farClip", frame.camera.farClip },
        } },
        { "sceneState", frame.sceneState },
        { "diagnostics", {
            { "activeObjects", diagnostics.activeObjects }, { "changedObjects", diagnostics.changedObjects },
            { "spawnedObjects", diagnostics.spawnedObjects }, { "removedObjects", diagnostics.removedObjects },
            { "hpChanges", diagnostics.hpChanges }, { "deaths", diagnostics.deaths },
            { "activeSprites", diagnostics.activeSprites }, { "changedSprites", diagnostics.changedSprites },
        } },
    };
}

bool ReplayDebugger::DeserializeFrame(const json& data, FrameSnapshot& frame, std::string& error) const {
    try {
        if (!data.is_object() || !data.contains("objects") || !data.contains("sprites")) {
            error = "フレーム構造が不足しています。";
            return false;
        }
        frame.time = data.value("time", 0.0);
        for (const json& objectData : data.at("objects")) {
            ObjectSnapshot snapshot;
            snapshot.replayId = objectData.value("replayId", uint64_t{ 0 });
            snapshot.persistentGuid = objectData.value("persistentGuid", std::string{});
            snapshot.name = objectData.value("name", std::string{});
            snapshot.className = objectData.value("className", std::string{});
            snapshot.runtimeSpawned = objectData.value("runtimeSpawned", false);
            snapshot.saveCategory = objectData.value("saveCategory", std::string{});
            snapshot.enemyType = objectData.value("enemyType", std::string{});
            snapshot.gimmickType = objectData.value("gimmickType", std::string{});
            snapshot.itemType = objectData.value("itemType", std::string{});
            snapshot.state = JsonToObjectReplayState(objectData.value("state", json::object()));
            frame.objects.push_back(std::move(snapshot));
        }
        for (const json& spriteData : data.at("sprites")) {
            SpriteSnapshot snapshot;
            snapshot.replayId = spriteData.value("replayId", uint64_t{ 0 });
            snapshot.bindingKey = spriteData.value("bindingKey", std::string{});
            snapshot.textureName = spriteData.value("textureName", std::string{});
            snapshot.name = spriteData.value("name", std::string{});
            snapshot.state = JsonToSpriteReplayState(spriteData.value("state", json::object()));
            frame.sprites.push_back(std::move(snapshot));
        }

        const json cameraData = data.value("camera", json::object());
        frame.camera.valid = cameraData.value("valid", false);
        frame.camera.eye = JsonToVector3(cameraData.value("eye", json::array()), frame.camera.eye);
        frame.camera.target = JsonToVector3(cameraData.value("target", json::array()), frame.camera.target);
        frame.camera.rotation = JsonToVector3(cameraData.value("rotation", json::array()), frame.camera.rotation);
        frame.camera.fovY = cameraData.value("fovY", frame.camera.fovY);
        frame.camera.nearClip = cameraData.value("nearClip", frame.camera.nearClip);
        frame.camera.farClip = cameraData.value("farClip", frame.camera.farClip);
        frame.sceneState = data.value("sceneState", json::object());

        const json diagnostics = data.value("diagnostics", json::object());
        frame.diagnostics.activeObjects = diagnostics.value("activeObjects", std::size_t{ 0 });
        frame.diagnostics.changedObjects = diagnostics.value("changedObjects", std::size_t{ 0 });
        frame.diagnostics.spawnedObjects = diagnostics.value("spawnedObjects", std::size_t{ 0 });
        frame.diagnostics.removedObjects = diagnostics.value("removedObjects", std::size_t{ 0 });
        frame.diagnostics.hpChanges = diagnostics.value("hpChanges", std::size_t{ 0 });
        frame.diagnostics.deaths = diagnostics.value("deaths", std::size_t{ 0 });
        frame.diagnostics.activeSprites = diagnostics.value("activeSprites", std::size_t{ 0 });
        frame.diagnostics.changedSprites = diagnostics.value("changedSprites", std::size_t{ 0 });
        frame.estimatedBytes = EstimateFrameBytes(frame);
        return true;
    } catch (const std::exception& exception) {
        error = std::string("フレーム解析失敗: ") + exception.what();
        return false;
    }
}


json ReplayDebugger::SerializeInputState(const InputManager::ReplayState& state) const {
    auto pressedIndices = [](const auto& values) {
        json result = json::array();
        for (std::size_t index = 0; index < values.size(); ++index) {
            if ((values[index] & 0x80u) != 0) {
                result.push_back(index);
            }
        }
        return result;
    };

    return {
        { "keys", pressedIndices(state.keys) },
        { "previousKeys", pressedIndices(state.previousKeys) },
        { "mouse", {
            { "buttons", pressedIndices(state.mouseButtons) },
            { "previousButtons", pressedIndices(state.previousMouseButtons) },
            { "x", state.mouseX }, { "y", state.mouseY }, { "wheel", state.mouseWheel },
            { "previousX", state.previousMouseX }, { "previousY", state.previousMouseY },
            { "previousWheel", state.previousMouseWheel }
        } },
        { "gamepad", {
            { "buttons", state.gamepadButtons }, { "previousButtons", state.previousGamepadButtons },
            { "leftTrigger", state.leftTrigger }, { "rightTrigger", state.rightTrigger },
            { "previousLeftTrigger", state.previousLeftTrigger },
            { "previousRightTrigger", state.previousRightTrigger },
            { "leftX", state.leftX }, { "leftY", state.leftY },
            { "rightX", state.rightX }, { "rightY", state.rightY },
            { "previousLeftX", state.previousLeftX }, { "previousLeftY", state.previousLeftY },
            { "previousRightX", state.previousRightX }, { "previousRightY", state.previousRightY },
            { "mode", state.gamepadMode }
        } },
        { "accelerometer", Vector3ToJson(state.accelerometer) },
        { "gyroscope", Vector3ToJson(state.gyroscope) },
        { "baseAccelerometer", Vector3ToJson(state.baseAccelerometer) }
    };
}

bool ReplayDebugger::DeserializeInputState(
    const json& data,
    InputManager::ReplayState& state,
    std::string& error) const {
    if (!data.is_object()) {
        error = "入力データの構造が不正です。";
        return false;
    }

    state = {};
    auto readPressedIndices = [&error](const json& values, auto& destination, const char* label) {
        if (!values.is_array()) {
            error = std::string(label) + " のキー配列が不正です。";
            return false;
        }
        for (const json& value : values) {
            if (!value.is_number_unsigned() && !value.is_number_integer()) {
                error = std::string(label) + " に数値以外が含まれています。";
                return false;
            }
            const int index = value.get<int>();
            if (index < 0 || index >= static_cast<int>(destination.size())) {
                error = std::string(label) + " のキー番号が範囲外です。";
                return false;
            }
            destination[static_cast<std::size_t>(index)] = 0x80u;
        }
        return true;
    };

    if (!readPressedIndices(data.value("keys", json::array()), state.keys, "keys") ||
        !readPressedIndices(data.value("previousKeys", json::array()), state.previousKeys, "previousKeys")) {
        return false;
    }

    const json mouse = data.value("mouse", json::object());
    if (!readPressedIndices(mouse.value("buttons", json::array()), state.mouseButtons, "mouse.buttons") ||
        !readPressedIndices(mouse.value("previousButtons", json::array()), state.previousMouseButtons, "mouse.previousButtons")) {
        return false;
    }
    state.mouseX = mouse.value("x", 0);
    state.mouseY = mouse.value("y", 0);
    state.mouseWheel = mouse.value("wheel", 0);
    state.previousMouseX = mouse.value("previousX", 0);
    state.previousMouseY = mouse.value("previousY", 0);
    state.previousMouseWheel = mouse.value("previousWheel", 0);

    const json gamepad = data.value("gamepad", json::object());
    auto readInt16 = [&gamepad](const char* key) {
        return static_cast<int16_t>((std::clamp)(gamepad.value(key, 0), -32768, 32767));
    };
    state.gamepadButtons = static_cast<uint16_t>((std::clamp)(gamepad.value("buttons", 0), 0, 65535));
    state.previousGamepadButtons = static_cast<uint16_t>((std::clamp)(gamepad.value("previousButtons", 0), 0, 65535));
    state.leftTrigger = static_cast<uint8_t>((std::clamp)(gamepad.value("leftTrigger", 0), 0, 255));
    state.rightTrigger = static_cast<uint8_t>((std::clamp)(gamepad.value("rightTrigger", 0), 0, 255));
    state.previousLeftTrigger = static_cast<uint8_t>((std::clamp)(gamepad.value("previousLeftTrigger", 0), 0, 255));
    state.previousRightTrigger = static_cast<uint8_t>((std::clamp)(gamepad.value("previousRightTrigger", 0), 0, 255));
    state.leftX = readInt16("leftX");
    state.leftY = readInt16("leftY");
    state.rightX = readInt16("rightX");
    state.rightY = readInt16("rightY");
    state.previousLeftX = readInt16("previousLeftX");
    state.previousLeftY = readInt16("previousLeftY");
    state.previousRightX = readInt16("previousRightX");
    state.previousRightY = readInt16("previousRightY");
    state.gamepadMode = gamepad.value("mode", false);
    state.accelerometer = JsonToVector3(data.value("accelerometer", json::array()));
    state.gyroscope = JsonToVector3(data.value("gyroscope", json::array()));
    state.baseAccelerometer = JsonToVector3(data.value("baseAccelerometer", json::array()));
    return true;
}

json ReplayDebugger::SerializeInputSample(const InputSample& sample, double firstFrameTime) const {
    return {
        { "time", (std::max)(0.0, sample.time - firstFrameTime) },
        { "deltaTime", sample.deltaTime },
        { "state", SerializeInputState(sample.state) }
    };
}

bool ReplayDebugger::DeserializeInputSample(
    const json& data,
    InputSample& sample,
    std::string& error) const {
    if (!data.is_object() || !data.contains("state")) {
        error = "入力サンプルの構造が不正です。";
        return false;
    }
    sample.time = (std::max)(0.0, data.value("time", 0.0));
    sample.deltaTime = (std::clamp)(data.value("deltaTime", 0.0f), 0.0f, 0.25f);
    if (sample.deltaTime <= 0.0f) {
        error = "入力サンプルのdeltaTimeが不正です。";
        return false;
    }
    return DeserializeInputState(data.at("state"), sample.state, error);
}
bool ReplayDebugger::SaveCurrentReplayArchive() {
    if (!HasFrames() || loadedArchiveReadOnly_) {
        statusMessage_ = loadedArchiveReadOnly_
            ? "読み込んだ保存リプレイは再保存できません。"
            : "保存できるリプレイ履歴がありません。";
        return false;
    }

    std::error_code fileError;
    const std::filesystem::path archiveDirectory = GetReplayArchiveDirectory();
    std::filesystem::create_directories(archiveDirectory, fileError);
    if (fileError) {
        statusMessage_ = "保存フォルダーを作成できません: " + fileError.message();
        return false;
    }

    std::string fileStem = "replay_" + MakeLocalTimestamp("%Y%m%d_%H%M%S");
    std::filesystem::path archivePath = archiveDirectory / (fileStem + ".cgr");
    for (int suffix = 1; std::filesystem::exists(archivePath); ++suffix) {
        archivePath = archiveDirectory / (fileStem + "_" + std::to_string(suffix) + ".cgr");
    }
    const std::filesystem::path temporaryPath = archivePath.string() + ".tmp";

    const SceneLoadContext sceneContext = sceneManager_->GetActiveSceneLoadContext();
    const std::string currentSceneName = sceneManager_->GetCurrentSceneName();
    const std::string sceneLabel = !sceneContext.displayName.empty()
        ? sceneContext.displayName
        : (!sceneContext.sceneAssetId.empty() ? sceneContext.sceneAssetId : currentSceneName);
    const double firstFrameTime = frames_.front().time;
    const double duration = frames_.size() >= 2 ? frames_.back().time - firstFrameTime : 0.0;
    const std::size_t savedInputSampleCount = static_cast<std::size_t>(std::count_if(
        inputSamples_.begin(), inputSamples_.end(), [firstFrameTime](const InputSample& sample) {
            return sample.time > firstFrameTime && sample.deltaTime > 0.0f;
        }));
    json header = {
        { "schemaVersion", kReplayArchiveSchemaVersion },
        { "createdAt", MakeLocalTimestamp("%Y-%m-%d %H:%M:%S") },
        { "sceneName", currentSceneName }, { "sceneLabel", sceneLabel },
        { "sceneContext", SceneLoadContextToJson(sceneContext) },
        { "captureRate", captureRate_ }, { "frameCount", frames_.size() },
        { "inputSampleCount", savedInputSampleCount },
        { "durationSeconds", duration },
    };
    json payload;
    payload["frames"] = json::array();
    for (const FrameSnapshot& frame : frames_) {
        payload["frames"].push_back(SerializeFrame(frame, firstFrameTime));
    }
    payload["inputs"] = json::array();
    for (const InputSample& sample : inputSamples_) {
        if (sample.time <= firstFrameTime || sample.deltaTime <= 0.0f) {
            continue;
        }
        payload["inputs"].push_back(SerializeInputSample(sample, firstFrameTime));
    }

    try {
        const std::vector<std::uint8_t> binaryPayload = json::to_cbor(payload);
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream) {
            statusMessage_ = "リプレイ保存ファイルを作成できません。";
            return false;
        }
        stream << kReplayArchiveMagic << '\n' << header.dump() << '\n';
        stream.write(reinterpret_cast<const char*>(binaryPayload.data()), static_cast<std::streamsize>(binaryPayload.size()));
        stream.close();
        if (!stream) {
            std::filesystem::remove(temporaryPath, fileError);
            statusMessage_ = "リプレイデータの書き込みに失敗しました。";
            return false;
        }
    } catch (const std::exception& exception) {
        std::filesystem::remove(temporaryPath, fileError);
        statusMessage_ = std::string("リプレイ保存失敗: ") + exception.what();
        return false;
    }

    std::filesystem::rename(temporaryPath, archivePath, fileError);
    if (fileError) {
        std::filesystem::remove(temporaryPath, fileError);
        statusMessage_ = "リプレイ保存の確定に失敗しました: " + fileError.message();
        return false;
    }

    statusMessage_ = "リプレイを保存しました: " + archivePath.filename().string();
    RefreshReplayArchiveList();
    return true;
}

void ReplayDebugger::RefreshReplayArchiveList() {
    replayArchiveEntries_.clear();
    selectedArchiveIndex_ = -1;
    std::error_code fileError;
    const std::filesystem::path archiveDirectory = GetReplayArchiveDirectory();
    std::filesystem::create_directories(archiveDirectory, fileError);
    if (fileError) {
        statusMessage_ = "保存リプレイ一覧を開けません: " + fileError.message();
        return;
    }

    for (const std::filesystem::directory_entry& file : std::filesystem::directory_iterator(archiveDirectory, fileError)) {
        if (fileError) break;
        if (!file.is_regular_file() || file.path().extension() != ".cgr") continue;

        ReplayArchiveEntry entry;
        entry.filePath = file.path().string();
        entry.fileName = file.path().filename().string();
        json header;
        entry.valid = ReadArchiveHeader(file.path(), header, entry.error);
        if (entry.valid) {
            entry.createdAt = header.value("createdAt", std::string{});
            entry.sceneLabel = header.value("sceneLabel", header.value("sceneName", std::string{}));
            entry.frameCount = header.value("frameCount", std::size_t{ 0 });
            entry.durationSeconds = header.value("durationSeconds", 0.0);
            entry.inputSampleCount = header.value("inputSampleCount", std::size_t{ 0 });
            entry.regressionReady = entry.frameCount >= 2 && entry.inputSampleCount > 0;
        }
        replayArchiveEntries_.push_back(std::move(entry));
    }
    std::sort(replayArchiveEntries_.begin(), replayArchiveEntries_.end(),
        [](const ReplayArchiveEntry& lhs, const ReplayArchiveEntry& rhs) {
            return lhs.fileName > rhs.fileName;
        });
    if (!replayArchiveEntries_.empty()) selectedArchiveIndex_ = 0;
}

bool ReplayDebugger::RequestLoadReplayArchive(std::size_t archiveIndex) {
    if (!sceneManager_ || archiveIndex >= replayArchiveEntries_.size()) return false;
    if (!sceneManager_->IsPlaying() && debugEditor_ && debugEditor_->HasAnyDirty()) {
        statusMessage_ = "未保存のシーン編集があります。編集内容を保存してからリプレイを読み込んでください。";
        return false;
    }
    const ReplayArchiveEntry& entry = replayArchiveEntries_[archiveIndex];
    if (!entry.valid) {
        statusMessage_ = "保存リプレイを読み込めません: " + entry.error;
        return false;
    }

    const std::filesystem::path archivePath(entry.filePath);
    std::error_code fileError;
    constexpr std::uintmax_t kMaximumArchiveBytes = 512ull * 1024ull * 1024ull;
    const std::uintmax_t archiveBytes = std::filesystem::file_size(archivePath, fileError);
    if (fileError || archiveBytes > kMaximumArchiveBytes) {
        statusMessage_ = fileError
            ? "保存リプレイのサイズを確認できません。"
            : "保存リプレイが大きすぎます（上限512MB）。";
        return false;
    }
    std::ifstream stream(archivePath, std::ios::binary);
    std::string magic;
    std::string headerLine;
    if (!stream || !std::getline(stream, magic) || magic != kReplayArchiveMagic || !std::getline(stream, headerLine)) {
        statusMessage_ = "保存リプレイのヘッダーを読み込めません。";
        return false;
    }

    try {
        const json header = json::parse(headerLine);
        if (!IsSupportedReplayArchiveSchema(header.value("schemaVersion", 0))) {
            statusMessage_ = "未対応の保存リプレイバージョンです。";
            return false;
        }
        const std::istreambuf_iterator<char> payloadBegin(stream);
        const std::istreambuf_iterator<char> payloadEnd;
        const std::vector<std::uint8_t> bytes(payloadBegin, payloadEnd);
        const json payload = json::from_cbor(bytes, true, false);
        if (payload.is_discarded() || !payload.contains("frames") || !payload.at("frames").is_array()) {
            statusMessage_ = "保存リプレイ本体が破損しています。";
            return false;
        }
        if (payload.at("frames").empty() || payload.at("frames").size() > 100000) {
            statusMessage_ = "保存リプレイのフレーム数が不正です。";
            return false;
        }

        PendingReplayArchive archive;
        archive.filePath = entry.filePath;
        archive.createdAt = header.value("createdAt", std::string{});
        archive.sceneName = header.value("sceneName", std::string{});
        archive.sceneContext = JsonToSceneLoadContext(header.value("sceneContext", json::object()));
        archive.captureRate = (std::clamp)(header.value("captureRate", 15.0f), kMinimumCaptureRate, kMaximumCaptureRate);
        for (const json& frameData : payload.at("frames")) {
            FrameSnapshot frame;
            std::string frameError;
            if (!DeserializeFrame(frameData, frame, frameError)) {
                statusMessage_ = frameError;
                return false;
            }
            archive.frames.push_back(std::move(frame));
        }
        if (const auto inputs = payload.find("inputs"); inputs != payload.end()) {
            if (!inputs->is_array() || inputs->size() > 1000000) {
                statusMessage_ = "保存リプレイの入力サンプル数が不正です。";
                return false;
            }
            archive.inputSamples.reserve(inputs->size());
            for (const json& inputData : *inputs) {
                InputSample sample;
                std::string inputError;
                if (!DeserializeInputSample(inputData, sample, inputError)) {
                    statusMessage_ = inputError;
                    return false;
                }
                archive.inputSamples.push_back(std::move(sample));
            }
        }
        pendingReplayArchive_ = std::move(archive);
    } catch (const std::exception& exception) {
        statusMessage_ = std::string("保存リプレイ読込失敗: ") + exception.what();
        return false;
    }

    statusMessage_ = "保存リプレイの元シーンを読み込みます。";
    if (!RequestPendingArchiveScene()) {
        pendingReplayArchive_.reset();
        return false;
    }
    return true;
}

bool ReplayDebugger::RequestStartRegression(std::size_t archiveIndex) {
    if (archiveIndex >= replayArchiveEntries_.size() || !replayArchiveEntries_[archiveIndex].regressionReady) {
        statusMessage_ = "この保存リプレイには入力列がないため、自動回帰テストを実行できません。";
        return false;
    }
    pendingRegressionStart_ = true;
    if (!RequestLoadReplayArchive(archiveIndex)) {
        pendingRegressionStart_ = false;
        return false;
    }
    return true;
}

bool ReplayDebugger::RequestPendingArchiveScene() {
    if (!sceneManager_ || !pendingReplayArchive_) return false;
    if (IsPendingArchiveSceneCurrent()) return true;

    const SceneLoadContext& context = pendingReplayArchive_->sceneContext;
    if (context.IsSceneAsset() && sceneManager_->IsPlaying()) {
        statusMessage_ = "保存元と別のScene Assetです。Playを停止してから、もう一度読込してください。";
        return false;
    }

    ResetForSceneChange();
    if (context.IsSceneAsset()) {
        if (!sceneManager_->OpenSceneAsset(context)) {
            statusMessage_ = "保存元のScene Assetを開けませんでした。";
            return false;
        }
        return true;
    }
    if (pendingReplayArchive_->sceneName.empty() ||
        !sceneManager_->IsSceneRegistered(pendingReplayArchive_->sceneName)) {
        statusMessage_ = "保存元の実行シーンが登録されていません。";
        return false;
    }
    sceneManager_->ChangeScene(pendingReplayArchive_->sceneName);
    return true;
}

bool ReplayDebugger::IsPendingArchiveSceneCurrent() const {
    if (!sceneManager_ || !pendingReplayArchive_ || sceneManager_->IsTransitioning() ||
        !sceneManager_->GetCurrentScene()) {
        return false;
    }
    const SceneLoadContext& expected = pendingReplayArchive_->sceneContext;
    if (expected.IsSceneAsset()) {
        const SceneLoadContext& current = sceneManager_->GetActiveSceneLoadContext();
        return current.sceneAssetId == expected.sceneAssetId && current.runtimeScene == expected.runtimeScene;
    }
    return sceneManager_->GetCurrentSceneName() == pendingReplayArchive_->sceneName;
}

bool ReplayDebugger::TryCompletePendingArchiveLoad(bool isPlaying) {
    if (!pendingReplayArchive_) return false;
    if (sceneManager_->IsTransitioning() || !IsPendingArchiveSceneCurrent()) return true;
    if (!isPlaying) {
        statusMessage_ = "元シーンを開きました。上部の再生ボタンでPlayを開始すると保存履歴を表示します。";
        return true;
    }

    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene) return true;

    ReleaseRetainedObjects();
    frames_.clear();
    activeScene_ = scene;
    activeSceneGeneration_ = sceneManager_->GetSceneGeneration();
    RebindPendingArchiveToCurrentScene(scene);
    frames_ = std::move(pendingReplayArchive_->frames);
    inputSamples_ = std::move(pendingReplayArchive_->inputSamples);
    captureRate_ = pendingReplayArchive_->captureRate;
    loadedArchiveName_ = std::filesystem::path(pendingReplayArchive_->filePath).filename().string();
    pendingReplayArchive_.reset();

    cursor_ = 0;
    mode_ = Mode::Paused;
    captureAccumulator_ = 0.0f;
    playbackTime_ = frames_.empty() ? 0.0f : static_cast<float>(frames_.front().time);
    timelineTime_ = frames_.empty() ? 0.0 : frames_.back().time;
    selectedReplayId_ = 0;
    selectedSpriteReplayId_ = 0;
    loadedArchiveReadOnly_ = true;
    RecalculateMemoryEstimate();

    if (frames_.empty()) {
        loadedArchiveReadOnly_ = false;
        statusMessage_ = "保存リプレイにフレームがありません。";
        return true;
    }
    const auto player = std::find_if(frames_.front().objects.begin(), frames_.front().objects.end(),
        [](const ObjectSnapshot& snapshot) {
            return snapshot.className == "Player" || snapshot.name == "player";
        });
    if (player != frames_.front().objects.end()) selectedReplayId_ = player->replayId;
    else if (!frames_.front().objects.empty()) selectedReplayId_ = frames_.front().objects.front().replayId;
    if (!frames_.front().sprites.empty()) selectedSpriteReplayId_ = frames_.front().sprites.front().replayId;

    ClearTransientRuntime();
    ApplyFrame(0);
    if (pendingRegressionStart_) {
        pendingRegressionStart_ = false;
        if (!StartRegression()) {
            mode_ = Mode::Paused;
        }
        return true;
    }

    std::string resumeBlockReason;
    if (CanResumeLoadedArchive(&resumeBlockReason)) {
        statusMessage_ = "読込完了。「再生・確認」タブで履歴を再生できます: " +
            loadedArchiveName_;
        if (recreatedArchiveObjectCount_ > 0) {
            statusMessage_ += "（動的Object " + std::to_string(recreatedArchiveObjectCount_) + "件を再生成）";
        }
    } else {
        statusMessage_ = "保存リプレイを閲覧用に読み込みました: " + loadedArchiveName_ +
            "（分岐停止理由: " + resumeBlockReason + "）";
    }
    return true;
}



#endif
