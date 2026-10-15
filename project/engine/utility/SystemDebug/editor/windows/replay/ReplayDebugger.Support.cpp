#include "engine/utility/SystemDebug/editor/windows/replay/ReplayDebugger.Internal.h"

#ifdef USE_IMGUI
using namespace ReplayDebuggerImplementation;

// Replay Archive と実行時 Snapshot の相互変換をここへまとめます。
namespace ReplayDebuggerImplementation {
// Replay Archiveの読込対象となるSchemaの範囲か判定します。
bool IsSupportedReplayArchiveSchema(int version) {
    return version >= kMinimumReplayArchiveSchemaVersion &&
        version <= kReplayArchiveSchemaVersion;
}

json Vector2ToJson(const engine::Vector2& value) {
    return json::array({ value.x, value.y });
}

json Vector3ToJson(const engine::Vector3& value) {
    return json::array({ value.x, value.y, value.z });
}

json Vector4ToJson(const engine::Vector4& value) {
    return json::array({ value.x, value.y, value.z, value.w });
}

json QuaternionToJson(const engine::Quaternion& value) {
    return json::array({ value.x, value.y, value.z, value.w });
}

engine::Vector2 JsonToVector2(const json& value, const engine::Vector2& fallback) {
    if (!value.is_array() || value.size() < 2) return fallback;
    return { value[0].get<float>(), value[1].get<float>() };
}

engine::Vector3 JsonToVector3(const json& value, const engine::Vector3& fallback) {
    if (!value.is_array() || value.size() < 3) return fallback;
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
}

engine::Vector4 JsonToVector4(const json& value, const engine::Vector4& fallback) {
    if (!value.is_array() || value.size() < 4) return fallback;
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() };
}

engine::Quaternion JsonToQuaternion(const json& value, const engine::Quaternion& fallback) {
    if (!value.is_array() || value.size() < 4) return fallback;
    return { value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() };
}

