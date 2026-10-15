#pragma once
// 分割実装だけが使う補助関数の宣言です。状態を重複して持たせません。
#include "engine/utility/SystemDebug/editor/windows/replay/ReplayDebugger.h"

#ifdef USE_IMGUI

#include "BaseScene.h"
#include "BulletManager.h"
#include "CaptureToolWindow.h"
#include "Camera.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
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

namespace ReplayDebuggerImplementation {
inline constexpr float kMinimumCaptureRate = 5.0f;
inline constexpr float kMaximumCaptureRate = 60.0f;
inline constexpr float kMinimumHistorySeconds = 3.0f;
inline constexpr float kMaximumHistorySeconds = 60.0f;
inline constexpr int kReplayArchiveSchemaVersion = 3;
inline constexpr int kMinimumReplayArchiveSchemaVersion = 1;
inline constexpr const char* kReplayArchiveMagic = "CG4_REPLAY_ARCHIVE";

// 読み込みは旧スキーマも許すが、書き込み時は常に現在の版を使います。
bool IsSupportedReplayArchiveSchema(int version);

json Vector2ToJson(const engine::Vector2& value);

json Vector3ToJson(const engine::Vector3& value);

json Vector4ToJson(const engine::Vector4& value);

json QuaternionToJson(const engine::Quaternion& value);

engine::Vector2 JsonToVector2(const json& value, const engine::Vector2& fallback = {});

engine::Vector3 JsonToVector3(const json& value, const engine::Vector3& fallback = {});

engine::Vector4 JsonToVector4(const json& value, const engine::Vector4& fallback = {});

engine::Quaternion JsonToQuaternion(const json& value, const engine::Quaternion& fallback = { 0.0f, 0.0f, 0.0f, 1.0f });

json SceneLoadContextToJson(const SceneLoadContext& context);

SceneLoadContext JsonToSceneLoadContext(const json& value);

json EntityParameterToJson(const engine::Object3d::EntityParameter& value);

engine::Object3d::EntityParameter JsonToEntityParameter(const json& value);

json AnimatorSnapshotToJson(const AnimatorControllerRuntime::Snapshot& value);

AnimatorControllerRuntime::Snapshot JsonToAnimatorSnapshot(const json& value);

json ParticleEmitterToJson(const std::optional<ParticleEmitterComponent>& value);

std::optional<ParticleEmitterComponent> JsonToParticleEmitter(const json& value);

json MeshEffectToJson(const std::optional<MeshEffectComponent>& value);

std::optional<MeshEffectComponent> JsonToMeshEffect(const json& value);

json PathMoverToJson(const std::optional<PathMoverComponent>& value);

std::optional<PathMoverComponent> JsonToPathMover(const json& value);

json GameplayLinkToJson(const std::optional<GameplayLinkComponent>& value);

std::optional<GameplayLinkComponent> JsonToGameplayLink(const json& value);

json NavAgentToJson(const std::optional<NavAgentComponent>& value);

std::optional<NavAgentComponent> JsonToNavAgent(const json& value);

json ObjectReplayStateToJson(const engine::Object3d::ReplayState& value);

engine::Object3d::ReplayState JsonToObjectReplayState(const json& value);

json SpriteReplayStateToJson(const engine::Sprite::ReplayState& value);

engine::Sprite::ReplayState JsonToSpriteReplayState(const json& value);

std::filesystem::path GetReplayArchiveDirectory();

std::string MakeLocalTimestamp(const char* format);

// アーカイブ全体を復元する前に識別子・版を確認します。失敗理由は error に返します。
bool ReadArchiveHeader(const std::filesystem::path& filePath, json& header, std::string& error);
}


#endif
