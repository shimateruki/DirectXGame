#pragma once

#include "GPUParticleConfig.h"
#include "json.hpp"

// 実行時とEditorで同じプリセット項目を読書きする。
// 呼び出し側が初期値を決め、この読込処理はJSONに存在する項目だけを上書きする。
namespace GPUParticlePresetJsonCodec {
using Json = nlohmann::json;

template <typename T>
inline void ReadValue(const Json& json, const char* key, T& target) {
    if (const auto found = json.find(key); found != json.end()) {
        target = found->get<T>();
    }
}

// キーがある場合は3成分配列として読む。型や成分不足の例外は呼び出し側で扱う。
inline void ReadVector3(const Json& json, const char* key, engine::Vector3& target) {
    if (const auto found = json.find(key); found != json.end()) {
        target.x = (*found)[0].get<float>();
        target.y = (*found)[1].get<float>();
        target.z = (*found)[2].get<float>();
    }
}

inline void ReadVector4(const Json& json, const char* key, engine::Vector4& target) {
    if (const auto found = json.find(key); found != json.end()) {
        target.x = (*found)[0].get<float>();
        target.y = (*found)[1].get<float>();
        target.z = (*found)[2].get<float>();
        target.w = (*found)[3].get<float>();
    }
}

inline void ReadLegacyFlag(const Json& json, const char* key, int& target) {
    if (const auto found = json.find(key); found != json.end()) {
        // 古いプリセットの整数値と、Editorが保存するbool値をどちらも受け取る。
        target = found->is_boolean() ? (found->get<bool>() ? 1 : 0) : found->get<int>();
    }
}

inline void Read(const Json& json, GPUParticleConfig& config) {
    ReadVector3(json, "emitPos", config.emitPos);
    ReadVector3(json, "emitArea", config.emitArea);
    ReadVector3(json, "emitVelocity", config.emitVelocity);
    ReadValue(json, "emitCount", config.emitCount);

    if (const auto found = json.find("lod"); found != json.end() && found->is_object()) {
        config.lod.enabled = found->value("enabled", false);
        config.lod.nearDistance = found->value("nearDistance", 12.0f);
        config.lod.farDistance = found->value("farDistance", 45.0f);
        config.lod.farEmissionScale = found->value("farEmissionScale", 0.25f);
        config.lod.maxAliveParticles = found->value("maxAliveParticles", 0);
        config.lod.Sanitize();
    }

    ReadValue(json, "emitLife", config.emitLife);
    ReadValue(json, "maxParticles", config.maxParticles);
    ReadValue(json, "velocityVariance", config.velocityVariance);
    ReadVector4(json, "baseColor", config.baseColor);
    ReadVector4(json, "endColor", config.endColor);
    ReadValue(json, "baseSize", config.baseSize);
    ReadValue(json, "endSize", config.endSize);
    ReadValue(json, "rotSpeed", config.rotSpeed);
    ReadValue(json, "blendModeIndex", config.blendModeIndex);
    ReadVector3(json, "envGravity", config.envGravity);
    ReadValue(json, "envDrag", config.envDrag);
    ReadVector3(json, "envWind", config.envWind);
    ReadValue(json, "envTurbulence", config.envTurbulence);
    ReadValue(json, "fieldType", config.fieldType);
    ReadVector3(json, "fieldPosition", config.fieldPosition);
    ReadValue(json, "fieldStrength", config.fieldStrength);
    ReadValue(json, "fieldRadius", config.fieldRadius);
    ReadValue(json, "fieldFalloff", config.fieldFalloff);
    ReadValue(json, "isLooping", config.isLooping);
    ReadValue(json, "emitInterval", config.emitInterval);
    ReadValue(json, "shapeType", config.shapeType);
    ReadValue(json, "shapeRadius", config.shapeRadius);
    ReadValue(json, "shapeAngle", config.shapeAngle);
    ReadVector4(json, "midColor", config.midColor);
    ReadValue(json, "colorMidTime", config.colorMidTime);
    ReadValue(json, "midSize", config.midSize);
    ReadValue(json, "sizeMidTime", config.sizeMidTime);
    ReadValue(json, "softParticleFade", config.softParticleFade);
    ReadValue(json, "sizeEaseType", config.sizeEaseType);
    ReadValue(json, "colorEaseType", config.colorEaseType);
    ReadValue(json, "enableCollision", config.enableCollision);
    ReadValue(json, "restitution", config.restitution);
    ReadValue(json, "colorIntensity", config.colorIntensity);
    ReadValue(json, "texturePath", config.texturePath);

    // 平坦な旧形式を先に読み、まとまった新形式で指定された項目を優先する。
    ReadValue(json, "spriteSheetColumns", config.spriteSheetColumns);
    ReadValue(json, "spriteSheetRows", config.spriteSheetRows);
    ReadValue(json, "spriteSheetFrameCount", config.spriteSheetFrameCount);
    ReadValue(json, "spriteSheetFps", config.spriteSheetFps);
    ReadLegacyFlag(json, "spriteSheetLoop", config.spriteSheetLoop);
    ReadLegacyFlag(json, "spriteSheetRandomStart", config.spriteSheetRandomStart);
    ReadLegacyFlag(json, "alignToVelocity", config.alignToVelocity);
    ReadValue(json, "velocityStretch", config.velocityStretch);
    ReadValue(json, "particleType", config.particleType);
    ReadValue(json, "trailLength", config.trailLength);
    ReadLegacyFlag(json, "receiveLighting", config.receiveLighting);
    ReadVector3(json, "lightDirection", config.lightDirection);
    ReadVector3(json, "lightColor", config.lightColor);
    ReadValue(json, "lightingStrength", config.lightingStrength);

    if (const auto found = json.find("spriteAnimation"); found != json.end() && found->is_object()) {
        ReadValue(*found, "columns", config.spriteSheetColumns);
        ReadValue(*found, "rows", config.spriteSheetRows);
        ReadValue(*found, "frameCount", config.spriteSheetFrameCount);
        ReadValue(*found, "fps", config.spriteSheetFps);
        ReadLegacyFlag(*found, "loop", config.spriteSheetLoop);
        ReadLegacyFlag(*found, "randomStart", config.spriteSheetRandomStart);
    }
}

// 実行時の粒子やGPUバッファは含めず、編集可能なプリセット設定をJSONへ変換する。
inline Json Write(const GPUParticleConfig& config) {
    Json json;
    json["emitPos"] = {config.emitPos.x, config.emitPos.y, config.emitPos.z};
    json["emitArea"] = {config.emitArea.x, config.emitArea.y, config.emitArea.z};
    json["emitVelocity"] = {config.emitVelocity.x, config.emitVelocity.y, config.emitVelocity.z};
    json["emitCount"] = config.emitCount;
    json["emitLife"] = config.emitLife;
    json["maxParticles"] = config.maxParticles;
    json["velocityVariance"] = config.velocityVariance;
    json["baseColor"] = {config.baseColor.x, config.baseColor.y, config.baseColor.z, config.baseColor.w};
    json["envGravity"] = {config.envGravity.x, config.envGravity.y, config.envGravity.z};
    json["envDrag"] = config.envDrag;
    json["envWind"] = {config.envWind.x, config.envWind.y, config.envWind.z};
    json["envTurbulence"] = config.envTurbulence;
    json["fieldType"] = config.fieldType;
    json["fieldPosition"] = {config.fieldPosition.x, config.fieldPosition.y, config.fieldPosition.z};
    json["fieldStrength"] = config.fieldStrength;
    json["fieldRadius"] = config.fieldRadius;
    json["fieldFalloff"] = config.fieldFalloff;
    json["baseSize"] = config.baseSize;
    json["endSize"] = config.endSize;
    json["rotSpeed"] = config.rotSpeed;
    json["endColor"] = {config.endColor.x, config.endColor.y, config.endColor.z, config.endColor.w};
    json["shapeType"] = config.shapeType;
    json["shapeRadius"] = config.shapeRadius;
    json["shapeAngle"] = config.shapeAngle;
    json["blendModeIndex"] = config.blendModeIndex;
    json["isLooping"] = config.isLooping;
    json["emitInterval"] = config.emitInterval;
    json["midColor"] = {config.midColor.x, config.midColor.y, config.midColor.z, config.midColor.w};
    json["colorMidTime"] = config.colorMidTime;
    json["midSize"] = config.midSize;
    json["sizeMidTime"] = config.sizeMidTime;
    json["softParticleFade"] = config.softParticleFade;
    json["sizeEaseType"] = config.sizeEaseType;
    json["colorEaseType"] = config.colorEaseType;
    json["enableCollision"] = config.enableCollision;
    json["restitution"] = config.restitution;
    json["colorIntensity"] = config.colorIntensity;
    json["texturePath"] = config.texturePath;
    json["alignToVelocity"] = config.alignToVelocity != 0;
    json["velocityStretch"] = config.velocityStretch;
    json["particleType"] = config.particleType;
    json["trailLength"] = config.trailLength;
    json["receiveLighting"] = config.receiveLighting != 0;
    json["lightDirection"] = {config.lightDirection.x, config.lightDirection.y, config.lightDirection.z};
    json["lightColor"] = {config.lightColor.x, config.lightColor.y, config.lightColor.z};
    json["lightingStrength"] = config.lightingStrength;
    json["spriteAnimation"] = {{"columns", config.spriteSheetColumns},
                               {"rows", config.spriteSheetRows},
                               {"frameCount", config.spriteSheetFrameCount},
                               {"fps", config.spriteSheetFps},
                               {"loop", config.spriteSheetLoop != 0},
                               {"randomStart", config.spriteSheetRandomStart != 0}};
    json["lod"] = {{"enabled", config.lod.enabled},
                   {"nearDistance", config.lod.nearDistance},
                   {"farDistance", config.lod.farDistance},
                   {"farEmissionScale", config.lod.farEmissionScale},
                   {"maxAliveParticles", config.lod.maxAliveParticles}};
    return json;
}
} // namespace GPUParticlePresetJsonCodec
