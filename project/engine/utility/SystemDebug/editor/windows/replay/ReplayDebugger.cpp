#include "engine/utility/SystemDebug/editor/windows/replay/ReplayDebugger.Internal.h"

#ifdef USE_IMGUI
using namespace ReplayDebuggerImplementation;

void ReplayDebugger::Initialize(SceneManager* sceneManager, DebugEditor* debugEditor) {
    sceneManager_ = sceneManager;
    debugEditor_ = debugEditor;
    RefreshReplayArchiveList();
}

void ReplayDebugger::Finalize() {
    pendingReplayArchive_.reset();
    ResetForSceneChange();
    sceneManager_ = nullptr;
    debugEditor_ = nullptr;
}

bool ReplayDebugger::ShouldFreezeSimulation() const {
    return (mode_ == Mode::Paused || mode_ == Mode::Playback) &&
        GetValidatedActiveScene() != nullptr;
}

float ReplayDebugger::ResolveSimulationDeltaTime(float defaultDeltaTime) const {
    if (mode_ == Mode::Regression && regressionStepPrepared_) {
        return regressionStepDeltaTime_;
    }
    return defaultDeltaTime;
}


bool ReplayDebugger::HasFrames() const {
    return !frames_.empty() && GetValidatedActiveScene() != nullptr;
}

bool ReplayDebugger::BeginPlayInEditorSnapshot() {
    if (!sceneManager_ || sceneManager_->IsTransitioning()) {
        return false;
    }

    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene) {
        return false;
    }

    ResetForSceneChange();
    activeScene_ = scene;
    activeSceneGeneration_ = sceneManager_->GetSceneGeneration();
    cursor_ = 0;
    timelineTime_ = 0.0;
    captureAccumulator_ = 0.0f;
    playbackTime_ = 0.0f;
    mode_ = Mode::Idle;
    CaptureFrame(0.0);
    if (frames_.empty()) {
        ResetForSceneChange();
        return false;
    }

    // 通常のReplay履歴をユーザーが消してもStop復元に影響しないよう、基準フレームを分離保持します。
    playInEditorSnapshot_ = frames_.front();
    playBaselinePersistentGuids_.clear();
    playBaselinePersistentGuids_.reserve(playInEditorSnapshot_->objects.size());
    for (const ObjectSnapshot& snapshot : playInEditorSnapshot_->objects) {
        if (!snapshot.persistentGuid.empty()) {
            playBaselinePersistentGuids_.insert(snapshot.persistentGuid);
        }
    }
    mode_ = autoRecord_ ? Mode::Recording : Mode::Idle;
    statusMessage_ = "Play開始直前の編集状態を保持しました。";
    return true;
}

bool ReplayDebugger::RestorePlayInEditorSnapshot() {
    BaseScene* scene = GetValidatedActiveScene();
    if (!scene || !playInEditorSnapshot_) {
        return false;
    }

    // ApplyFrameの既存復元経路を利用するため、独立保持した基準フレームを履歴先頭へ戻します。
    const FrameSnapshot baseline = *playInEditorSnapshot_;
    frames_.push_front(baseline);
    ApplyFrame(0);

    // Play中に生まれたObjectは基準フレームへ戻すだけでは残るため、記録時のIDで差分を探す。
    std::unordered_set<uint64_t> baselineObjectIds;
    baselineObjectIds.reserve(baseline.objects.size());
    for (const ObjectSnapshot& snapshot : baseline.objects) {
        baselineObjectIds.insert(snapshot.replayId);
    }

    std::vector<Object3d*> runtimeObjects;
    for (auto& object : scene->GetObjects()) {
        if (!object || object->IsEditorInternal()) {
            continue;
        }
        if (!baselineObjectIds.contains(object->EnsureReplayId())) {
            runtimeObjects.push_back(object.get());
        }
    }
    for (Object3d* object : runtimeObjects) {
        if (!object->IsReplayRetained()) {
            scene->DestroyObject(object);
        }
    }

    // SpriteもObjectとは別に寿命を管理するので、同じ基準との差分を削除・無効化する。
    std::unordered_set<uint64_t> baselineSpriteIds;
    baselineSpriteIds.reserve(baseline.sprites.size());
    for (const SpriteSnapshot& snapshot : baseline.sprites) {
        baselineSpriteIds.insert(snapshot.replayId);
    }

    std::vector<Sprite*> currentSprites;
    scene->CollectReplaySprites(currentSprites);
    std::unordered_set<Sprite*> uniqueSprites;
    std::vector<Sprite*> runtimeSprites;
    for (Sprite* sprite : currentSprites) {
        if (!sprite || !uniqueSprites.insert(sprite).second) {
            continue;
        }
        if (!baselineSpriteIds.contains(sprite->EnsureReplayId())) {
            runtimeSprites.push_back(sprite);
        }
    }
    for (Sprite* sprite : runtimeSprites) {
        if (sprite->IsReplayRetained()) {
            continue;
        }
        if (!scene->DestroySprite(sprite)) {
            // 派生Sceneが個別unique_ptrで持つSpriteは共通配列から削除できないため、無効状態へ戻します。
            sprite->SetParent(nullptr, false);
            sprite->SetReplayRemoved(true);
            sprite->SetVisible(false);
        }
    }

    ClearTransientRuntime();
    ReleaseRetainedObjects();
    ResetForSceneChange();
    return true;
}

