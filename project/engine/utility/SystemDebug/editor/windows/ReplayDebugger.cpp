#include "ReplayDebugger.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "BulletManager.h"
#include "CaptureToolWindow.h"
#include "Camera.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebugConsole.h"
#include "DebrisEffectManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "ParticleSystem.h"
#include "ProfilerManager.h"
#include "SceneManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr float kMinimumCaptureRate = 5.0f;
constexpr float kMaximumCaptureRate = 60.0f;
constexpr float kMinimumHistorySeconds = 3.0f;
constexpr float kMaximumHistorySeconds = 60.0f;
constexpr int kReplayArchiveSchemaVersion = 3;
constexpr int kMinimumReplayArchiveSchemaVersion = 1;
constexpr const char* kReplayArchiveMagic = "CG4_REPLAY_ARCHIVE";

bool IsSupportedReplayArchiveSchema(int version) {
    return version >= kMinimumReplayArchiveSchemaVersion &&
        version <= kReplayArchiveSchemaVersion;
}

json Vector2ToJson(const Vector2& value) {
    return json::array({ value.x, value.y });
}

json Vector3ToJson(const Vector3& value) {
    return json::array({ value.x, value.y, value.z });
}

json Vector4ToJson(const Vector4& value) {
    return json::array({ value.x, value.y, value.z, value.w });
}

json QuaternionToJson(const Quaternion& value) {
    return json::array({ value.x, value.y, value.z, value.w });
}

Vector2 JsonToVector2(const json& value, const Vector2& fallback = {}) {
    if (!value.is_array() || value.size() < 2) return fallback;
    return { value[0].get<float>(), value[1].get<float>() };
}

Vector3 JsonToVector3(const json& value, const Vector3& fallback = {}) {
    if (!value.is_array() || value.size() < 3) return fallback;
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
}

Vector4 JsonToVector4(const json& value, const Vector4& fallback = {}) {
    if (!value.is_array() || value.size() < 4) return fallback;
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() };
}

Quaternion JsonToQuaternion(const json& value, const Quaternion& fallback = { 0.0f, 0.0f, 0.0f, 1.0f }) {
    if (!value.is_array() || value.size() < 4) return fallback;
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() };
}

json SceneLoadContextToJson(const SceneLoadContext& context) {
    return {
        { "sceneAssetId", context.sceneAssetId },
        { "displayName", context.displayName },
        { "runtimeScene", context.runtimeScene },
        { "objectLayoutPath", context.objectLayoutPath },
        { "spriteLayoutPath", context.spriteLayoutPath },
        { "controllerName", context.controllerName },
        { "bgmPath", context.bgmPath },
        { "lightPath", context.lightPath },
        { "cameraPath", context.cameraPath },
        { "skyboxPath", context.skyboxPath },
    };
}

SceneLoadContext JsonToSceneLoadContext(const json& value) {
    SceneLoadContext context;
    if (!value.is_object()) return context;
    context.sceneAssetId = value.value("sceneAssetId", std::string{});
    context.displayName = value.value("displayName", std::string{});
    context.runtimeScene = value.value("runtimeScene", std::string{});
    context.objectLayoutPath = value.value("objectLayoutPath", std::string{});
    context.spriteLayoutPath = value.value("spriteLayoutPath", std::string{});
    context.controllerName = value.value("controllerName", std::string{ "DEFAULT" });
    context.bgmPath = value.value("bgmPath", std::string{});
    context.lightPath = value.value("lightPath", std::string{});
    context.cameraPath = value.value("cameraPath", std::string{});
    context.skyboxPath = value.value("skyboxPath", std::string{});
    return context;
}

