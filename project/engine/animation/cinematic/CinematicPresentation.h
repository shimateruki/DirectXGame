#pragma once

#include "engine/utility/math/Math.h"
#include <string>
#include <vector>
#include <algorithm>

namespace engine {

// 既存移動トラックと見た目キーで、姿勢の形式を共有する。
// 関連：CinematicSequence（保存）、CinematicPlayer（生成・評価）、GhostDirector（編集）
struct CinematicTransformKey {
    // 時刻はシーケンス先頭からの秒数、rotation はラジアン、scale は各軸の倍率。
    float time = 0.0f;
    engine::Vector3 position{}, rotation{}, scale{1, 1, 1};
    int easingToNext = 4;
};

// 区間探索は全キー列で共有する。離散値の補間方法は呼び出し側で指定する。
// keys は time の昇順が前提。空なら既定値、範囲外なら端のキーを返す。
template<class Key, class Blend>
// 指定秒の両側のキーを探し、呼出側の補間関数で値を求める。
Key SampleCinematicKeys(const std::vector<Key>& keys, float time, Blend blend) {
    if (keys.empty()) return {};
    auto upper = std::upper_bound(keys.begin(), keys.end(), time,
        [](float t, const Key& key) { return t < key.time; });
    if (upper == keys.begin()) return keys.front();
    if (upper == keys.end()) return keys.back();
    return blend(*(upper - 1), *upper, time);
}

// 保存済みの姿勢キーは長い回転を線形、手付けキーは従来の最短回転で補間する。
CinematicTransformKey InterpolateCinematicTransform(const CinematicTransformKey& a,
    const CinematicTransformKey& b, float time, bool continuousRotation = false);

// 指定秒の位置・回転・拡縮をキー間で補間する。
inline CinematicTransformKey SampleCinematicTransform(const std::vector<CinematicTransformKey>& keys, float time) {
    return SampleCinematicKeys(keys, time, [](const auto& a, const auto& b, float t) {
        return InterpolateCinematicTransform(a, b, t);
    });
}

// 演出Objectの姿勢と見た目を同じ時刻から評価する。再生時の所有状態は含めない。
// 関連：CinematicSequence（保存）、CinematicPlayer（生成・評価）、GhostDirector（編集）
struct CinematicVisualKey : CinematicTransformKey {
    CinematicVisualKey() { easingToNext = 0; }
    engine::Vector4 color{1, 1, 1, 1};
    float emissive = 1.0f;
    float clipHeight = 0.0f;
    float bounce = 0.0f;
    bool visible = true;
    bool clipEnabled = false;
};

// spawn=trueの部品は再生器が所有する。falseならシーンの既存Objectを操作する。
// 関連：CinematicSequence（保存）、CinematicPlayer（生成・評価）、GhostDirector（編集）
struct CinematicObjectClip {
    std::string name, model, source;
    std::string positionSpace = "world";
    engine::Vector3 pivot{};
    bool spawn = true, additive = false, lit = true, enabled = true;
    // 一時Objectの寿命は再生器が管理し、必要な姿勢成分だけ実対象へ追従させる。
    bool animate = true, followPosition = false, followRotation = false, followScale = false;
    bool inheritVisibility = false;
    engine::Vector3 rotationMask{1, 1, 1};
    bool inheritScale = true, inheritRotation = true, clipScaleHeight = false;
    float floorHeight = -100000.0f;
    float clipCenterOffset = 0.0f;
    float clipEdgeWidth = 0.075f, clipDissolve = 0.20f;
    engine::Vector4 clipColor{1, 1, 1, 1};
    std::vector<CinematicVisualKey> keys;
};

// 2対象の間へ注視点を補間し、対象に向いた座標系でカメラ位置を指定する。
// 関連：CinematicSequence（保存）、CinematicPlayer（生成・評価）、GhostDirector（編集）
struct CinematicViewKey {
    // 空なら従来の2対象を使用する。指定時はその時刻の対象位置を注視する。
    std::string reference, lookToward, eyeReference;
    float time = 0.0f, focusMix = 0.0f;
    engine::Vector3 focusOffset{}, scaledFocusOffset{}, eyeOffset{}, jitter{};
    float fov = 0.62f, bars = 0.0f;
};

// 移動する発生点から一定間隔で粒子を出す。シーク中は発火しない。
// 関連：CinematicSequence（保存）、CinematicPlayer（生成・評価）、GhostDirector（編集）
struct CinematicParticleClip {
    std::string target, preset, directionMode = "fixed";
    // 開始・終了時刻と発生間隔は秒単位。方向と拡縮は粒子生成時に適用する。
    float startTime = 0.0f, endTime = 1.0f, interval = 1.0f / 9.0f, scale = 1.0f;
    engine::Vector3 direction{0, 1, 0};
};

// カットシーンに含まれる一時部品・カメラ・粒子。ゲーム固有のクラスや状態は参照しない。
// 関連：CinematicSequence（保存）、CinematicPlayer（生成・評価）、GhostDirector（編集）
struct CinematicPresentation {
    std::string reference, lookToward;
    bool viewWorldSpace = false;
    std::vector<std::string> hideObjects;
    std::vector<CinematicObjectClip> objects;
    std::vector<CinematicViewKey> view;
    std::vector<CinematicParticleClip> particles;
};

} // namespace engine

// 既存呼び出しとの互換名。新規コードは engine:: の正式名を使用する。
using engine::CinematicVisualKey;
using engine::CinematicObjectClip;
using engine::CinematicViewKey;
using engine::CinematicParticleClip;
using engine::CinematicPresentation;