void ReplayDebugger::ToggleSimulationPause() {
    if (!HasFrames()) {
        if (!frames_.empty()) {
            ResetForSceneChange();
        }
        return;
    }

    if (mode_ == Mode::Recording) {
        PauseAt(frames_.size() - 1);
        statusMessage_ = "Main Menu Barから実行を一時停止しました。";
        return;
    }

    if (mode_ == Mode::Paused || mode_ == Mode::Playback) {
        if (loadedArchiveReadOnly_) {
            if (mode_ == Mode::Playback) {
                PauseAt(cursor_);
            } else {
                StartPlayback();
            }
            return;
        }
        ResumeFromCursor();
    }
}

void ReplayDebugger::UpdateBeforeSimulation(float realDeltaTime, bool isPlaying) {
    if (!sceneManager_) {
        return;
    }

    if (pendingReplayArchive_ && TryCompletePendingArchiveLoad(isPlaying)) {
        wasPlaying_ = isPlaying;
        return;
    }

    if (!isPlaying) {
        if (!isPlaying && wasPlaying_) {
            ResetForSceneChange();
        }
        wasPlaying_ = isPlaying;
        return;
    }

    if (sceneManager_->IsTransitioning()) {
        if (activeScene_ || !frames_.empty()) {
            ResetForSceneChange();
        }
        wasPlaying_ = isPlaying;
        return;
    }

    BaseScene* scene = sceneManager_->GetCurrentScene();
    if (!scene) {
        wasPlaying_ = isPlaying;
        return;
    }

    // アドレスが再利用されても古い履歴を別シーンへ適用しないよう、ポインタと世代を両方確認する。
    if (activeScene_ != scene || activeSceneGeneration_ != sceneManager_->GetSceneGeneration()) {
        const bool replacedScene = activeScene_ != nullptr;
        ResetForSceneChange();
        activeScene_ = scene;
        activeSceneGeneration_ = sceneManager_->GetSceneGeneration();
        cursor_ = 0;
        timelineTime_ = 0.0;
        captureAccumulator_ = 0.0f;
        playbackTime_ = 0.0f;
        mode_ = Mode::Idle;
        selectedReplayId_ = 0;
        selectedSpriteReplayId_ = 0;
        estimatedMemoryBytes_ = 0;
        if (replacedScene) {
            statusMessage_ = "シーンが切り替わったため、リプレイ履歴を初期化しました。";
        }
    }

    if (mode_ == Mode::Regression) {
        UpdateRegressionBeforeSimulation();
        wasPlaying_ = isPlaying;
        return;
    }

    if (autoRecord_ && mode_ == Mode::Idle) {
        BeginRecording(scene);
    }

    if (mode_ == Mode::Playback && !frames_.empty()) {
        playbackTime_ += (std::max)(0.0f, realDeltaTime) * playbackSpeed_;
        while (cursor_ + 1 < frames_.size() && frames_[cursor_ + 1].time <= playbackTime_) {
            ++cursor_;
        }
        ApplyFrame(cursor_);
        if (cursor_ + 1 >= frames_.size()) {
            mode_ = Mode::Paused;
        }
    }

    wasPlaying_ = isPlaying;
}

void ReplayDebugger::CaptureAfterSimulation(float simulationDeltaTime, bool isPlaying) {
    if (!isPlaying || !activeScene_ || !sceneManager_ || sceneManager_->IsTransitioning()) {
        return;
    }

    if (!GetValidatedActiveScene()) {
        ResetForSceneChange();
        return;
    }

    if (mode_ == Mode::Regression) {
        CaptureRegressionAfterSimulation();
        return;
    }
    if (mode_ != Mode::Recording) {
        return;
    }

    const float safeDeltaTime = (std::max)(0.0f, simulationDeltaTime);

    // 入力は毎ゲーム更新で記録し、Objectの状態はcaptureRate_の間隔で間引いて保持する。
    if (safeDeltaTime > 0.0f) {
        InputSample sample;
        sample.time = timelineTime_ + safeDeltaTime;
        sample.deltaTime = (std::min)(safeDeltaTime, 0.25f);
        sample.state = InputManager::GetInstance()->CaptureReplayState();
        inputSamples_.push_back(std::move(sample));
        estimatedMemoryBytes_ += sizeof(InputSample);
    }
    timelineTime_ += safeDeltaTime;
    captureAccumulator_ += safeDeltaTime;

    const float interval = 1.0f / (std::max)(captureRate_, kMinimumCaptureRate);
    if (captureAccumulator_ < interval) {
        return;
    }

    captureAccumulator_ = std::fmod(captureAccumulator_, interval);
    CaptureFrame(timelineTime_);
}

