#include "VFXSequencer.h"
#include "AudioPlayer.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "Easing.h"
#include "GameplayEventTrace.h"
#include "InputManager.h"
#include "GPUParticleManager.h"
#include "LightManager.h"
#include "MeshEffectManager.h"
#include "PostEffect.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <memory>
#include <unordered_map>

using json = nlohmann::json;

namespace {
    constexpr const char* kSequenceDirectory = "Resources/json/vfx_sequence/";
    constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
    float gameplayTimeScale = 1.0f;

    const char* GetVFXEventTypeName(VFXEventType type) {
        switch (type) {
        case VFXEventType::GPUParticle: return "GPU Particle";
        case VFXEventType::MeshEffect: return "Mesh Effect";
        case VFXEventType::SoundEffect: return "Audio Event";
        case VFXEventType::MovingParticle: return "Moving Particle";
        case VFXEventType::CameraShake: return "Camera Shake";
        case VFXEventType::PostEffectPulse: return "Post Effect";
        case VFXEventType::LightPulse: return "Light Pulse";
        case VFXEventType::HitStop: return "Hit Stop";
        case VFXEventType::ControllerRumble: return "Controller Rumble";
        case VFXEventType::CameraFovPulse: return "Camera FOV";
        default: return "Unknown";
        }
    }

    std::string DescribeTraceTarget(const Object3d* targetObject) {
        if (!targetObject) {
            return "World";
        }
        std::string source = targetObject->GetName();
        const std::string& guid = targetObject->GetPersistentGuid();
        if (!guid.empty()) {
            source += " [" + guid + "]";
        }
        return source;
    }

    void TraceSequence(
        GameplayEventTracePhase phase,
        const std::string& sequenceName,
        const Object3d* targetObject,
        const std::string& detail = {}) {
        GameplayEventTrace::GetInstance()->Record(
            phase, "Feedback Cue", DescribeTraceTarget(targetObject), sequenceName, detail);
    }

    std::vector<std::unique_ptr<VFXSequencer>>& ActiveOneShotSequences() {
        static std::vector<std::unique_ptr<VFXSequencer>> sequences;
        return sequences;
    }

    struct HitStopLayer {
        float remaining = 0.0f;
        float timeScale = 0.0f;
    };

    std::vector<HitStopLayer>& ActiveHitStops() {
        static std::vector<HitStopLayer> layers;
        return layers;
    }

    std::unordered_map<std::string, std::vector<VFXEvent>>& SequenceEventCache() {
        static std::unordered_map<std::string, std::vector<VFXEvent>> cache;
        return cache;
    }

    std::unordered_map<std::string, std::filesystem::file_time_type>& SequenceWriteTimeCache() {
        static std::unordered_map<std::string, std::filesystem::file_time_type> cache;
        return cache;
    }

    void ResetEventRuntimeState(std::vector<VFXEvent>& events) {
        for (auto& event : events) {
            event.hasFired = false;
            event.isFinished = false;
            event.hasAppliedPostPulse = false;
            event.appliedRadialIntensity = 0.0f;
            event.appliedDamageFlash = 0.0f;
            event.appliedChromaticAberration = 0.0f;
            event.appliedWobbleIntensity = 0.0f;
            event.appliedBloomIntensity = 0.0f;
        }
    }

    Vector3 ReadVector3(const json& value, const Vector3& fallback) {
        if (!value.is_array() || value.size() < 3) return fallback;
        return {
            value[0].get<float>(),
            value[1].get<float>(),
            value[2].get<float>()
        };
    }

    json WriteVector3(const Vector3& value) {
        return json::array({ value.x, value.y, value.z });
    }

    Vector4 ReadVector4(const json& value, const Vector4& fallback) {
        if (!value.is_array() || value.size() < 4) return fallback;
        return {
            value[0].get<float>(),
            value[1].get<float>(),
            value[2].get<float>(),
            value[3].get<float>()
        };
    }

    json WriteVector4(const Vector4& value) {
        return json::array({ value.x, value.y, value.z, value.w });
    }

    float ApplyEventEasing(int easingType, float progress) {
        progress = std::clamp(progress, 0.0f, 1.0f);
        if (easingType == 2) return Easing::OutSine(progress);
        if (easingType == 4) return Easing::InQuad(progress);
        return progress;
    }