json EntityParameterToJson(const Object3d::EntityParameter& value) {
    return {
        { "hp", value.hp }, { "maxHp", value.maxHp }, { "attackPower", value.attackPower },
        { "speed", value.speed }, { "gravity", value.gravity }, { "maxFallSpeed", value.maxFallSpeed },
        { "jumpPower", value.jumpPower }, { "morphLimited", value.morphLimited },
        { "morphDuration", value.morphDuration }, { "enemyType", value.enemyType },
        { "gimmickType", value.gimmickType }, { "itemType", value.itemType },
        { "healAmount", value.healAmount }, { "interval", value.interval }, { "maxCount", value.maxCount },
        { "detectionRange", value.detectionRange }, { "shakeDuration", value.shakeDuration },
        { "fallDuration", value.fallDuration }, { "colorType", value.colorType },
        { "switchMode", value.switchMode }, { "actionMode", value.actionMode },
        { "targetScene", value.targetScene }, { "moveAmount", value.moveAmount },
        { "moveSpeed", value.moveSpeed }, { "startActive", value.startActive },
        { "returnOnOff", value.returnOnOff },
    };
}

Object3d::EntityParameter JsonToEntityParameter(const json& value) {
    Object3d::EntityParameter result;
    if (!value.is_object()) return result;
    result.hp = value.value("hp", result.hp);
    result.maxHp = value.value("maxHp", result.maxHp);
    result.attackPower = value.value("attackPower", result.attackPower);
    result.speed = value.value("speed", result.speed);
    result.gravity = value.value("gravity", result.gravity);
    result.maxFallSpeed = value.value("maxFallSpeed", result.maxFallSpeed);
    result.jumpPower = value.value("jumpPower", result.jumpPower);
    result.morphLimited = value.value("morphLimited", result.morphLimited);
    result.morphDuration = value.value("morphDuration", result.morphDuration);
    result.enemyType = value.value("enemyType", result.enemyType);
    result.gimmickType = value.value("gimmickType", result.gimmickType);
    result.itemType = value.value("itemType", result.itemType);
    result.healAmount = value.value("healAmount", result.healAmount);
    result.interval = value.value("interval", result.interval);
    result.maxCount = value.value("maxCount", result.maxCount);
    result.detectionRange = value.value("detectionRange", result.detectionRange);
    result.shakeDuration = value.value("shakeDuration", result.shakeDuration);
    result.fallDuration = value.value("fallDuration", result.fallDuration);
    result.colorType = value.value("colorType", result.colorType);
    result.switchMode = value.value("switchMode", result.switchMode);
    result.actionMode = value.value("actionMode", result.actionMode);
    result.targetScene = value.value("targetScene", result.targetScene);
    result.moveAmount = value.value("moveAmount", result.moveAmount);
    result.moveSpeed = value.value("moveSpeed", result.moveSpeed);
    result.startActive = value.value("startActive", result.startActive);
    result.returnOnOff = value.value("returnOnOff", result.returnOnOff);
    return result;
}

json AnimatorSnapshotToJson(const AnimatorControllerRuntime::Snapshot& value) {
    return {
        { "currentState", value.currentState }, { "previousState", value.previousState },
        { "currentTime", value.currentTime }, { "previousTime", value.previousTime },
        { "transitionElapsed", value.transitionElapsed }, { "transitionDuration", value.transitionDuration },
        { "transitionEasing", value.transitionEasing }, { "valid", value.valid },
    };
}

AnimatorControllerRuntime::Snapshot JsonToAnimatorSnapshot(const json& value) {
    AnimatorControllerRuntime::Snapshot result;
    if (!value.is_object()) return result;
    result.currentState = value.value("currentState", std::string{});
    result.previousState = value.value("previousState", std::string{});
    result.currentTime = value.value("currentTime", 0.0f);
    result.previousTime = value.value("previousTime", 0.0f);
    result.transitionElapsed = value.value("transitionElapsed", 0.0f);
    result.transitionDuration = value.value("transitionDuration", 0.0f);
    result.transitionEasing = value.value("transitionEasing", 4);
    result.valid = value.value("valid", false);
    return result;
}

json ParticleEmitterToJson(const std::optional<ParticleEmitterComponent>& value) {
    if (!value) return nullptr;
    return {
        { "cpuParticle", value->GetCpuParticle() },
        { "gpuParticle", value->GetGpuParticle() },
        { "emissionTimer", value->GetEmissionTimer() },
    };
}