void ReplayDebugger::BeginRecording(BaseScene* scene) {
    if (!sceneManager_ || sceneManager_->IsTransitioning() ||
        sceneManager_->GetCurrentScene() != scene) {
        return;
    }

    activeScene_ = scene;
    activeSceneGeneration_ = sceneManager_->GetSceneGeneration();
    loadedArchiveReadOnly_ = false;
    loadedArchiveName_.clear();
    mode_ = Mode::Recording;
    captureAccumulator_ = 0.0f;
    playbackTime_ = 0.0f;
    timelineTime_ = frames_.empty() ? 0.0 : frames_.back().time;
    if (frames_.empty()) {
        CaptureFrame(timelineTime_);
    }
}

void ReplayDebugger::CaptureFrame(double time) {
    BaseScene* scene = GetValidatedActiveScene();
    if (!scene) {
        return;
    }

    FrameSnapshot frame;
    frame.time = time;
    auto& objects = scene->GetObjects();
    frame.objects.reserve(objects.size());
    for (auto& object : objects) {
        if (!object || object->IsEditorInternal()) {
            continue;
        }
        object->SetReplayRetained(true);
        ObjectSnapshot snapshot;
        snapshot.replayId = object->EnsureReplayId();
        snapshot.persistentGuid = object->EnsurePersistentGuid();
        snapshot.name = object->GetName();
        snapshot.className = object->GetClassName();
        snapshot.runtimeSpawned =
            playInEditorSnapshot_.has_value() &&
            !playBaselinePersistentGuids_.contains(snapshot.persistentGuid);
        snapshot.saveCategory = object->GetSaveCategory();
        snapshot.enemyType = object->GetEnemyType();
        snapshot.gimmickType = object->GetGimmickType();
        snapshot.itemType = object->GetItemType();
        snapshot.state = object->CaptureReplayState();
        frame.objects.push_back(std::move(snapshot));
    }

    std::vector<Sprite*> replaySprites;
    scene->CollectReplaySprites(replaySprites);
    frame.sprites.reserve(replaySprites.size());
    std::unordered_set<Sprite*> capturedSprites;
    capturedSprites.reserve(replaySprites.size());
    std::unordered_map<std::string, std::size_t> spriteBindingOccurrences;
    for (Sprite* sprite : replaySprites) {
        if (!sprite || !capturedSprites.insert(sprite).second) {
            continue;
        }
        sprite->SetReplayRetained(true);
        SpriteSnapshot snapshot;
        snapshot.replayId = sprite->EnsureReplayId();
        snapshot.name = sprite->GetName().empty() ? "Sprite" : sprite->GetName();
        snapshot.textureName = sprite->GetTextureName();
        const std::string bindingBase = snapshot.name + "|" + snapshot.textureName;
        snapshot.bindingKey = bindingBase + "|" + std::to_string(spriteBindingOccurrences[bindingBase]++);
        snapshot.state = sprite->CaptureReplayState();
        frame.sprites.push_back(std::move(snapshot));
    }

    scene->CaptureReplaySceneState(frame.sceneState);

    if (Camera* camera = CameraManager::GetInstance()->GetActiveCamera()) {
        frame.camera.valid = true;
        frame.camera.eye = camera->GetEye();
        frame.camera.target = camera->GetTargetPoint();
        frame.camera.rotation = camera->GetRotation();
        frame.camera.fovY = camera->GetFovY();
        frame.camera.nearClip = camera->GetNearClip();
        frame.camera.farClip = camera->GetFarClip();
    }

    BuildFrameDiagnostics(frame);
    frame.estimatedBytes = EstimateFrameBytes(frame);
    estimatedMemoryBytes_ += frame.estimatedBytes;
    frames_.push_back(std::move(frame));
    cursor_ = frames_.empty() ? 0 : frames_.size() - 1;
    if (selectedReplayId_ == 0 && !frames_.back().objects.empty()) {
        const auto player = std::find_if(
            frames_.back().objects.begin(),
            frames_.back().objects.end(),
            [](const ObjectSnapshot& snapshot) {
                return snapshot.className == "Player" || snapshot.name == "player";
            });
        selectedReplayId_ = player != frames_.back().objects.end()
            ? player->replayId
            : frames_.back().objects.front().replayId;
    }
    if (selectedSpriteReplayId_ == 0 && !frames_.back().sprites.empty()) {
        selectedSpriteReplayId_ = frames_.back().sprites.front().replayId;
    }
    TrimToCapacity();
}