    Vector3 CalculateBezier(const Vector3& p0, const Vector3& p1, const Vector3& p2, float t) {
        auto lerp = [](float a, float b, float ratio) {
            return a + (b - a) * ratio;
            };
        Vector3 a = {
            lerp(p0.x, p1.x, t),
            lerp(p0.y, p1.y, t),
            lerp(p0.z, p1.z, t)
        };
        Vector3 b = {
            lerp(p1.x, p2.x, t),
            lerp(p1.y, p2.y, t),
            lerp(p1.z, p2.z, t)
        };
        return {
            lerp(a.x, b.x, t),
            lerp(a.y, b.y, t),
            lerp(a.z, b.z, t)
        };
    }

    Vector3 ResolveTargetPosition(Object3d* targetObject, const Vector3& localOffset, Matrix4x4& emitMatrix) {
        emitMatrix = Math::MakeIdentity4x4();
        if (!targetObject) return localOffset;

        Matrix4x4 worldMat = targetObject->GetWorldMatrix();
        Vector3 spawnPos = Math::TransformNormal(localOffset, worldMat);
        spawnPos.x += worldMat.m[3][0];
        spawnPos.y += worldMat.m[3][1];
        spawnPos.z += worldMat.m[3][2];
        emitMatrix = worldMat;
        return spawnPos;
    }

    Vector3 ResolveSequencerPosition(
        Object3d* targetObject,
        bool useRootPosition,
        const Vector3& rootPosition,
        const Vector3& rootScale,
        const Vector3& rootRotation,
        const Vector3& localOffset,
        Matrix4x4& emitMatrix) {
        if (targetObject) {
            Vector3 targetLocalOffset = {
                localOffset.x * rootScale.x,
                localOffset.y * rootScale.y,
                localOffset.z * rootScale.z
            };
            if (useRootPosition) {
                targetLocalOffset.x += rootPosition.x;
                targetLocalOffset.y += rootPosition.y;
                targetLocalOffset.z += rootPosition.z;
            }
            return ResolveTargetPosition(targetObject, targetLocalOffset, emitMatrix);
        }

        Vector3 scaledOffset = {
            localOffset.x * rootScale.x,
            localOffset.y * rootScale.y,
            localOffset.z * rootScale.z
        };
        const Matrix4x4 rootRotateMatrix = Math::MakeRotateMatrix(rootRotation);
        Vector3 spawnPos = Math::TransformNormal(scaledOffset, rootRotateMatrix);
        if (useRootPosition) {
            spawnPos.x += rootPosition.x;
            spawnPos.y += rootPosition.y;
            spawnPos.z += rootPosition.z;
        }
        emitMatrix = Math::MakeAffineMatrix(rootScale, rootRotation, spawnPos);
        return spawnPos;
    }
    void RemovePostPulseContribution(VFXEvent& event) {
        if (!event.hasAppliedPostPulse) return;

        PostEffect::Params* params = PostEffect::GetInstance()->GetParams();
        if (params) {
            params->radialIntensity -= event.appliedRadialIntensity;
            params->damageFlash -= event.appliedDamageFlash;
            params->chromaticAberration -= event.appliedChromaticAberration;
            params->wobbleIntensity -= event.appliedWobbleIntensity;
            params->bloomIntensity -= event.appliedBloomIntensity;
        }

        event.hasAppliedPostPulse = false;
        event.appliedRadialIntensity = 0.0f;
        event.appliedDamageFlash = 0.0f;
        event.appliedChromaticAberration = 0.0f;
        event.appliedWobbleIntensity = 0.0f;
        event.appliedBloomIntensity = 0.0f;
    }

