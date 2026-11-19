#pragma once

// AttackSequenceの配置キーを、術者・親トラックの姿勢を基準に合成する計算。描画物やActorは所有しない。
#include "AttackSequenceData.h"
#include <numbers>
#include "engine/utility/math/Math.h"

// ギズモのワールド移動を、保存形式である右・上・前の相対位置へ戻します。
namespace AttackPlacementMath {
inline engine::Vector3 ToLocalDelta(engine::Vector3 delta, float yaw) {
    // yawはラジアン。水平変位を術者の右と前の単位軸へ投影し、高さは保存座標へそのまま渡します。
    return {delta.x * std::cos(yaw) - delta.z * std::sin(yaw), delta.y,
        delta.x * std::sin(yaw) + delta.z * std::cos(yaw)};
}

// 術者基準の発生位置と向きを一緒に回し、サイズ依存オフセットを含む保存形式へ戻します。
inline void RotateEvent(AttackSequenceEvent& event, engine::Vector3 ownerSize, float deltaYaw) {
    // 術者サイズに比例する成分を足した実効オフセットを回し、保存値へ戻す際に同じ成分を引きます。
    const float right = event.right + ownerSize.x * event.sizeRight;
    const float forward = event.forward + ownerSize.z * event.sizeForward;
    event.right = right * std::cos(deltaYaw) - forward * std::sin(deltaYaw) - ownerSize.x * event.sizeRight;
    event.forward = right * std::sin(deltaYaw) + forward * std::cos(deltaYaw) - ownerSize.z * event.sizeForward;
    // 入力のラジアンを保存用の度へ変換し、角度を一周の範囲へ折り返します。
    event.yawDegrees = std::remainder(event.yawDegrees + deltaYaw * (180.0f / std::numbers::pi_v<float>),360.0f);
}

// 現在時刻へキーを置き、近接する既存キーの時刻と並び順を保ちます。
inline bool PutKey(AttackSequenceTrack& track, AttackTransformKey key) {
    // 時刻差は秒で比較します。丸め誤差程度の差でキーが重なる場合は差し替え先として扱います。
    auto found = std::find_if(track.keys.begin(),track.keys.end(),[&](const auto& k){return std::abs(k.time-key.time)<0.0001f;});
    // ほぼ同時刻なら既存キーの時刻を残して値だけ差し替え、上限に達していても更新を許可します。
    if (found != track.keys.end()) {
        key.time=found->time;
        *found=key;
        return true;
    }
    // 追加時だけ128件の上限を確認し、時刻順に並べてから補間側へ渡します。
    if(track.keys.size()>=128)
        return false;
    track.keys.push_back(key);
    std::sort(track.keys.begin(),track.keys.end(),[](const auto& a,const auto& b){return a.time<b.time;});
    return true;
}
}
