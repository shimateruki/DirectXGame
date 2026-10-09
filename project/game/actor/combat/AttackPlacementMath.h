#pragma once
#include "AttackSequenceData.h"
#include "engine/utility/math/Math.h"

// ギズモのワールド移動を、保存形式である右・上・前の相対位置へ戻します。
namespace AttackPlacementMath {
inline Vector3 ToLocalDelta(Vector3 delta, float yaw) {
    return {delta.x * std::cos(yaw) - delta.z * std::sin(yaw), delta.y,
        delta.x * std::sin(yaw) + delta.z * std::cos(yaw)};
}

// 発射方向を回しても発生位置は動かさないよう、サイズ依存オフセットを含めて逆変換します。
inline void RotateEvent(AttackSequenceEvent& event, Vector3 ownerSize, float deltaYaw) {
    const float right = event.right + ownerSize.x * event.sizeRight;
    const float forward = event.forward + ownerSize.z * event.sizeForward;
    event.right = right * std::cos(deltaYaw) - forward * std::sin(deltaYaw) - ownerSize.x * event.sizeRight;
    event.forward = right * std::sin(deltaYaw) + forward * std::cos(deltaYaw) - ownerSize.z * event.sizeForward;
    event.yawDegrees = std::remainder(event.yawDegrees + deltaYaw * 57.295779513f,360.0f);
}

// 現在時刻へキーを置き、近接する既存キーの時刻と並び順を保ちます。
inline bool PutKey(AttackSequenceTrack& track, AttackTransformKey key) {
    auto found = std::find_if(track.keys.begin(),track.keys.end(),[&](const auto& k){return std::abs(k.time-key.time)<0.0001f;});
    if (found != track.keys.end()) { key.time=found->time; *found=key; return true; }
    if(track.keys.size()>=128) return false;
    track.keys.push_back(key);
    std::sort(track.keys.begin(),track.keys.end(),[](const auto& a,const auto& b){return a.time<b.time;});
    return true;
}
}