    void ApplyPostPulse(VFXEvent& event, float currentTime) {
        PostEffect::Params* params = PostEffect::GetInstance()->GetParams();
        if (!params) {
            event.isFinished = true;
            return;
        }

        RemovePostPulseContribution(event);

        const float duration = (std::max)(event.duration, 0.01f);
        const float progress = std::clamp((currentTime - event.triggerTime) / duration, 0.0f, 1.0f);
        if (progress >= 1.0f) {
            event.isFinished = true;
            return;
        }

        const float envelope = 1.0f - ApplyEventEasing(event.easingType, progress);
        event.appliedRadialIntensity = event.radialIntensity * envelope;
        event.appliedDamageFlash = event.damageFlash * envelope;
        event.appliedChromaticAberration = event.chromaticAberration * envelope;
        event.appliedWobbleIntensity = event.wobbleIntensity * envelope;
        event.appliedBloomIntensity = event.bloomIntensity * envelope;

        params->radialIntensity += event.appliedRadialIntensity;
        params->damageFlash += event.appliedDamageFlash;
        params->chromaticAberration += event.appliedChromaticAberration;
        params->wobbleIntensity += event.appliedWobbleIntensity;
        params->bloomIntensity += event.appliedBloomIntensity;
        event.hasAppliedPostPulse = true;
        event.hasFired = true;
    }


    float GetVFXEventEndTime(const VFXEvent& event) {
        if (event.type == VFXEventType::MovingParticle ||
            event.type == VFXEventType::CameraShake ||
            event.type == VFXEventType::PostEffectPulse ||
            event.type == VFXEventType::LightPulse ||
            event.type == VFXEventType::HitStop ||
            event.type == VFXEventType::ControllerRumble ||
            event.type == VFXEventType::CameraFovPulse) {
            return event.triggerTime + (std::max)(event.duration, 0.01f);
        }
        return event.triggerTime + 0.08f;
    }
}

void VFXSequencer::Initialize(Object3d* targetObject) {
    Reset();
    targetObject_ = targetObject;
    useRootPosition_ = false;
    rootPosition_ = { 0.0f, 0.0f, 0.0f };
    rootScale_ = { 1.0f, 1.0f, 1.0f };
    rootRotation_ = { 0.0f, 0.0f, 0.0f };
    sequenceName_.clear();
    events_.clear();
}

void VFXSequencer::SetRootPosition(const Vector3& rootPosition) {
    rootPosition_ = rootPosition;
    useRootPosition_ = true;
}

void VFXSequencer::SetRootScale(const Vector3& rootScale) {
    rootScale_ = {
        (std::max)(0.001f, rootScale.x),
        (std::max)(0.001f, rootScale.y),
        (std::max)(0.001f, rootScale.z)
    };
}

void VFXSequencer::SetRootRotation(const Vector3& rootRotation) {
    rootRotation_ = rootRotation;
}

void VFXSequencer::ClearRootPosition() {
    rootPosition_ = { 0.0f, 0.0f, 0.0f };
    rootScale_ = { 1.0f, 1.0f, 1.0f };
    rootRotation_ = { 0.0f, 0.0f, 0.0f };
    useRootPosition_ = false;
}

void VFXSequencer::AddEvent(
    VFXEventType type,
    const std::string& presetName,
    float triggerTime,
    const Vector3& offset,
    const Vector3& rotation,
    const Vector3& scale) {
    VFXEvent event;
    event.type = type;
    event.presetName = presetName;
    event.triggerTime = triggerTime;
    event.offset = offset;
    event.rotation = rotation;
    event.scale = scale;
    events_.push_back(event);
}

void VFXSequencer::Play() {
    Reset();
    isPlaying_ = true;
    TraceSequence(
        GameplayEventTracePhase::Started,
        sequenceName_.empty() ? "(unsaved sequence)" : sequenceName_,
        targetObject_,
        std::to_string(events_.size()) + " events");
}

void VFXSequencer::Stop() {
    if (isPlaying_) {
        TraceSequence(
            GameplayEventTracePhase::Cancelled,
            sequenceName_.empty() ? "(unsaved sequence)" : sequenceName_,
            targetObject_,
            "Stopped before completion");
    }
    Reset();
}

void VFXSequencer::Reset() {
    currentTime_ = 0.0f;
    isPlaying_ = false;
    for (auto& event : events_) {
        RemovePostPulseContribution(event);
        event.hasFired = false;
        event.isFinished = false;
    }
}

float VFXSequencer::GetDuration() const {
    float duration = 0.0f;
    for (const auto& event : events_) {
        duration = (std::max)(duration, GetVFXEventEndTime(event));
    }
    return duration;
}