std::optional<ParticleEmitterComponent> JsonToParticleEmitter(const json& value) {
    if (!value.is_object()) return std::nullopt;
    ParticleEmitterComponent result;
    result.SetCpuParticle(value.value("cpuParticle", std::string{}));
    result.SetGpuParticle(value.value("gpuParticle", std::string{}));
    result.GetEmissionTimer() = value.value("emissionTimer", 0.0f);
    return result;
}

json MeshEffectToJson(const std::optional<MeshEffectComponent>& value) {
    if (!value) return nullptr;
    return {
        { "primaryEffect", value->GetPrimaryEffect() },
        { "secondaryEffect", value->GetSecondaryEffect() },
    };
}

std::optional<MeshEffectComponent> JsonToMeshEffect(const json& value) {
    if (!value.is_object()) return std::nullopt;
    MeshEffectComponent result;
    result.SetPrimaryEffect(value.value("primaryEffect", std::string{}));
    result.SetSecondaryEffect(value.value("secondaryEffect", std::string{}));
    return result;
}

json PathMoverToJson(const std::optional<PathMoverComponent>& value) {
    if (!value) return nullptr;
    return {
        { "pathName", value->GetPathName() }, { "loop", value->IsLoop() },
        { "relative", value->IsRelative() },
    };
}

std::optional<PathMoverComponent> JsonToPathMover(const json& value) {
    if (!value.is_object()) return std::nullopt;
    PathMoverComponent result;
    result.SetPathName(value.value("pathName", std::string{}));
    result.SetLoop(value.value("loop", false));
    result.SetRelative(value.value("relative", false));
    return result;
}

json GameplayLinkToJson(const std::optional<GameplayLinkComponent>& value) {
    if (!value) return nullptr;
    return { { "eventId", value->GetEventId() }, { "targetId", value->GetTargetId() } };
}

std::optional<GameplayLinkComponent> JsonToGameplayLink(const json& value) {
    if (!value.is_object()) return std::nullopt;
    GameplayLinkComponent result;
    result.SetEventId(value.value("eventId", -1));
    result.SetTargetId(value.value("targetId", -1));
    return result;
}

json NavAgentToJson(const std::optional<NavAgentComponent>& value) {
    if (!value) return nullptr;
    return {
        { "enabled", value->IsEnabled() }, { "cellSize", value->GetCellSize() },
        { "agentRadius", value->GetAgentRadius() }, { "agentHeight", value->GetAgentHeight() },
        { "searchPadding", value->GetSearchPadding() }, { "repathInterval", value->GetRepathInterval() },
        { "stoppingDistance", value->GetStoppingDistance() }, { "obstacleMask", value->GetObstacleMask() },
        { "allowDiagonal", value->AllowsDiagonal() },
    };
}

std::optional<NavAgentComponent> JsonToNavAgent(const json& value) {
    if (!value.is_object()) return std::nullopt;
    NavAgentComponent result;
    result.SetEnabled(value.value("enabled", true));
    result.SetCellSize(value.value("cellSize", result.GetCellSize()));
    result.SetAgentRadius(value.value("agentRadius", result.GetAgentRadius()));
    result.SetAgentHeight(value.value("agentHeight", result.GetAgentHeight()));
    result.SetSearchPadding(value.value("searchPadding", result.GetSearchPadding()));
    result.SetRepathInterval(value.value("repathInterval", result.GetRepathInterval()));
    result.SetStoppingDistance(value.value("stoppingDistance", result.GetStoppingDistance()));
    result.SetObstacleMask(value.value("obstacleMask", result.GetObstacleMask()));
    result.SetAllowDiagonal(value.value("allowDiagonal", true));
    return result;
}

