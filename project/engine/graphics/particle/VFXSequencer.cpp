#include "VFXSequencer.h"
#include "AudioPlayer.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "Easing.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "PostEffect.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <json.hpp>

using json = nlohmann::json;

namespace {
    constexpr const char* kSequenceDirectory = "Resources/json/vfx_sequence/";

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

    float SmoothStep(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
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

    void CapturePostBase(VFXEvent& event, PostEffect::Params* params) {
        if (!params || event.hasCapturedPostBase) return;

        event.baseRadialIntensity = params->radialIntensity;
        event.baseDamageFlash = params->damageFlash;
        event.baseChromaticAberration = params->chromaticAberration;
        event.baseWobbleIntensity = params->wobbleIntensity;
        event.hasCapturedPostBase = true;
    }

    void ApplyPostPulse(VFXEvent& event, float currentTime) {
        PostEffect::Params* params = PostEffect::GetInstance()->GetParams();
        if (!params) {
            event.isFinished = true;
            return;
        }

        CapturePostBase(event, params);
        float duration = (std::max)(event.duration, 0.01f);
        float progress = std::clamp((currentTime - event.triggerTime) / duration, 0.0f, 1.0f);

        if (progress >= 1.0f) {
            params->radialIntensity = event.baseRadialIntensity;
            params->damageFlash = event.baseDamageFlash;
            params->chromaticAberration = event.baseChromaticAberration;
            params->wobbleIntensity = event.baseWobbleIntensity;
            event.isFinished = true;
            return;
        }

        float envelope = 1.0f - SmoothStep(progress);
        params->radialIntensity = event.baseRadialIntensity + event.radialIntensity * envelope;
        params->damageFlash = event.baseDamageFlash + event.damageFlash * envelope;
        params->chromaticAberration = event.baseChromaticAberration + event.chromaticAberration * envelope;
        params->wobbleIntensity = event.baseWobbleIntensity + event.wobbleIntensity * envelope;
        event.hasFired = true;
    }

    float GetVFXEventEndTime(const VFXEvent& event) {
        if (event.type == VFXEventType::MovingParticle ||
            event.type == VFXEventType::CameraShake ||
            event.type == VFXEventType::PostEffectPulse) {
            return event.triggerTime + (std::max)(event.duration, 0.01f);
        }
        return event.triggerTime + 0.08f;
    }
}

void VFXSequencer::Initialize(Object3d* targetObject) {
    targetObject_ = targetObject;
    events_.clear();
    Reset();
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
}

void VFXSequencer::Stop() {
    Reset();
}

void VFXSequencer::Reset() {
    currentTime_ = 0.0f;
    isPlaying_ = false;
    PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();
    for (auto& event : events_) {
        if (postParams && event.type == VFXEventType::PostEffectPulse && event.hasCapturedPostBase) {
            postParams->radialIntensity = event.baseRadialIntensity;
            postParams->damageFlash = event.baseDamageFlash;
            postParams->chromaticAberration = event.baseChromaticAberration;
            postParams->wobbleIntensity = event.baseWobbleIntensity;
        }
        event.hasFired = false;
        event.isFinished = false;
        event.hasCapturedPostBase = false;
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

    float timeStep = deltaTime;
    if (timeStep <= 0.0001f) timeStep = 1.0f / 60.0f;
    currentTime_ += timeStep;

    bool allFinished = true;
    for (auto& event : events_) {
        if (event.isFinished) continue;

        if (currentTime_ < event.triggerTime) {
            allFinished = false;
            continue;
        }

        if (event.type == VFXEventType::PostEffectPulse) {
            ApplyPostPulse(event, currentTime_);
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
            Vector3 spawnPos = ResolveTargetPosition(targetObject_, localPos, emitMat);
            emitMat.m[3][0] = spawnPos.x;
            emitMat.m[3][1] = spawnPos.y;
            emitMat.m[3][2] = spawnPos.z;
            GPUParticleManager::GetInstance()->Emit(event.presetName, spawnPos, emitMat);
        }
        else if (!event.hasFired) {
            if (event.type == VFXEventType::GPUParticle) {
                Matrix4x4 emitMat;
                Vector3 spawnPos = ResolveTargetPosition(targetObject_, event.offset, emitMat);
                GPUParticleManager::GetInstance()->Emit(event.presetName, spawnPos, emitMat);
            }
            else if (event.type == VFXEventType::MeshEffect) {
                std::string path = "Resources/json/effect/" + event.presetName + ".json";
                MeshEffectManager::GetInstance()->SpawnEffect(path, targetObject_, event.offset, event.rotation, event.scale);
            }
            else if (event.type == VFXEventType::SoundEffect) {
                std::string path = "Resources/audio/se/" + event.presetName;
                uint32_t soundHandle = AudioPlayer::GetInstance()->LoadSoundFile(path);
                AudioPlayer::GetInstance()->PlaySE(soundHandle, false, 1.0f);
            }
            else if (event.type == VFXEventType::CameraShake) {
                Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
                if (camera) {
                    camera->StartShake(event.duration, event.intensity, event.frequency, event.scale);
                }
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
    }
}

void VFXSequencer::Save(const std::string& sequenceName) {
    json root;
    root["version"] = 2;
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
        eventJson["damageFlash"] = event.damageFlash;
        eventJson["chromaticAberration"] = event.chromaticAberration;
        eventJson["wobbleIntensity"] = event.wobbleIntensity;
        root["events"].push_back(eventJson);
    }

    std::filesystem::create_directories(kSequenceDirectory);
    std::string filepath = std::string(kSequenceDirectory) + sequenceName + ".json";

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << root.dump(4);
        if (DebugConsole::GetInstance()) {
            DebugConsole::GetInstance()->AddLog("Saved VFX Sequence: " + sequenceName);
        }
    }
}

void VFXSequencer::Load(const std::string& sequenceName) {
    std::string filepath = std::string(kSequenceDirectory) + sequenceName + ".json";
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    json root;
    file >> root;
    events_.clear();

    if (root.contains("events") && root["events"].is_array()) {
        for (const auto& eventJson : root["events"]) {
            VFXEvent event;
            int type = eventJson.value("type", static_cast<int>(VFXEventType::GPUParticle));
            type = std::clamp(type, static_cast<int>(VFXEventType::GPUParticle), static_cast<int>(VFXEventType::PostEffectPulse));
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
            event.chromaticAberration = eventJson.value("chromaticAberration", 0.0f);
            event.wobbleIntensity = eventJson.value("wobbleIntensity", 0.0f);
            events_.push_back(event);
        }
    }

    if (DebugConsole::GetInstance()) {
        DebugConsole::GetInstance()->AddLog("Loaded VFX Sequence: " + sequenceName);
    }
}