void VFXSequencer::Update(float deltaTime) {
    if (!isPlaying_) return;
    if (deltaTime <= 0.0001f) return;

    float timeStep = deltaTime;
    currentTime_ += timeStep;

    bool allFinished = true;
    for (auto& event : events_) {
        if (event.isFinished) continue;

        if (currentTime_ < event.triggerTime) {
            allFinished = false;
            continue;
        }

        if (!event.hasFired) {
            GameplayEventTrace::GetInstance()->Record(
                GameplayEventTracePhase::Fired,
                GetVFXEventTypeName(event.type),
                DescribeTraceTarget(targetObject_),
                event.presetName,
                (sequenceName_.empty() ? "(unsaved sequence)" : sequenceName_) +
                    " @ " + std::to_string(event.triggerTime) + " sec");
        }

        if (event.type == VFXEventType::PostEffectPulse) {
            ApplyPostPulse(event, currentTime_);
        }
        else if (event.type == VFXEventType::LightPulse) {
            if (!event.hasFired) {
                Matrix4x4 emitMat;
                Vector3 lightPos = ResolveSequencerPosition(targetObject_, useRootPosition_, rootPosition_, rootScale_, rootRotation_, event.offset, emitMat);
                LightManager::GetInstance()->PlayPointLightPulse(
                    lightPos,
                    event.lightColor,
                    event.intensity,
                    event.lightRadius,
                    event.duration,
                    event.lightDecay);
                event.hasFired = true;
            }
            if (currentTime_ >= event.triggerTime + (std::max)(event.duration, 0.01f)) {
                event.isFinished = true;
            }
        }
        else if (event.type == VFXEventType::MovingParticle) {
            event.hasFired = true;

            float duration = (std::max)(event.duration, 0.01f);
            float progress = (currentTime_ - event.triggerTime) / duration;
            if (progress >= 1.0f) {
                progress = 1.0f;
                event.isFinished = true;
            }

            float easeT = ApplyEventEasing(event.easingType, progress);
            Vector3 localPos = CalculateBezier(event.offset, event.controlPoint, event.endOffset, easeT);

            Matrix4x4 emitMat;
            Vector3 spawnPos = ResolveSequencerPosition(targetObject_, useRootPosition_, rootPosition_, rootScale_, rootRotation_, localPos, emitMat);
            emitMat.m[3][0] = spawnPos.x;
            emitMat.m[3][1] = spawnPos.y;
            emitMat.m[3][2] = spawnPos.z;
            GPUParticleManager::GetInstance()->Emit(event.presetName, spawnPos, emitMat);
        }
        else if (!event.hasFired) {
            if (event.type == VFXEventType::GPUParticle) {
                Matrix4x4 emitMat;
                Vector3 spawnPos = ResolveSequencerPosition(targetObject_, useRootPosition_, rootPosition_, rootScale_, rootRotation_, event.offset, emitMat);
                GPUParticleManager::GetInstance()->Emit(event.presetName, spawnPos, emitMat);
            }
            else if (event.type == VFXEventType::MeshEffect) {
                std::string path = "Resources/json/effect/" + event.presetName + ".json";
                Object3d* target = targetObject_;
                Vector3 eventScale = {
                    event.scale.x * rootScale_.x,
                    event.scale.y * rootScale_.y,
                    event.scale.z * rootScale_.z
                };
                Vector3 eventRotation = event.rotation;
                if (target) {
                    Vector3 spawnOffset = {
                        event.offset.x * rootScale_.x,
                        event.offset.y * rootScale_.y,
                        event.offset.z * rootScale_.z
                    };
                    if (useRootPosition_) {
                        spawnOffset.x += rootPosition_.x;
                        spawnOffset.y += rootPosition_.y;
                        spawnOffset.z += rootPosition_.z;
                    }
                    eventRotation.x += rootRotation_.x;
                    eventRotation.y += rootRotation_.y;
                    eventRotation.z += rootRotation_.z;
                    MeshEffectManager::GetInstance()->SpawnEffect(path, target, spawnOffset, eventRotation, eventScale);
                } else {
                    Matrix4x4 emitMat;
                    Vector3 spawnPos = ResolveSequencerPosition(
                        targetObject_,
                        useRootPosition_,
                        rootPosition_,
                        rootScale_,
                        rootRotation_,
                        event.offset,
                        emitMat);
                    eventRotation.x += rootRotation_.x;
                    eventRotation.y += rootRotation_.y;
                    eventRotation.z += rootRotation_.z;
                    MeshEffectManager::GetInstance()->SpawnEffectAt(path, spawnPos, eventRotation, eventScale);
                }
            }
            else if (event.type == VFXEventType::SoundEffect) {
                Matrix4x4 emitMatrix;
                Vector3 spawnPosition = ResolveSequencerPosition(
                    targetObject_, useRootPosition_, rootPosition_, rootScale_, rootRotation_, event.offset, emitMatrix);
                AudioEventSystem::GetInstance()->PlayReference(event.presetName, &spawnPosition);
            }
            else if (event.type == VFXEventType::CameraShake) {
                CameraManager::GetInstance()->PlayShake(event.duration, event.intensity, event.frequency, event.scale);
            }
            else if (event.type == VFXEventType::CameraFovPulse) {
                CameraManager::GetInstance()->PlayFovPulse(event.duration, event.intensity * kDegreesToRadians, event.attackRatio);
            }
            else if (event.type == VFXEventType::ControllerRumble) {
                InputManager::GetInstance()->PlayRumble(event.intensity, event.secondaryIntensity, event.duration);
            }
            else if (event.type == VFXEventType::HitStop) {
                RequestHitStop(event.duration, event.intensity);
            }

            event.hasFired = true;
            event.isFinished = true;
        }

        if (!event.isFinished) {
            allFinished = false;
        }
    }

    if (allFinished) {
        isPlaying_ = false;
        TraceSequence(
            GameplayEventTracePhase::Completed,
            sequenceName_.empty() ? "(unsaved sequence)" : sequenceName_,
            targetObject_,
            std::to_string(events_.size()) + " events completed");
    }
}