void ReplayDebugger::ApplyFrame(std::size_t index) {
    BaseScene* scene = GetValidatedActiveScene();
    if (!scene || frames_.empty()) {
        return;
    }

    index = (std::min)(index, frames_.size() - 1);
    FrameSnapshot& frame = frames_[index];
    // Scene固有の状態を先に戻し、その後でObject・Sprite・Cameraの個別状態を復元する。
    scene->RestoreReplaySceneState(frame.sceneState);
    auto& objects = scene->GetObjects();

    std::unordered_map<uint64_t, Object3d*> currentObjects;
    currentObjects.reserve(objects.size());
    for (auto& object : objects) {
        if (object) {
            currentObjects[object->EnsureReplayId()] = object.get();
        }
    }

    std::vector<Sprite*> replaySprites;
    scene->CollectReplaySprites(replaySprites);
    std::unordered_map<uint64_t, Sprite*> currentSprites;
    currentSprites.reserve(replaySprites.size());
    for (Sprite* sprite : replaySprites) {
        if (sprite) {
            currentSprites[sprite->EnsureReplayId()] = sprite;
        }
    }

    std::unordered_set<uint64_t> spriteSnapshotIds;
    spriteSnapshotIds.reserve(frame.sprites.size());
    for (const SpriteSnapshot& snapshot : frame.sprites) {
        spriteSnapshotIds.insert(snapshot.replayId);
    }
    for (auto& [replayId, sprite] : currentSprites) {
        if (!sprite->IsReplayRetained() || spriteSnapshotIds.contains(replayId)) {
            continue;
        }
        sprite->SetReplayRemoved(true);
        sprite->SetVisible(false);
    }

    std::unordered_set<uint64_t> snapshotIds;
    snapshotIds.reserve(frame.objects.size());
    for (const ObjectSnapshot& snapshot : frame.objects) {
        snapshotIds.insert(snapshot.replayId);
    }

    lastMissingSpriteCount_ = 0;
    for (const SpriteSnapshot& snapshot : frame.sprites) {
        const auto found = currentSprites.find(snapshot.replayId);
        if (found == currentSprites.end()) {
            ++lastMissingSpriteCount_;
            continue;
        }
        Sprite* sprite = found->second;
        sprite->RestoreReplayState(snapshot.state);
        sprite->SetReplayRetained(true);
    }

    // Sprite階層は全ローカル状態を戻してから再接続し、親の復元順へ依存させません。
    for (const SpriteSnapshot& snapshot : frame.sprites) {
        const auto found = currentSprites.find(snapshot.replayId);
        if (found == currentSprites.end()) {
            continue;
        }
        Sprite* sprite = found->second;
        Sprite* parent = nullptr;
        if (snapshot.state.parentReplayId != 0) {
            const auto parentFound = currentSprites.find(snapshot.state.parentReplayId);
            if (parentFound != currentSprites.end()) {
                parent = parentFound->second;
            }
        }
        sprite->SetParent(parent, false);
        sprite->RefreshAfterReplayRestore();
    }

    // 選択フレームに存在しない保持Objectは破棄せず隠し、未来フレームで再利用できるようにする。
    CollisionManager* collisionManager = CollisionManager::GetInstance();
    for (auto& [replayId, object] : currentObjects) {
        if (!object->IsReplayRetained() || snapshotIds.contains(replayId)) {
            continue;
        }
        if (!object->IsReplayRemoved()) {
            collisionManager->RemoveObject(object);
        }
        object->SetReplayRemoved(true);
        object->SetIsVisible(false);
        object->isDead = true;
    }

    lastMissingObjectCount_ = 0;
    for (const ObjectSnapshot& snapshot : frame.objects) {
        auto found = currentObjects.find(snapshot.replayId);
        if (found == currentObjects.end()) {
            ++lastMissingObjectCount_;
            continue;
        }

        Object3d* object = found->second;
        const bool wasRemoved = object->IsReplayRemoved();
        object->RestoreReplayState(snapshot.state);
        object->SetReplayRetained(true);
        const bool nowRemoved = object->IsReplayRemoved();
        if (wasRemoved && !nowRemoved) {
            collisionManager->AddObject(object);
        } else if (!wasRemoved && nowRemoved) {
            collisionManager->RemoveObject(object);
        }
    }

    // 親子関係を持つObjectは、全ローカル状態を戻した後にルートから行列を確定します。
    std::unordered_set<Object3d*> managedObjects;
    managedObjects.reserve(objects.size());
    for (const auto& object : objects) {
        if (object && !object->IsReplayRemoved()) {
            managedObjects.insert(object.get());
        }
    }
    for (auto& object : objects) {
        if (!object || object->IsReplayRemoved()) {
            continue;
        }
        Object3d* parent = object->GetParent();
        if (!parent || !managedObjects.contains(parent)) {
            object->UpdateWorldMatrix();
        }
    }

    if (frame.camera.valid) {
        if (Camera* camera = CameraManager::GetInstance()->GetActiveCamera()) {
            camera->ApplyReplayView(
                frame.camera.eye,
                frame.camera.target,
                frame.camera.rotation,
                frame.camera.fovY,
                frame.camera.nearClip,
                frame.camera.farClip);
        }
    }

    cursor_ = index;
}

void ReplayDebugger::PauseAt(std::size_t index) {
    if (!HasFrames()) {
        if (!frames_.empty()) {
            ResetForSceneChange();
        }
        return;
    }
    const bool enteringPause = mode_ == Mode::Recording || mode_ == Mode::Playback;
    mode_ = Mode::Paused;
    if (enteringPause) {
        ClearTransientRuntime();
    }
    ApplyFrame(index);
}

void ReplayDebugger::StartPlayback() {
    if (!HasFrames()) {
        if (!frames_.empty()) {
            ResetForSceneChange();
        }
        return;
    }
    mode_ = Mode::Playback;
    playbackTime_ = static_cast<float>(frames_[cursor_].time);
    ClearTransientRuntime();
    ApplyFrame(cursor_);
}

