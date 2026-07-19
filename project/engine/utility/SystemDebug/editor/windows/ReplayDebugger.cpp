#include "ReplayDebugger.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "BulletManager.h"
#include "Camera.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebrisEffectManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "ParticleSystem.h"
#include "SceneManager.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr float kMinimumCaptureRate = 5.0f;
constexpr float kMaximumCaptureRate = 60.0f;
constexpr float kMinimumHistorySeconds = 3.0f;
constexpr float kMaximumHistorySeconds = 60.0f;
}

void ReplayDebugger::Initialize(SceneManager* sceneManager, DebugEditor* debugEditor) {
    sceneManager_ = sceneManager;
    debugEditor_ = debugEditor;
}

void ReplayDebugger::Finalize() {
    ResetForSceneChange();
    sceneManager_ = nullptr;
    debugEditor_ = nullptr;
}

bool ReplayDebugger::ShouldFreezeSimulation() const {
    return (mode_ == Mode::Paused || mode_ == Mode::Playback) &&
        GetValidatedActiveScene() != nullptr;
}

bool ReplayDebugger::HasFrames() const {
    return !frames_.empty() && GetValidatedActiveScene() != nullptr;
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
        ResumeFromCursor();
    }
}

void ReplayDebugger::UpdateBeforeSimulation(float realDeltaTime, bool isPlaying) {
    if (!sceneManager_) {
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
    if (!isPlaying || mode_ != Mode::Recording || !activeScene_ ||
        !sceneManager_ || sceneManager_->IsTransitioning()) {
        return;
    }

    if (!GetValidatedActiveScene()) {
        ResetForSceneChange();
        return;
    }

    const float safeDeltaTime = (std::max)(0.0f, simulationDeltaTime);
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
        snapshot.name = object->GetName();
        snapshot.className = object->GetClassName();
        snapshot.state = object->CaptureReplayState();
        frame.objects.push_back(std::move(snapshot));
    }

    std::vector<Sprite*> replaySprites;
    scene->CollectReplaySprites(replaySprites);
    frame.sprites.reserve(replaySprites.size());
    std::unordered_set<Sprite*> capturedSprites;
    capturedSprites.reserve(replaySprites.size());
    for (Sprite* sprite : replaySprites) {
        if (!sprite || !capturedSprites.insert(sprite).second) {
            continue;
        }
        sprite->SetReplayRetained(true);
        SpriteSnapshot snapshot;
        snapshot.replayId = sprite->EnsureReplayId();
        snapshot.name = sprite->GetName().empty() ? "Sprite" : sprite->GetName();
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
    frame.estimatedBytes = sizeof(FrameSnapshot) +
        frame.objects.capacity() * sizeof(ObjectSnapshot) +
        frame.sprites.capacity() * sizeof(SpriteSnapshot);
    for (const ObjectSnapshot& snapshot : frame.objects) {
        frame.estimatedBytes += snapshot.name.capacity();
        frame.estimatedBytes += snapshot.className.capacity();
        frame.estimatedBytes += snapshot.state.animationName.capacity();
        frame.estimatedBytes += snapshot.state.animatorControllerPath.capacity();
        frame.estimatedBytes += snapshot.state.animatorSnapshot.currentState.capacity();
        frame.estimatedBytes += snapshot.state.animatorSnapshot.previousState.capacity();
        frame.estimatedBytes += snapshot.state.modelName.capacity();
        frame.estimatedBytes += snapshot.state.texturePath.capacity();
        frame.estimatedBytes += snapshot.state.custom.size() * 64;
    }
    for (const SpriteSnapshot& snapshot : frame.sprites) {
        frame.estimatedBytes += snapshot.name.capacity();
    }
    frame.estimatedBytes += frame.sceneState.dump().size();
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

void ReplayDebugger::ResumeFromCursor() {
    if (!HasFrames()) {
        if (!frames_.empty()) {
            ResetForSceneChange();
        }
        return;
    }

    ApplyFrame(cursor_);
    while (frames_.size() > cursor_ + 1) {
        estimatedMemoryBytes_ -= (std::min)(estimatedMemoryBytes_, frames_.back().estimatedBytes);
        frames_.pop_back();
    }
    timelineTime_ = frames_.back().time;
    captureAccumulator_ = 0.0f;
    playbackTime_ = static_cast<float>(timelineTime_);
    ClearTransientRuntime();
    mode_ = Mode::Recording;
    statusMessage_ = "選択フレームから分岐し、未来側の履歴を破棄して記録を再開しました。";
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
    if (!continueRecording) {
        ReleaseRetainedObjects();
    }
    frames_.clear();
    cursor_ = 0;
    timelineTime_ = 0.0;
    captureAccumulator_ = 0.0f;
    playbackTime_ = 0.0f;
    lastMissingObjectCount_ = 0;
    lastMissingSpriteCount_ = 0;
    estimatedMemoryBytes_ = 0;
    selectedReplayId_ = 0;
    selectedSpriteReplayId_ = 0;
    statusMessage_ = "リプレイ履歴をクリアしました。";
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
    ReleaseRetainedObjects();
    frames_.clear();
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
    selectedReplayId_ = 0;
    selectedSpriteReplayId_ = 0;
    statusMessage_.clear();
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
}

std::size_t ReplayDebugger::GetMaxFrameCount() const {
    return static_cast<std::size_t>(std::ceil(captureRate_ * historySeconds_)) + 1;
}

const char* ReplayDebugger::GetModeLabel() const {
    switch (mode_) {
    case Mode::Recording: return "記録中";
    case Mode::Paused: return "停止中";
    case Mode::Playback: return "履歴再生中";
    case Mode::Idle:
    default: return "待機中";
    }
}

#endif