void VFXSequencer::Save(const std::string& sequenceName) {
    json root;
    root["version"] = 3;
    root["events"] = json::array();

    for (const auto& event : events_) {

        json eventJson;
        eventJson["type"] = static_cast<int>(event.type);
        eventJson["presetName"] = event.presetName;
        eventJson["triggerTime"] = event.triggerTime;
        eventJson["offset"] = WriteVector3(event.offset);
        eventJson["rotation"] = WriteVector3(event.rotation);
        eventJson["scale"] = WriteVector3(event.scale);
        eventJson["controlPoint"] = WriteVector3(event.controlPoint);
        eventJson["endOffset"] = WriteVector3(event.endOffset);
        eventJson["duration"] = event.duration;
        eventJson["easingType"] = event.easingType;
        eventJson["intensity"] = event.intensity;
        eventJson["frequency"] = event.frequency;
        eventJson["radialIntensity"] = event.radialIntensity;
        eventJson["secondaryIntensity"] = event.secondaryIntensity;
        eventJson["damageFlash"] = event.damageFlash;
        eventJson["attackRatio"] = event.attackRatio;
        eventJson["chromaticAberration"] = event.chromaticAberration;
        eventJson["wobbleIntensity"] = event.wobbleIntensity;
        eventJson["bloomIntensity"] = event.bloomIntensity;
        eventJson["lightRadius"] = event.lightRadius;
        eventJson["lightDecay"] = event.lightDecay;
        eventJson["lightColor"] = WriteVector4(event.lightColor);
        root["events"].push_back(eventJson);
    }

    std::filesystem::create_directories(kSequenceDirectory);
    std::string filepath = std::string(kSequenceDirectory) + sequenceName + ".json";

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << root.dump(4);
        SequenceEventCache()[sequenceName] = events_;
        ResetEventRuntimeState(SequenceEventCache()[sequenceName]);
        try {
            SequenceWriteTimeCache()[sequenceName] = std::filesystem::last_write_time(filepath);
        } catch (const std::filesystem::filesystem_error&) {
            SequenceWriteTimeCache().erase(sequenceName);
        }
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Saved VFX Sequence: " + sequenceName);
        }
    }
}