// Replay再開時に同じScene Assetを開けるよう読込条件を保存します。
json SceneLoadContextToJson(const SceneLoadContext& context) {
    // リプレイで同じシーンを開けるよう、読み込み条件をJSONへ保存する。
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

// Scene読込条件を復元し、旧ArchiveにないController名はDEFAULTで補います。
SceneLoadContext JsonToSceneLoadContext(const json& value) {
    SceneLoadContext context;
    if (!value.is_object()) return context;
    // 旧 Archive にキーがない場合も、Scene の既定 Controller だけは DEFAULT を保持します。
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

json EntityParameterToJson(const engine::Object3d::EntityParameter& value) {
    // 配置由来の戦闘・移動・ギミック設定を Replay に含め、シーク後の動作を再現します。
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

engine::Object3d::EntityParameter JsonToEntityParameter(const json& value) {
    engine::Object3d::EntityParameter result;
    if (!value.is_object()) return result;
    // 未保存キーは現行の既定値に戻し、旧版 Archive を読み込めるようにします。
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
    // 現在・前状態と遷移の経過時間を一緒に残し、復元後の補間位置を保ちます。
    return {
        { "currentState", value.currentState }, { "previousState", value.previousState },
        { "currentTime", value.currentTime }, { "previousTime", value.previousTime },
        { "transitionElapsed", value.transitionElapsed }, { "transitionDuration", value.transitionDuration },
        { "transitionEasing", value.transitionEasing }, { "valid", value.valid },
    };
}

AnimatorControllerRuntime::Snapshot JsonToAnimatorSnapshot(const json& value) {
    // 記録済みの状態・パラメーターを再生用スナップショットへ戻す。
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
    // Component の不在は null として保存し、設定だけでなく発生タイマーも残します。
    if (!value) return nullptr;
    return {
        { "cpuParticle", value->GetCpuParticle() },
        { "gpuParticle", value->GetGpuParticle() },
        { "emissionTimer", value->GetEmissionTimer() },
    };
}

std::optional<ParticleEmitterComponent> JsonToParticleEmitter(const json& value) {
    // 保存時の null は Component 不在のまま戻し、発生タイマーも再開位置として復元する。
    if (!value.is_object()) return std::nullopt;
    ParticleEmitterComponent result;
    result.SetCpuParticle(value.value("cpuParticle", std::string{}));
    result.SetGpuParticle(value.value("gpuParticle", std::string{}));
    result.GetEmissionTimer() = value.value("emissionTimer", 0.0f);
    return result;
}

json MeshEffectToJson(const std::optional<MeshEffectComponent>& value) {
    // 未装着の Component は null とし、二系統の演出参照だけを保存する。
    if (!value) return nullptr;
    return {
        { "primaryEffect", value->GetPrimaryEffect() },
        { "secondaryEffect", value->GetSecondaryEffect() },
    };
}

std::optional<MeshEffectComponent> JsonToMeshEffect(const json& value) {
    // 保存済みの演出参照がない場合は Component を新規生成しない。
    if (!value.is_object()) return std::nullopt;
    MeshEffectComponent result;
    result.SetPrimaryEffect(value.value("primaryEffect", std::string{}));
    result.SetSecondaryEffect(value.value("secondaryEffect", std::string{}));
    return result;
}

json PathMoverToJson(const std::optional<PathMoverComponent>& value) {
    // 経路名と座標解釈を一緒に保存し、再生時の動き方を維持する。
    if (!value) return nullptr;
    return {
        { "pathName", value->GetPathName() }, { "loop", value->IsLoop() },
        { "relative", value->IsRelative() },
    };
}

std::optional<PathMoverComponent> JsonToPathMover(const json& value) {
    // 旧記録で省略されたループと相対指定は無効として復元する。
    if (!value.is_object()) return std::nullopt;
    PathMoverComponent result;
    result.SetPathName(value.value("pathName", std::string{}));
    result.SetLoop(value.value("loop", false));
    result.SetRelative(value.value("relative", false));
    return result;
}

json GameplayLinkToJson(const std::optional<GameplayLinkComponent>& value) {
    // イベント接続の両端 ID を保存し、未装着は null のまま残す。
    if (!value) return nullptr;
    return { { "eventId", value->GetEventId() }, { "targetId", value->GetTargetId() } };
}

std::optional<GameplayLinkComponent> JsonToGameplayLink(const json& value) {
    // 接続先が省略された旧記録では、無効 ID に戻して誤接続を避ける。
    if (!value.is_object()) return std::nullopt;
    GameplayLinkComponent result;
    result.SetEventId(value.value("eventId", -1));
    result.SetTargetId(value.value("targetId", -1));
    return result;
}

json NavAgentToJson(const std::optional<NavAgentComponent>& value) {
    // コンポーネントなしはnullにし、存在する場合だけ追跡・回避設定を保存する。
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
    // 記録がない項目はコンポーネントの既定値を維持して復元する。
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

json ObjectReplayStateToJson(const engine::Object3d::ReplayState& value) {
    // Transform、可視状態、Animator、衝突、描画材質を同じフレームの Snapshot として保存します。
    // Component の optional 値と派生型の custom 状態も、Object 本体と同時に復元できる形へ変換します。
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

engine::Object3d::ReplayState JsonToObjectReplayState(const json& value) {
    engine::Object3d::ReplayState result;
    // 不足キーは ReplayState の初期値を維持し、古い Snapshot の読み込みを許容します。
    // JSON の配列要素は専用変換を通し、短い配列では初期値に戻します。
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
    // Animator Snapshot は空 Object へフォールバックし、EntityParameter は保存フラグがあるときだけ復元します。
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
    // null の Component は生成せず、保存されていた種類だけ optional を再構成します。
    result.particleEmitterComponent = JsonToParticleEmitter(value.value("particleEmitter", json(nullptr)));
    result.meshEffectComponent = JsonToMeshEffect(value.value("meshEffect", json(nullptr)));
    result.pathMoverComponent = JsonToPathMover(value.value("pathMover", json(nullptr)));
    result.gameplayLinkComponent = JsonToGameplayLink(value.value("gameplayLink", json(nullptr)));
    result.navAgentComponent = JsonToNavAgent(value.value("navAgent", json(nullptr)));
    result.replayRemoved = value.value("replayRemoved", false);
    result.custom = value.value("custom", json::object());
    return result;
}

json SpriteReplayStateToJson(const engine::Sprite::ReplayState& value) {
    // Sprite は UV とアニメーション時刻、親 Replay ID まで残して描画状態を再現します。
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

engine::Sprite::ReplayState JsonToSpriteReplayState(const json& value) {
    engine::Sprite::ReplayState result;
    // 新旧 Archive 間で項目が増減しても、各フィールドは既定値へフォールバックします。
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
    // 本文の読み込み前に Magic・ヘッダー・Schema の順で検証します。
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

#endif