json ObjectReplayStateToJson(const Object3d::ReplayState& value) {
    return {
        { "scale", Vector3ToJson(value.scale) }, { "rotation", Vector3ToJson(value.rotation) },
        { "translation", Vector3ToJson(value.translation) }, { "quaternion", QuaternionToJson(value.quaternion) },
        { "quaternionMaster", value.quaternionMaster }, { "visible", value.visible }, { "dead", value.dead },
        { "collecting", value.collecting }, { "collectTimer", value.collectTimer },
        { "animationTime", value.animationTime }, { "animationName", value.animationName },
        { "animationLoop", value.animationLoop }, { "animatorControllerPath", value.animatorControllerPath },
        { "animatorSnapshot", AnimatorSnapshotToJson(value.animatorSnapshot) },
        { "hasParameter", value.hasParameter },
        { "parameter", value.hasParameter ? EntityParameterToJson(value.parameter) : json(nullptr) },
        { "collisionAttribute", value.collisionAttribute }, { "collisionMask", value.collisionMask },
        { "modelName", value.modelName }, { "texturePath", value.texturePath },
        { "materialType", value.materialType }, { "color", Vector4ToJson(value.color) },
        { "emissive", value.emissive }, { "particleEmitter", ParticleEmitterToJson(value.particleEmitterComponent) },
        { "meshEffect", MeshEffectToJson(value.meshEffectComponent) },
        { "pathMover", PathMoverToJson(value.pathMoverComponent) },
        { "gameplayLink", GameplayLinkToJson(value.gameplayLinkComponent) },
        { "navAgent", NavAgentToJson(value.navAgentComponent) },
        { "replayRemoved", value.replayRemoved }, { "custom", value.custom },
    };
}

Object3d::ReplayState JsonToObjectReplayState(const json& value) {
    Object3d::ReplayState result;
    result.scale = JsonToVector3(value.value("scale", json::array()), result.scale);
    result.rotation = JsonToVector3(value.value("rotation", json::array()), result.rotation);
    result.translation = JsonToVector3(value.value("translation", json::array()), result.translation);
    result.quaternion = JsonToQuaternion(value.value("quaternion", json::array()), result.quaternion);
    result.quaternionMaster = value.value("quaternionMaster", result.quaternionMaster);
    result.visible = value.value("visible", result.visible);
    result.dead = value.value("dead", result.dead);
    result.collecting = value.value("collecting", result.collecting);
    result.collectTimer = value.value("collectTimer", result.collectTimer);
    result.animationTime = value.value("animationTime", result.animationTime);
    result.animationName = value.value("animationName", std::string{});
    result.animationLoop = value.value("animationLoop", result.animationLoop);
    result.animatorControllerPath = value.value("animatorControllerPath", std::string{});
    result.animatorSnapshot = JsonToAnimatorSnapshot(value.value("animatorSnapshot", json::object()));
    result.hasParameter = value.value("hasParameter", false);
    if (result.hasParameter) result.parameter = JsonToEntityParameter(value.value("parameter", json::object()));
    result.collisionAttribute = value.value("collisionAttribute", uint32_t{ 0 });
    result.collisionMask = value.value("collisionMask", uint32_t{ 0 });
    result.modelName = value.value("modelName", std::string{});
    result.texturePath = value.value("texturePath", std::string{});
    result.materialType = value.value("materialType", int32_t{ 0 });
    result.color = JsonToVector4(value.value("color", json::array()), result.color);
    result.emissive = value.value("emissive", result.emissive);
    result.particleEmitterComponent = JsonToParticleEmitter(value.value("particleEmitter", json(nullptr)));
    result.meshEffectComponent = JsonToMeshEffect(value.value("meshEffect", json(nullptr)));
    result.pathMoverComponent = JsonToPathMover(value.value("pathMover", json(nullptr)));
    result.gameplayLinkComponent = JsonToGameplayLink(value.value("gameplayLink", json(nullptr)));
    result.navAgentComponent = JsonToNavAgent(value.value("navAgent", json(nullptr)));
    result.replayRemoved = value.value("replayRemoved", false);
    result.custom = value.value("custom", json::object());
    return result;
}