bool ReplayDebugger::CanResumeLoadedArchive(std::string* reason) const {
    if (!loadedArchiveReadOnly_) {
        return true;
    }
    auto fail = [reason](const std::string& message) {
        if (reason) {
            *reason = message;
        }
        return false;
    };
    if (!HasFrames()) {
        return fail("復元対象のフレームがありません。");
    }
    if (mode_ != Mode::Paused) {
        return fail("履歴再生を停止してから分岐再開してください。");
    }
    if (!playInEditorSnapshot_) {
        return fail("Play開始時の編集状態を保持できていないため、安全に分岐できません。");
    }
    if (lastMissingObjectCount_ > 0) {
        return fail("現在フレームに復元不能Objectがあるため分岐できません。");
    }
    if (lastMissingSpriteCount_ > 0) {
        return fail("現在フレームに復元不能Spriteがあるため分岐できません。");
    }
    return true;
}

void ReplayDebugger::ResumeFromCursor() {
    if (!HasFrames()) {
        if (!frames_.empty()) {
            ResetForSceneChange();
        }
        return;
    }

    const bool resumeFromArchive = loadedArchiveReadOnly_;
    const std::string archiveName = loadedArchiveName_;
    if (resumeFromArchive) {
        std::string reason;
        if (!CanResumeLoadedArchive(&reason)) {
            statusMessage_ = "保存リプレイから分岐できません: " + reason;
            return;
        }
    }

    ApplyFrame(cursor_);
    if (resumeFromArchive) {
        std::string reason;
        if (!CanResumeLoadedArchive(&reason)) {
            statusMessage_ = "保存リプレイから分岐できません: " + reason;
            return;
        }
    }
    // 分岐後に古い未来の履歴や入力が混ざらないよう、カーソルより後ろを両方破棄する。
    while (frames_.size() > cursor_ + 1) {
        estimatedMemoryBytes_ -= (std::min)(estimatedMemoryBytes_, frames_.back().estimatedBytes);
        frames_.pop_back();
    }
    timelineTime_ = frames_.back().time;
    while (!inputSamples_.empty() && inputSamples_.back().time > timelineTime_) {
        estimatedMemoryBytes_ -= (std::min)(estimatedMemoryBytes_, sizeof(InputSample));
        inputSamples_.pop_back();
    }
    captureAccumulator_ = 0.0f;
    playbackTime_ = static_cast<float>(timelineTime_);
    ClearTransientRuntime();
    mode_ = Mode::Recording;
    loadedArchiveReadOnly_ = false;
    loadedArchiveName_.clear();
    recreatedArchiveObjectCount_ = 0;
    statusMessage_ = resumeFromArchive
        ? "保存リプレイ「" + archiveName + "」の選択フレームからゲームを再開し、新しい時間軸の記録を開始しました。"
        : "選択フレームから分岐し、未来側の履歴を破棄して記録を再開しました。";
}

void ReplayDebugger::StepCursor(int direction) {
    if (!HasFrames()) {
        return;
    }
    const std::ptrdiff_t current = static_cast<std::ptrdiff_t>(cursor_);
    const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(frames_.size() - 1);
    const std::ptrdiff_t next = (std::clamp)(current + direction, std::ptrdiff_t{ 0 }, last);
    PauseAt(static_cast<std::size_t>(next));
}

void ReplayDebugger::ClearHistory(bool continueRecording) {
    if (!continueRecording && !playInEditorSnapshot_) {
        ReleaseRetainedObjects();
    }
    frames_.clear();
    cursor_ = 0;
    inputSamples_.clear();
    timelineTime_ = 0.0;
    captureAccumulator_ = 0.0f;
    playbackTime_ = 0.0f;
    lastMissingObjectCount_ = 0;
    lastMissingSpriteCount_ = 0;
    estimatedMemoryBytes_ = 0;
    recreatedArchiveObjectCount_ = 0;
    loadedArchiveReadOnly_ = false;
    loadedArchiveName_.clear();
    selectedReplayId_ = 0;
    selectedSpriteReplayId_ = 0;
    statusMessage_ = "リプレイ履歴をクリアしました。";
    if (playInEditorSnapshot_) {
        frames_.push_back(*playInEditorSnapshot_);
        estimatedMemoryBytes_ = playInEditorSnapshot_->estimatedBytes;
    }
    mode_ = continueRecording && GetValidatedActiveScene() ? Mode::Recording : Mode::Idle;
    if (mode_ == Mode::Recording) {
        CaptureFrame(0.0);
    }
}

BaseScene* ReplayDebugger::GetValidatedActiveScene(bool allowTransition) const {
    if (!sceneManager_ || !activeScene_) {
        return nullptr;
    }
    if (!allowTransition && sceneManager_->IsTransitioning()) {
        return nullptr;
    }
    if (sceneManager_->GetSceneGeneration() != activeSceneGeneration_) {
        return nullptr;
    }

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    return currentScene == activeScene_ ? currentScene : nullptr;
}

