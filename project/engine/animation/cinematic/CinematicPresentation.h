#pragma once

#include "engine/utility/math/Math.h"
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>

namespace engine {

// 既存移動トラックと見た目キーで、姿勢の形式を共有する。
// 関連：CinematicSequence（保存）、CinematicPlayer（生成・評価）、GhostDirector（編集）
struct CinematicTransformKey {
    // 時刻は秒。移動トラックではトラック開始が、見た目キーでは演出開始が0秒です。回転はラジアン、拡縮は各軸の倍率です。
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
    // 指定時刻より後ろの最初のキーを探します。同時刻のキーが並ぶ場合は最後のキーを補間の左端に選びます。
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
    // 指定した2点間のワールド位置へpositionを加算。空なら従来の座標系を使う。
    std::string positionReference, positionTarget;
    float positionMix = 0.0f; // 0で基準点、1で移動先。
    float emissive = 1.0f;
    float clipHeight = 0.0f;
    float bounce = 0.0f;
    bool visible = true;
    bool clipEnabled = false;
};

// 2地点間の移動を少数の値で作る区間。キーの大量生成やゲーム固有の分岐は持たない。
// 時刻・揺れの長さは秒、回転はrad、位置・弧・揺れ幅はワールド単位。
struct CinematicMotionSegment {
    std::string name;
    float startTime = 0.0f, endTime = 1.0f;
    std::string startSignal, endSignal; // 指定時はSignalの時刻を使い、秒数欄は未指定時の値になる。
    float startSignalOffset = 0.0f, endSignalOffset = 0.0f; // Signal指定時だけ加える秒数。負なら節目より前。
    CinematicVisualKey from, to; // 基準点・位置補正・姿勢・発光・表示。キー時刻は使用しない。
    bool smoothProgress = false; // 位置と回転を始終点で滑らかに停止させる。
    float arcHeight = 0.0f;
    engine::Vector3 rotationArc{}; // 正弦波で加える傾きの振幅。
    float rotationCycles = 0.5f; // 区間内の周期数。0.5なら従来の中央で山になる傾き。
    bool smoothScale = false;
    // 区間内だけ加える各軸の拡縮倍率。位相は演出先頭の秒数から計算し、シークでも一致する。
    engine::Vector3 scaleWaveAmplitude{};
    float scaleWaveFrequency = 0.0f; // 角速度(rad/秒)。0なら揺れを加えない。
    bool scaleWaveFadeIn = false; // 区間の進行に沿って揺れ幅をSmoothStepで立ち上げる。
    float scaleDecay = 0.0f, scaleCycles = 0.0f; // 0なら従来の補間。減衰率と区間内の揺れ周期数。
    float scaleStart = 0.0f, minimumScale = 0.0f; // 移動の進行割合で縮小開始を指定。下限は各軸の倍率。
    float bounceHeight = 0.0f, bounceDuration = 1.0f, bounceCycles = 1.0f;
    float emissiveAmplitude = 0.0f, emissiveFrequency = 0.0f; // 発光の振幅と角速度(rad/秒)。
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
    // 基準位置からの床の高さです。既定値は床による押し上げを通常の演出範囲で起こさないための低い値です。
    float floorHeight = -100000.0f;
    float clipCenterOffset = 0.0f;
    float clipEdgeWidth = 0.075f, clipDissolve = 0.20f;
    engine::Vector4 clipColor{1, 1, 1, 1};
    std::vector<CinematicVisualKey> keys;
    std::vector<CinematicMotionSegment> motions; // キー形式とは排他。区間そのものを保存・評価する。
};

class CinematicSequence;
// 見た目キーと区間設定の評価を共有する。sequence指定時はSignalの編集時刻を反映する。
CinematicVisualKey SampleCinematicVisual(const CinematicObjectClip& clip, float time,
    const CinematicSequence* sequence = nullptr);

// 2対象の間へ注視点を補間し、対象に向いた座標系でカメラ位置を指定する。
// 関連：CinematicSequence（保存）、CinematicPlayer（生成・評価）、GhostDirector（編集）
struct CinematicViewKey {
    // 空なら従来の2対象を使用する。指定時はその時刻の対象位置を注視する。
    std::string reference, lookToward, eyeReference;
    float time = 0.0f, focusMix = 0.0f;
    engine::Vector3 focusOffset{}, scaledFocusOffset{}, eyeOffset{}, jitter{};
    // fov は垂直画角のラジアン、bars は上下の演出帯の高さを PostEffect に渡すための画面比率です。
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

// Y-upの追従部品に使う位置・回転ばね。距離はワールド単位、速度は毎秒、回転はラジアン。
// Directorで保存する係数だけを持ち、積分中の位置と速度はCinematicPlayerが所有する。
struct CinematicSpringSettings {
    // 初回だけ足す位置差（ワールドY-up）と回転差（ラジアン）。装着部品ごとに保存する。
    engine::Vector3 initialPositionOffset{0.0f, -0.08f, 0.0f};
    engine::Vector3 initialRotationOffset{0.10f, 0.52f, -0.12f};
    float initialForwardVelocity = -0.18f; // 基準対象の前方向へ与える初速。負なら後ろへ揺れる。
    // 追従位置・回転の硬さと減衰。毎秒の積分に使用する。

    float positionStiffness = 76.0f;
    float positionDamping = 8.8f;
    float angularStiffness = 52.0f;
    float angularDamping = 7.4f;
    // ワールド距離で制限する横ずれ・沈み・浮き。
    float maxPlanarOffset = 0.32f;
    float maxSink = 0.18f;
    float maxLift = 0.23f;
    // 偏差と速度から作る傾き、および基準対象の回転を受け継ぐ倍率。
    float tiltFromOffset = 1.65f;
    float tiltFromVelocity = 0.045f;
    float sourceTiltFollow = 0.34f;
    // 初回の上下速度とロール角速度。圧縮は元スケールへの倍率。
    float initialVerticalVelocity = -1.15f;
    float initialRollVelocity = 2.20f;
    float compressionScale = 0.14f;
};

// カットシーンに含まれる一時部品・カメラ・粒子。ゲーム固有のクラスや状態は参照しない。
// 関連：CinematicSequence（保存）、CinematicPlayer（生成・評価）、GhostDirector（編集）
struct CinematicPresentation {
    // 名前は呼出側の追従部品ID。シーンや敵の型には依存しない。
    std::unordered_map<std::string, CinematicSpringSettings> springs;
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