json SpriteReplayStateToJson(const Sprite::ReplayState& value) {
    return {
        { "position", Vector2ToJson(value.position) }, { "rotation", value.rotation },
        { "size", Vector2ToJson(value.size) }, { "color", Vector4ToJson(value.color) },
        { "anchorPoint", Vector2ToJson(value.anchorPoint) }, { "flipX", value.flipX }, { "flipY", value.flipY },
        { "textureLeftTop", Vector2ToJson(value.textureLeftTop) },
        { "textureSize", Vector2ToJson(value.textureSize) }, { "textureHandle", value.textureHandle },
        { "emissive", value.emissive }, { "visible", value.visible }, { "locked", value.locked },
        { "animationPlaying", value.animationPlaying }, { "animationLooping", value.animationLooping },
        { "frameDuration", value.frameDuration }, { "animationTimer", value.animationTimer },
        { "totalFrames", value.totalFrames }, { "currentFrame", value.currentFrame },
        { "frameWidth", value.frameWidth }, { "frameHeight", value.frameHeight },
        { "parentReplayId", value.parentReplayId }, { "replayRemoved", value.replayRemoved },
    };
}

Sprite::ReplayState JsonToSpriteReplayState(const json& value) {
    Sprite::ReplayState result;
    result.position = JsonToVector2(value.value("position", json::array()), result.position);
    result.rotation = value.value("rotation", result.rotation);
    result.size = JsonToVector2(value.value("size", json::array()), result.size);
    result.color = JsonToVector4(value.value("color", json::array()), result.color);
    result.anchorPoint = JsonToVector2(value.value("anchorPoint", json::array()), result.anchorPoint);
    result.flipX = value.value("flipX", result.flipX);
    result.flipY = value.value("flipY", result.flipY);
    result.textureLeftTop = JsonToVector2(value.value("textureLeftTop", json::array()), result.textureLeftTop);
    result.textureSize = JsonToVector2(value.value("textureSize", json::array()), result.textureSize);
    result.textureHandle = value.value("textureHandle", uint32_t{ 0 });
    result.emissive = value.value("emissive", result.emissive);
    result.visible = value.value("visible", result.visible);
    result.locked = value.value("locked", result.locked);
    result.animationPlaying = value.value("animationPlaying", result.animationPlaying);
    result.animationLooping = value.value("animationLooping", result.animationLooping);
    result.frameDuration = value.value("frameDuration", result.frameDuration);
    result.animationTimer = value.value("animationTimer", result.animationTimer);
    result.totalFrames = value.value("totalFrames", result.totalFrames);
    result.currentFrame = value.value("currentFrame", result.currentFrame);
    result.frameWidth = value.value("frameWidth", result.frameWidth);
    result.frameHeight = value.value("frameHeight", result.frameHeight);
    result.parentReplayId = value.value("parentReplayId", uint64_t{ 0 });
    result.replayRemoved = value.value("replayRemoved", false);
    return result;
}

std::filesystem::path GetReplayArchiveDirectory() {
    return std::filesystem::path("Saved") / "Replays";
}

std::string MakeLocalTimestamp(const char* format) {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime{};
    localtime_s(&localTime, &now);
    std::ostringstream stream;
    stream << std::put_time(&localTime, format);
    return stream.str();
}

bool ReadArchiveHeader(const std::filesystem::path& filePath, json& header, std::string& error) {
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream) {
        error = "ファイルを開けません。";
        return false;
    }
    std::string magic;
    std::string headerLine;
    if (!std::getline(stream, magic) || magic != kReplayArchiveMagic || !std::getline(stream, headerLine)) {
        error = "リプレイ形式ではありません。";
        return false;
    }
    try {
        header = json::parse(headerLine);
    } catch (const std::exception& exception) {
        error = std::string("ヘッダー解析失敗: ") + exception.what();
        return false;
    }
    if (!IsSupportedReplayArchiveSchema(header.value("schemaVersion", 0))) {
        error = "未対応のリプレイバージョンです。";
        return false;
    }
    return true;
}
}

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
        statusMessage_ = "保存リプレイを読み込みました。任意フレームで停止し、分岐再開できます: " +
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