void ReplayDebugger::ReleaseRetainedObjects() {
    BaseScene* scene = GetValidatedActiveScene(true);
    if (!scene) {
        return;
    }

    auto& objects = scene->GetObjects();
    for (auto& object : objects) {
        if (!object || !object->IsReplayRetained()) {
            continue;
        }
        object->SetReplayRetained(false);
        if (object->IsReplayRemoved()) {
            scene->RequestRemoveObject(object.get());
        }
    }
    scene->ReleaseReplaySprites();
}

void ReplayDebugger::ClearTransientRuntime() {
    // 履歴に含めない一時エフェクトは巻き戻し直後の画面に残さない。
    BulletManager::GetInstance()->Clear();
    MeshEffectManager::GetInstance()->Clear();
    DebrisEffectManager::GetInstance()->Clear();
    GPUParticleManager::GetInstance()->ClearAllAutoEmitters();
    if (BaseScene* scene = GetValidatedActiveScene()) {
        if (ParticleSystem* particles = scene->GetParticleSystem()) {
            particles->Clear();
        }
    }
}

void ReplayDebugger::ResetForSceneChange() {
    const bool preservePendingArchive = pendingReplayArchive_.has_value();
    ReleaseRetainedObjects();
    frames_.clear();
    playInEditorSnapshot_.reset();
    inputSamples_.clear();
    playBaselinePersistentGuids_.clear();
    activeScene_ = nullptr;
    activeSceneGeneration_ = 0;
    cursor_ = 0;
    mode_ = Mode::Idle;
    captureAccumulator_ = 0.0f;
    playbackTime_ = 0.0f;
    timelineTime_ = 0.0;
    lastMissingObjectCount_ = 0;
    lastMissingSpriteCount_ = 0;
    estimatedMemoryBytes_ = 0;
    recreatedArchiveObjectCount_ = 0;
    loadedArchiveReadOnly_ = false;
    loadedArchiveName_.clear();
    selectedReplayId_ = 0;
    selectedSpriteReplayId_ = 0;
    regressionStepPrepared_ = false;
    regressionInputIndex_ = 0;
    regressionExpectedFrameIndex_ = 0;
    regressionElapsedTime_ = 0.0;
    pendingRegressionScreenshotPaths_.clear();
    regressionReportPending_ = false;
    if (!preservePendingArchive) {
        pendingRegressionStart_ = false;
        statusMessage_.clear();
    }
}

void ReplayDebugger::TrimToCapacity() {
    const std::size_t maxFrames = GetMaxFrameCount();
    while (frames_.size() > maxFrames) {
        estimatedMemoryBytes_ -= (std::min)(estimatedMemoryBytes_, frames_.front().estimatedBytes);
        frames_.pop_front();
        if (cursor_ > 0) {
            --cursor_;
        }
    }
    if (!frames_.empty()) {
        const double oldestFrameTime = frames_.front().time;
        while (!inputSamples_.empty() && inputSamples_.front().time <= oldestFrameTime) {
            estimatedMemoryBytes_ -= (std::min)(estimatedMemoryBytes_, sizeof(InputSample));
            inputSamples_.erase(inputSamples_.begin());
        }
    }
}

void ReplayDebugger::BuildFrameDiagnostics(FrameSnapshot& frame) const {
    for (const ObjectSnapshot& snapshot : frame.objects) {
        if (!snapshot.state.replayRemoved) {
            ++frame.diagnostics.activeObjects;
        }
    }
    for (const SpriteSnapshot& snapshot : frame.sprites) {
        if (!snapshot.state.replayRemoved) {
            ++frame.diagnostics.activeSprites;
        }
    }
    if (frames_.empty()) {
        return;
    }

    const FrameSnapshot& previous = frames_.back();
    std::unordered_map<uint64_t, const ObjectSnapshot*> previousObjects;
    previousObjects.reserve(previous.objects.size());
    for (const ObjectSnapshot& snapshot : previous.objects) {
        previousObjects[snapshot.replayId] = &snapshot;
    }

    std::unordered_set<uint64_t> currentIds;
    currentIds.reserve(frame.objects.size());
    for (const ObjectSnapshot& current : frame.objects) {
        currentIds.insert(current.replayId);
        const auto found = previousObjects.find(current.replayId);
        if (found == previousObjects.end()) {
            if (!current.state.replayRemoved) {
                ++frame.diagnostics.spawnedObjects;
                ++frame.diagnostics.changedObjects;
            }
            continue;
        }

        const ObjectSnapshot& before = *found->second;
        if (HasMeaningfulChange(before, current)) {
            ++frame.diagnostics.changedObjects;
        }
        if (!before.state.replayRemoved && current.state.replayRemoved) {
            ++frame.diagnostics.removedObjects;
        }
        if (!before.state.dead && current.state.dead) {
            ++frame.diagnostics.deaths;
        }
        if (before.state.hasParameter && current.state.hasParameter &&
            std::abs(before.state.parameter.hp - current.state.parameter.hp) > 0.001f) {
            ++frame.diagnostics.hpChanges;
        }
    }

    for (const ObjectSnapshot& before : previous.objects) {
        if (!currentIds.contains(before.replayId) && !before.state.replayRemoved) {
            ++frame.diagnostics.removedObjects;
            ++frame.diagnostics.changedObjects;
        }
    }

    std::unordered_map<uint64_t, const SpriteSnapshot*> previousSprites;
    previousSprites.reserve(previous.sprites.size());
    for (const SpriteSnapshot& snapshot : previous.sprites) {
        previousSprites[snapshot.replayId] = &snapshot;
    }
    std::unordered_set<uint64_t> currentSpriteIds;
    currentSpriteIds.reserve(frame.sprites.size());
    for (const SpriteSnapshot& current : frame.sprites) {
        currentSpriteIds.insert(current.replayId);
        const auto found = previousSprites.find(current.replayId);
        if (found == previousSprites.end() || HasMeaningfulChange(*found->second, current)) {
            ++frame.diagnostics.changedSprites;
        }
    }
    for (const SpriteSnapshot& before : previous.sprites) {
        if (!currentSpriteIds.contains(before.replayId)) {
            ++frame.diagnostics.changedSprites;
        }
    }
}