void VFXSequencer::Load(const std::string& sequenceName) {
    sequenceName_ = sequenceName;
    std::string filepath = std::string(kSequenceDirectory) + sequenceName + ".json";
    std::filesystem::file_time_type currentWriteTime{};
    bool hasWriteTime = false;
    try {
        if (std::filesystem::exists(filepath)) {
            currentWriteTime = std::filesystem::last_write_time(filepath);
            hasWriteTime = true;
        }
    } catch (const std::filesystem::filesystem_error&) {
        hasWriteTime = false;
    }
    Reset();

    auto& cache = SequenceEventCache();
    auto& writeTimeCache = SequenceWriteTimeCache();
    auto cacheIt = cache.find(sequenceName);
    auto writeTimeIt = writeTimeCache.find(sequenceName);
    const bool cacheIsFresh =
        cacheIt != cache.end() &&
        hasWriteTime &&
        writeTimeIt != writeTimeCache.end() &&
        writeTimeIt->second == currentWriteTime;
    if (cacheIsFresh) {
        events_ = cacheIt->second;
        ResetEventRuntimeState(events_);
        Reset();
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Loaded VFX Sequence from cache: " + sequenceName);
        }
        return;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        TraceSequence(
            GameplayEventTracePhase::Failed,
            sequenceName,
            targetObject_,
            "Sequence JSONを開けません");
        return;
    }

    json root;
    file >> root;
    events_.clear();

    if (root.contains("events") && root["events"].is_array()) {
        for (const auto& eventJson : root["events"]) {
            VFXEvent event;
            int type = eventJson.value("type", static_cast<int>(VFXEventType::GPUParticle));
            type = std::clamp(type, static_cast<int>(VFXEventType::GPUParticle), static_cast<int>(VFXEventType::CameraFovPulse));
            event.type = static_cast<VFXEventType>(type);
            event.presetName = eventJson.value("presetName", std::string{});
            event.triggerTime = eventJson.value("triggerTime", 0.0f);
            event.offset = eventJson.contains("offset") ? ReadVector3(eventJson["offset"], event.offset) : event.offset;
            event.rotation = eventJson.contains("rotation") ? ReadVector3(eventJson["rotation"], event.rotation) : event.rotation;
            event.scale = eventJson.contains("scale") ? ReadVector3(eventJson["scale"], event.scale) : event.scale;
            event.controlPoint = eventJson.contains("controlPoint") ? ReadVector3(eventJson["controlPoint"], event.controlPoint) : event.controlPoint;
            event.endOffset = eventJson.contains("endOffset") ? ReadVector3(eventJson["endOffset"], event.endOffset) : event.endOffset;
            event.duration = eventJson.value("duration", 1.0f);
            event.easingType = eventJson.value("easingType", 0);
            event.intensity = eventJson.value("intensity", 0.2f);
            event.frequency = eventJson.value("frequency", 24.0f);
            event.radialIntensity = eventJson.value("radialIntensity", 0.0f);
            event.damageFlash = eventJson.value("damageFlash", 0.0f);
            event.secondaryIntensity = eventJson.value("secondaryIntensity", 0.7f);
            event.chromaticAberration = eventJson.value("chromaticAberration", 0.0f);
            event.attackRatio = eventJson.value("attackRatio", 0.12f);
            event.wobbleIntensity = eventJson.value("wobbleIntensity", 0.0f);
            event.bloomIntensity = eventJson.value("bloomIntensity", 0.0f);
            event.lightRadius = eventJson.value("lightRadius", 9.0f);
            event.lightDecay = eventJson.value("lightDecay", 1.4f);
            event.lightColor = eventJson.contains("lightColor") ? ReadVector4(eventJson["lightColor"], event.lightColor) : event.lightColor;
            events_.push_back(event);
        }
    }

    ResetEventRuntimeState(events_);
    cache[sequenceName] = events_;
    if (hasWriteTime) {
        writeTimeCache[sequenceName] = currentWriteTime;
    } else {
        writeTimeCache.erase(sequenceName);
    }
    Reset();

    if (events_.empty()) {
        TraceSequence(
            GameplayEventTracePhase::Warning,
            sequenceName,
            targetObject_,
            "再生可能なEventがありません");
    }
    if (DebugConsole::GetInstance()) {
        DebugConsole::GetInstance()->AddLog("Loaded VFX Sequence: " + sequenceName);
    }
}

