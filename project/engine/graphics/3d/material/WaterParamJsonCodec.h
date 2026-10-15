#pragma once

#include "MeshRenderer.h"
#include "json.hpp"

namespace WaterParamJsonCodec {

// シーンと単体Objectの保存経路で、同じ authored パラメータだけを書き出します。
inline nlohmann::json Write(const MeshRenderer::WaterParamForGPU& water) {
    // GPU定数全体ではなく、利用者が調整する項目だけをシーンへ保存する。
    nlohmann::json result;
    result["waveSpeed"] = water.waveSpeed;
    result["waveHeight"] = water.waveHeight;
    result["waveFrequency"] = water.waveFrequency;
    result["flowSpeedX"] = water.flowSpeedX;
    result["flowSpeedY"] = water.flowSpeedY;
    result["effectType"] = water.effectType;
    result["effectScale"] = water.effectScale;
    result["effectScaleX"] = water.effectScaleX;
    result["effectScaleY"] = water.effectScaleY;
    result["effectScaleZ"] = water.effectScaleZ;
    result["topDownDetail"] = water.topDownDetail;
    result["effectSoftness"] = water.effectSoftness;
    result["effectIntensity"] = water.effectIntensity;
    result["billboardScale"] = water.billboardScale;
    return result;
}

// 旧JSONで省略された項目は、既に設定されている値を維持します。
inline void Read(const nlohmann::json& value, MeshRenderer::WaterParamForGPU& water) {
    // 欠けているキーは既定値を保ち、古い水面設定も読み込めるようにする。
    if (value.contains("waveSpeed")) water.waveSpeed = value["waveSpeed"];
    if (value.contains("waveHeight")) water.waveHeight = value["waveHeight"];
    if (value.contains("waveFrequency")) water.waveFrequency = value["waveFrequency"];
    if (value.contains("flowSpeedX")) water.flowSpeedX = value["flowSpeedX"];
    if (value.contains("flowSpeedY")) water.flowSpeedY = value["flowSpeedY"];
    if (value.contains("effectType")) water.effectType = value["effectType"];
    if (value.contains("effectScale")) water.effectScale = value["effectScale"];
    if (value.contains("effectScaleX")) water.effectScaleX = value["effectScaleX"];
    if (value.contains("effectScaleY")) water.effectScaleY = value["effectScaleY"];
    if (value.contains("effectScaleZ")) water.effectScaleZ = value["effectScaleZ"];
    if (value.contains("topDownDetail")) water.topDownDetail = value["topDownDetail"];
    if (value.contains("effectSoftness")) water.effectSoftness = value["effectSoftness"];
    if (value.contains("effectIntensity")) water.effectIntensity = value["effectIntensity"];
    if (value.contains("billboardScale")) water.billboardScale = value["billboardScale"];
}

} // namespace WaterParamJsonCodec