void ReplayDebugger::RecalculateMemoryEstimate() {
    estimatedMemoryBytes_ = 0;
    for (const FrameSnapshot& frame : frames_) {
        estimatedMemoryBytes_ += frame.estimatedBytes;
    }
    estimatedMemoryBytes_ += inputSamples_.capacity() * sizeof(InputSample);
}

std::size_t ReplayDebugger::EstimateFrameBytes(const FrameSnapshot& frame) const {
    std::size_t bytes = sizeof(FrameSnapshot) +
        frame.objects.capacity() * sizeof(ObjectSnapshot) +
        frame.sprites.capacity() * sizeof(SpriteSnapshot);
    for (const ObjectSnapshot& snapshot : frame.objects) {
        bytes += snapshot.persistentGuid.capacity();
        bytes += snapshot.name.capacity();
        bytes += snapshot.className.capacity();
        bytes += snapshot.saveCategory.capacity();
        bytes += snapshot.enemyType.capacity();
        bytes += snapshot.gimmickType.capacity();
        bytes += snapshot.itemType.capacity();
        bytes += snapshot.state.animationName.capacity();
        bytes += snapshot.state.animatorControllerPath.capacity();
        bytes += snapshot.state.animatorSnapshot.currentState.capacity();
        bytes += snapshot.state.animatorSnapshot.previousState.capacity();
        bytes += snapshot.state.modelName.capacity();
        bytes += snapshot.state.texturePath.capacity();
        bytes += snapshot.state.custom.dump().size();
    }
    for (const SpriteSnapshot& snapshot : frame.sprites) {
        bytes += snapshot.bindingKey.capacity();
        bytes += snapshot.textureName.capacity();
        bytes += snapshot.name.capacity();
    }
    bytes += frame.sceneState.dump().size();
    return bytes;
}