void VFXSequencer::PlayOneShot(const std::string& sequenceName, const Vector3& position) {
    PlayOneShot(sequenceName, position, { 1.0f, 1.0f, 1.0f });
}

void VFXSequencer::PlayOneShot(const std::string& sequenceName, const Vector3& position, const Vector3& scale) {
    PlayOneShot(sequenceName, position, scale, { 0.0f, 0.0f, 0.0f });
}

void VFXSequencer::PlayOneShot(const std::string& sequenceName, const Vector3& position, const Vector3& scale, const Vector3& rotation) {
    auto sequence = std::make_unique<VFXSequencer>();
    sequence->Initialize(nullptr);
    sequence->Load(sequenceName);
    if (sequence->GetEvents().empty()) {
        TraceSequence(
            GameplayEventTracePhase::Warning,
            sequenceName,
            nullptr,
            "OneShotを開始できません: Eventがありません");
        return;
    }

    sequence->SetRootPosition(position);
    sequence->SetRootScale(scale);
    sequence->SetRootRotation(rotation);
    sequence->Play();
    ActiveOneShotSequences().push_back(std::move(sequence));
}

void VFXSequencer::PlayOneShotOnTarget(const std::string& sequenceName, Object3d* targetObject) {
    PlayOneShotOnTarget(
        sequenceName,
        targetObject,
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f });
}

void VFXSequencer::PlayOneShotOnTarget(
    const std::string& sequenceName,
    Object3d* targetObject,
    const Vector3& localOffset,
    const Vector3& scale,
    const Vector3& rotation) {
    if (!targetObject) {
        TraceSequence(
            GameplayEventTracePhase::Failed,
            sequenceName,
            nullptr,
            "Target Objectがnullです");
        return;
    }

    auto sequence = std::make_unique<VFXSequencer>();
    sequence->Initialize(targetObject);
    sequence->Load(sequenceName);
    if (sequence->GetEvents().empty()) {
        TraceSequence(
            GameplayEventTracePhase::Warning,
            sequenceName,
            targetObject,
            "Target OneShotを開始できません: Eventがありません");
        return;
    }

    sequence->SetRootPosition(localOffset);
    sequence->SetRootScale(scale);
    sequence->SetRootRotation(rotation);
    sequence->Play();
    ActiveOneShotSequences().push_back(std::move(sequence));
}

void VFXSequencer::UpdateOneShots(float deltaTime) {
    auto& sequences = ActiveOneShotSequences();
    for (auto& sequence : sequences) {
        if (sequence) {
            sequence->Update(deltaTime);
        }
    }

    sequences.erase(
        std::remove_if(
            sequences.begin(),
            sequences.end(),
            [](const std::unique_ptr<VFXSequencer>& sequence) {
                return !sequence || !sequence->IsPlaying();
            }),
        sequences.end());
}

void VFXSequencer::ClearOneShots() {
    for (auto& sequence : ActiveOneShotSequences()) {
        if (sequence) {
            sequence->Stop();
        }
    }
    ActiveOneShotSequences().clear();
    ActiveHitStops().clear();
    gameplayTimeScale = 1.0f;
    InputManager::GetInstance()->StopRumble();
}

void VFXSequencer::RequestHitStop(float duration, float timeScale) {
    if (duration <= 0.0f) {
        return;
    }
    ActiveHitStops().push_back({ duration, std::clamp(timeScale, 0.0f, 1.0f) });
}

void VFXSequencer::UpdateFeedbackRuntime(float unscaledDeltaTime) {
    const float timeStep = (std::max)(unscaledDeltaTime, 0.0f);
    auto& layers = ActiveHitStops();
    for (HitStopLayer& layer : layers) {
        layer.remaining -= timeStep;
    }
    layers.erase(
        std::remove_if(layers.begin(), layers.end(), [](const HitStopLayer& layer) {
            return layer.remaining <= 0.0f;
        }),
        layers.end());

    gameplayTimeScale = 1.0f;
    for (const HitStopLayer& layer : layers) {
        gameplayTimeScale = (std::min)(gameplayTimeScale, layer.timeScale);
    }
}

float VFXSequencer::GetGameplayTimeScale() {
    return gameplayTimeScale;
}