void ReplayDebugger::RebindPendingArchiveToCurrentScene(BaseScene* scene) {
    if (!pendingReplayArchive_ || !scene) return;

    recreatedArchiveObjectCount_ = 0;
    std::unordered_map<std::string, Object3d*> objectsByGuid;
    std::unordered_map<std::string, std::vector<Object3d*>> objectsBySignature;
    std::unordered_set<uint64_t> currentObjectIds;
    for (auto& object : scene->GetObjects()) {
        if (!object || object->IsEditorInternal()) continue;
        const uint64_t currentId = object->EnsureReplayId();
        currentObjectIds.insert(currentId);
        objectsByGuid.try_emplace(object->GetPersistentGuid(), object.get());
        objectsBySignature[object->GetClassName() + "|" + object->GetName()].push_back(object.get());
    }

    std::unordered_map<uint64_t, uint64_t> objectIdMap;
    std::unordered_map<std::string, std::size_t> signatureOccurrences;
    uint64_t nextMissingObjectId = uint64_t{ 1 } << 63;
    for (FrameSnapshot& frame : pendingReplayArchive_->frames) {
        for (ObjectSnapshot& snapshot : frame.objects) {
            if (objectIdMap.contains(snapshot.replayId)) continue;
            Object3d* current = nullptr;
            if (!snapshot.persistentGuid.empty()) {
                const auto guidMatch = objectsByGuid.find(snapshot.persistentGuid);
                if (guidMatch != objectsByGuid.end()) current = guidMatch->second;
            }
            // 永続GUIDを持つ現行形式では、GUID不一致を同名Objectへ誤接続しません。
            // 名前Fallbackは旧形式互換のため、GUIDが存在しない場合だけ使います。
            if (!current && snapshot.persistentGuid.empty()) {
                const std::string signature = snapshot.className + "|" + snapshot.name;
                auto& occurrence = signatureOccurrences[signature];
                const auto signatureMatch = objectsBySignature.find(signature);
                if (signatureMatch != objectsBySignature.end() && occurrence < signatureMatch->second.size()) {
                    current = signatureMatch->second[occurrence++];
                }
            }
            if (!current && snapshot.runtimeSpawned) {
                json descriptor = {
                    { "name", snapshot.name },
                    { "className", snapshot.className },
                    { "saveCategory", snapshot.saveCategory },
                    { "enemyType", snapshot.enemyType },
                    { "gimmickType", snapshot.gimmickType },
                    { "itemType", snapshot.itemType },
                    { "modelName", snapshot.state.modelName },
                };
                std::unique_ptr<Object3d> recreated = scene->CreateReplayObject(descriptor);
                if (recreated &&
                    (snapshot.persistentGuid.empty() ||
                        recreated->SetPersistentGuid(snapshot.persistentGuid))) {
                    current = recreated.get();
                    current->SetReplayRetained(true);
                    CollisionManager::GetInstance()->AddObject(current);
                    scene->GetObjects().push_back(std::move(recreated));
                    currentObjectIds.insert(current->EnsureReplayId());
                    if (!snapshot.persistentGuid.empty()) {
                        objectsByGuid[snapshot.persistentGuid] = current;
                    }
                    objectsBySignature[current->GetClassName() + "|" + current->GetName()].push_back(current);
                    ++recreatedArchiveObjectCount_;
                }
            }
            if (current) {
                const uint64_t currentId = current->EnsureReplayId();
                objectIdMap[snapshot.replayId] = currentId;
                current->SetReplayRetained(true);
            } else {
                while (currentObjectIds.contains(nextMissingObjectId)) ++nextMissingObjectId;
                objectIdMap[snapshot.replayId] = nextMissingObjectId++;
            }
        }
    }
    if (recreatedArchiveObjectCount_ > 0) {
        scene->OnReplayObjectsRecreated();
    }
    for (FrameSnapshot& frame : pendingReplayArchive_->frames) {
        for (ObjectSnapshot& snapshot : frame.objects) {
            snapshot.replayId = objectIdMap.at(snapshot.replayId);
        }
    }

    std::vector<Sprite*> currentSprites;
    scene->CollectReplaySprites(currentSprites);
    std::unordered_set<Sprite*> uniqueSprites;
    std::unordered_map<std::string, Sprite*> spritesByBindingKey;
    std::unordered_map<std::string, std::size_t> bindingOccurrences;
    std::unordered_set<uint64_t> currentSpriteIds;
    for (Sprite* sprite : currentSprites) {
        if (!sprite || !uniqueSprites.insert(sprite).second) continue;
        const std::string name = sprite->GetName().empty() ? "Sprite" : sprite->GetName();
        const std::string base = name + "|" + sprite->GetTextureName();
        const std::string key = base + "|" + std::to_string(bindingOccurrences[base]++);
        spritesByBindingKey[key] = sprite;
        currentSpriteIds.insert(sprite->EnsureReplayId());
    }

    std::unordered_map<uint64_t, uint64_t> spriteIdMap;
    std::unordered_map<uint64_t, uint32_t> spriteTextureHandles;
    uint64_t nextMissingSpriteId = (uint64_t{ 1 } << 63) | (uint64_t{ 1 } << 62);
    for (FrameSnapshot& frame : pendingReplayArchive_->frames) {
        for (SpriteSnapshot& snapshot : frame.sprites) {
            if (spriteIdMap.contains(snapshot.replayId)) continue;
            const auto match = spritesByBindingKey.find(snapshot.bindingKey);
            if (match != spritesByBindingKey.end()) {
                Sprite* current = match->second;
                const uint64_t currentId = current->EnsureReplayId();
                spriteIdMap[snapshot.replayId] = currentId;
                spriteTextureHandles[currentId] = current->GetTextureHandle();
                current->SetReplayRetained(true);
            } else {
                while (currentSpriteIds.contains(nextMissingSpriteId)) ++nextMissingSpriteId;
                spriteIdMap[snapshot.replayId] = nextMissingSpriteId++;
            }
        }
    }
    for (FrameSnapshot& frame : pendingReplayArchive_->frames) {
        for (SpriteSnapshot& snapshot : frame.sprites) {
            const uint64_t oldId = snapshot.replayId;
            snapshot.replayId = spriteIdMap.at(oldId);
            if (snapshot.state.parentReplayId != 0) {
                const auto parent = spriteIdMap.find(snapshot.state.parentReplayId);
                snapshot.state.parentReplayId = parent != spriteIdMap.end() ? parent->second : 0;
            }
            const auto texture = spriteTextureHandles.find(snapshot.replayId);
            if (texture != spriteTextureHandles.end()) snapshot.state.textureHandle = texture->second;
        }
        frame.estimatedBytes = EstimateFrameBytes(frame);
    }
}

std::size_t ReplayDebugger::GetMaxFrameCount() const {
    return static_cast<std::size_t>(std::ceil(captureRate_ * historySeconds_)) + 1;
}

const char* ReplayDebugger::GetModeLabel() const {
    switch (mode_) {
    case Mode::Recording: return "記録中";
    case Mode::Paused: return "停止中";
    case Mode::Playback: return "履歴再生中";
    case Mode::Regression: return "自動回帰テスト中";
    case Mode::Idle:
    default: return "待機中";
    }
}

#endif
