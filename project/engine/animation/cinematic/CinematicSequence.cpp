#define NOMINMAX
#include "engine/animation/cinematic/CinematicSequence.h"

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <numbers>
#include "engine/utility/math/AnimationInterpolation.h"

namespace engine {

using json = nlohmann::json;
void SaveCinematicPresentation(json&, const CinematicPresentation&);
void LoadCinematicPresentation(const json&, CinematicPresentation&);

CinematicVisualKey SampleCinematicVisual(const CinematicObjectClip& clip, float time, const CinematicSequence* sequence) {
    if (clip.motions.empty()) {
        return SampleCinematicKeys(clip.keys, time, [](const auto& a, const auto& b, float seconds) {
            auto k = a;
            static_cast<CinematicTransformKey&>(k) = InterpolateCinematicTransform(a,b,seconds,true);
            const float rate = std::clamp((seconds-a.time)/std::max(0.000001f,b.time-a.time),0.0f,1.0f);
            k.color = Math::Lerp(a.color,b.color,rate);
            k.emissive = Math::Lerp(a.emissive,b.emissive,rate);
            k.clipHeight = Math::Lerp(a.clipHeight,b.clipHeight,rate);
            k.bounce = Math::Lerp(a.bounce,b.bounce,rate);
            return k;
        });
    }
    // Signalがある区間は同じ節目を参照し、時刻を別のキー列へ複製しない。
    const auto timing = [sequence](float seconds, const std::string& signal) {
        return sequence && !signal.empty() ? sequence->GetSignalTime(signal,seconds) : seconds;
    };

    // 区間の隙間では直前の終点を保持し、先頭より前は始点を返す。
    const CinematicMotionSegment* selected = &clip.motions.back();
    float sampleTime = time;
    for (size_t i=0; i<clip.motions.size(); ++i) {
        const auto& motion = clip.motions[i];
        const float begin = timing(motion.startTime,motion.startSignal);
        if (time < begin) {
            selected = &clip.motions[i==0?0:i-1];
            sampleTime = i==0 ? begin : timing(selected->endTime,selected->endSignal);
            break;
        }
        if (time < timing(motion.endTime,motion.endSignal)) { selected=&motion; break; }
    }

    const auto& motion = *selected;
    const float begin = timing(motion.startTime,motion.startSignal);
    const float end = timing(motion.endTime,motion.endSignal);
    const float raw = std::clamp((sampleTime-begin)/std::max(0.000001f,end-begin),0.0f,1.0f);
    const auto smooth=[](float x) { x=std::clamp(x,0.0f,1.0f); return x*x*(3.0f-2.0f*x); };
    const float progress = motion.smoothProgress ? smooth(raw) : raw;
    auto k = motion.from;
    k.time = time;
    k.positionReference = motion.from.positionReference;
    k.positionTarget = motion.to.positionReference;
    k.positionMix = progress;
    // 基準点間の補間は再生器が担当し、ここでは共通の弧と姿勢だけを評価する。
    const float arc = std::sin(progress*std::numbers::pi_v<float>);
    k.position = Math::Lerp(motion.from.position,motion.to.position,progress);
    k.position.y += arc*motion.arcHeight;

    // 着地の揺れは時間とともにゼロへ収束する。フレーム積分を使わず巻戻しも再現する。
    const float settle = std::clamp((sampleTime-begin)/std::max(0.000001f,motion.bounceDuration),0.0f,1.0f);
    k.position.y += std::abs(std::sin(settle*2.0f*std::numbers::pi_v<float>*motion.bounceCycles))*(1.0f-settle)*motion.bounceHeight;
    k.rotation = Math::Lerp(motion.from.rotation,motion.to.rotation,progress)+motion.rotationArc*std::sin(progress*2.0f*std::numbers::pi_v<float>*motion.rotationCycles);

    // 移動の後半だけ縮める場合も、同じ進行割合から倍率を求める。
    float scaleRate = std::clamp((progress-motion.scaleStart)/std::max(0.000001f,1.0f-motion.scaleStart),0.0f,1.0f);
    if (motion.smoothScale) scaleRate=smooth(scaleRate);
    // 初期の伸びを指数的に戻す場合と、着地で潰れを揺らして戻す場合を共有する。
    const float envelope = motion.scaleDecay > 0 ? std::exp(-scaleRate*motion.scaleDecay) : 1.0f-scaleRate;
    const float remaining = envelope*std::cos(scaleRate*2.0f*std::numbers::pi_v<float>*motion.scaleCycles);
    k.scale = Math::Lerp(motion.to.scale,motion.from.scale,remaining);
    k.scale = {std::max(k.scale.x,motion.minimumScale),std::max(k.scale.y,motion.minimumScale),std::max(k.scale.z,motion.minimumScale)};

    k.color = Math::Lerp(motion.from.color,motion.to.color,raw);
    k.emissive = Math::Lerp(motion.from.emissive,motion.to.emissive,raw)+motion.emissiveAmplitude*std::sin(time*motion.emissiveFrequency);
    k.visible = raw>=1.0f ? motion.to.visible : motion.from.visible;
    return k;
}

float CinematicSequence::GetSignalTime(const std::string& name, float fallback) const {
    const auto marker = std::find_if(signals.begin(), signals.end(), [&](const auto& signal) {
        return signal.enabled && (signal.name == name || signal.signal == name);
    });
    return marker != signals.end() ? marker->time : fallback;
}

CinematicTransformKey InterpolateCinematicTransform(const CinematicTransformKey& a,
    const CinematicTransformKey& b, float time, bool continuousRotation) {
    const float raw = continuousRotation
        ? std::clamp((time-a.time)/std::max(0.000001f,b.time-a.time),0.0f,1.0f)
        : AnimationInterpolation::SegmentRate(time,a.time,b.time);
    const float rate = AnimationInterpolation::ApplyEasing(raw,
        static_cast<AnimationInterpolation::EasingType>(std::clamp(a.easingToNext,0,4)));
    auto value = a;
    value.time = time;
    value.position = AnimationInterpolation::Lerp(a.position,b.position,rate);
    value.rotation = continuousRotation ? AnimationInterpolation::Lerp(a.rotation,b.rotation,rate)
                                       : AnimationInterpolation::SlerpEuler(a.rotation,b.rotation,rate);
    value.scale = AnimationInterpolation::Lerp(a.scale,b.scale,rate);
    return value;
}

// Editorの軌跡表示と再生器で、同じアンカー基準の座標変換を使います。
engine::Vector3 CinematicAnchorPosition(const engine::Vector3& local, const engine::Vector3& origin, float yaw) {
    // アンカーの向きでローカル座標を回してから、ワールド原点へ移す。
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {origin.x + local.x * c + local.z * s, origin.y + local.y, origin.z - local.x * s + local.z * c};
}

engine::Vector3 CinematicAnchorLocalPosition(const engine::Vector3& world, const engine::Vector3& origin, float yaw) {
    // 編集したワールド位置を逆回転し、保存用のアンカー相対座標へ戻す。
    return CinematicAnchorPosition({world.x - origin.x, world.y - origin.y, world.z - origin.z}, {}, -yaw);
}

// JSONライブラリの型変換を使い、保存と読込の項目一覧を一か所にする。
// 座標の変換はこの翻訳単位だけで使い、他のJSONの保存形式へ影響させない。
static void to_json(json& data, const engine::Vector3& value) {
    data = {value.x, value.y, value.z};
}
static void from_json(const json& data, engine::Vector3& value) {
    value = {};
    if (data.is_array() && data.size() >= 3) value = {data[0], data[1], data[2]};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CinematicObjectBinding, targetName, targetEventId)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CinematicTransformKey,
    time, position, rotation, scale, easingToNext)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CinematicTransformTrack,
    name, binding, legacyPathFile, startTime, enabled, muted, locked, relative, anchorRelative, holdLast, keys)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CinematicVfxTrackData,
    name, binding, sequenceName, startTime, duration, snapshotTargetPosition, rootOffset, rootScale, enabled, muted)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CinematicCameraShot,
    name, binding, startTime, duration, blendInDuration, blendOutDuration, easing, enabled, muted)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CinematicAnimationClipData,
    name, binding, driver, clipName, startTime, duration, playbackSpeed, blendInDuration, easing, loop, restoreOnStop, enabled, muted)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CinematicAudioClipData,
    name, audioId, startTime, duration, volume, loop, enabled, muted)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CinematicSignalMarker,
    time, name, signal, payload, enabled, binding)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CinematicEventMarker, time, eventId, name, binding)

namespace {
// 既存素材の平坦なBinding・旧項目名は入口と出口だけで変換する。
template<class T> json WriteTracks(const std::vector<T>& tracks) {
    auto items = json::array();
    for (const auto& track : tracks) {
        json item = track;
        if constexpr (requires { track.binding; }) {
            item.update(item.at("binding"));
            item.erase("binding");
        }
        if constexpr (requires { track.legacyPathFile; }) {
            item["pathFileName"] = track.legacyPathFile;
            item.erase("legacyPathFile");
        }
        if constexpr (requires { track.legacyPathFile; } || requires { track.sequenceName; })
            item["delayTime"] = track.startTime;
        items.push_back(std::move(item));
    }
    return items;
}

// 省略値は従来通りとし、変更した素材だけ形式が変わることを避ける。
template<class T> void ReadTracks(const json& items, std::vector<T>& tracks, const char* defaultName) {
    for (auto item : items) {
        if constexpr (requires { T{}.binding; }) item["binding"] = item.get<CinematicObjectBinding>();
        if constexpr (requires { T{}.legacyPathFile; }) {
            item["name"] = item.value("name", item.value("targetName", ""));
            item["legacyPathFile"] = item.value("pathFileName", "");
            if (!item.contains("keys")) item["keys"] = json::array();
            for (auto& key : item["keys"])
                if (key.contains("scale") && (!key["scale"].is_array() || key["scale"].size() < 3)) key.erase("scale");
        } else if constexpr (requires { T{}.sequenceName; }) {
            item["name"] = item.value("name", item.value("sequenceName", "VFX"));
            if (item.contains("rootScale") && (!item["rootScale"].is_array() || item["rootScale"].size() < 3)) item.erase("rootScale");
        } else if constexpr (std::is_same_v<T, CinematicCameraShot>)
            item["name"] = item.value("name", item.value("targetName", "Camera Shot"));
        else item["name"] = item.value("name", defaultName);
        if constexpr (requires { T{}.legacyPathFile; } || requires { T{}.sequenceName; })
            item["startTime"] = item.value("startTime", item.value("delayTime", 0.0f));
        if constexpr (requires { T{}.driver; }) item["driver"] = item.value("driver", "Model");
        tracks.push_back(item.get<T>());
    }
}
} // namespace

void CinematicSequence::Clear() {
    // 次の演出を読み込む前に全トラックとプレビュー用の役割を初期状態へ戻す。
    anchor = {};
    anchorYawOffset = 0.0f;
    roleBindings = {};
    previewModels = {"Characters/slime_pink", "Primitives/cube"};
    version = kCurrentVersion;
    duration = 0.0f;
    transformTracks.clear();
    vfxTracks.clear();
    cameraShots.clear();
    animationClips.clear();
    audioClips.clear();
    signals.clear();
    events.clear();
    presentation = {};
}

// 再生側が時刻順に走査できるよう、評価に順序が必要な項目だけ並べ替えます。
void CinematicSequence::Sort() {
    for (auto& clip : presentation.objects)
        std::stable_sort(clip.keys.begin(), clip.keys.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    std::stable_sort(presentation.view.begin(), presentation.view.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    // 補間が前後のキーを正しく見つけられるよう、各トラックを時刻順に揃える。
    for (auto& track : transformTracks) {
        std::sort(track.keys.begin(), track.keys.end(),
                  [](const CinematicTransformKey& a, const CinematicTransformKey& b) { return a.time < b.time; });
    }

    std::sort(events.begin(), events.end(),
              [](const CinematicEventMarker& a, const CinematicEventMarker& b) { return a.time < b.time; });
    std::sort(cameraShots.begin(), cameraShots.end(),
              [](const CinematicCameraShot& a, const CinematicCameraShot& b) { return a.startTime < b.startTime; });
    std::sort(animationClips.begin(), animationClips.end(),
              [](const CinematicAnimationClipData& a, const CinematicAnimationClipData& b) {
                  return a.startTime < b.startTime;
              });
    std::sort(
        audioClips.begin(), audioClips.end(),
        [](const CinematicAudioClipData& a, const CinematicAudioClipData& b) { return a.startTime < b.startTime; });
    std::sort(signals.begin(), signals.end(),
              [](const CinematicSignalMarker& a, const CinematicSignalMarker& b) { return a.time < b.time; });
}

// 入力されたdurationより長いトラックがあれば、末尾まで再生時間を延ばします。
float CinematicSequence::GetAuthoredDuration() const {
    // 設定上の長さと各トラックの最後のキーを比較し、途中で演出を切らない。
    float result = std::max(0.0f, duration);
    for (const auto& clip : presentation.objects)
        if (clip.enabled && !clip.keys.empty()) result = std::max(result, clip.keys.back().time);
    for (const auto& clip : presentation.objects)
        if (clip.enabled) for (const auto& motion : clip.motions)
            result = std::max(result, motion.endSignal.empty() ? motion.endTime : GetSignalTime(motion.endSignal,motion.endTime));
    if (!presentation.view.empty()) result = std::max(result, presentation.view.back().time);

    for (const auto& track : transformTracks) {
        if (!track.keys.empty()) {
            result = std::max(result, track.startTime + std::max(0.0f, track.keys.back().time));
        }
    }

    for (const auto& track : vfxTracks) {
        result = std::max(result, track.startTime + std::max(0.1f, track.duration));
    }

    for (const auto& shot : cameraShots) {
        result = std::max(result, shot.startTime + std::max(0.05f, shot.duration));
    }

    for (const auto& clip : animationClips) {
        result = std::max(result, clip.startTime + std::max(0.05f, clip.duration));
    }

    for (const auto& clip : audioClips) {
        result = std::max(result, clip.startTime + std::max(0.05f, clip.duration));
    }

    for (const auto& signal : signals) {
        result = std::max(result, signal.time);
    }

    for (const auto& event : events) {
        result = std::max(result, event.time);
    }

    return std::max(result, 0.1f);
}

// 保存と読込は同じ型変換を通す。旧項目の扱いは上の互換入口へ閉じ込める。
bool CinematicSequence::Save(const std::string& filePath) const {
    json root = {{"version", kCurrentVersion}, {"name", name}, {"duration", duration},
        {"anchor", anchor}, {"anchorYawOffset", anchorYawOffset},
        {"roleBindings", roleBindings}, {"previewModels", previewModels},
        {"tracks", WriteTracks(transformTracks)}, {"vfxCues", WriteTracks(vfxTracks)},
        {"cameraShots", WriteTracks(cameraShots)}, {"animationClips", WriteTracks(animationClips)},
        {"audioClips", WriteTracks(audioClips)}, {"signals", WriteTracks(signals)}, {"events", WriteTracks(events)}};
    SaveCinematicPresentation(root, presentation);
    const std::filesystem::path path(filePath);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << root.dump(4);
    return file.good();
}

bool CinematicSequence::Load(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return false;
    json root;
    try { file >> root; }
    catch (...) { return false; }
    try {
        Clear();
        version = root.value("version", 1);
        name = root.value("name", std::filesystem::path(filePath).stem().string());
        duration = root.value("duration", 0.0f);
        anchor = root.value("anchor", json::object()).get<CinematicObjectBinding>();
        anchorYawOffset = root.value("anchorYawOffset", 0.0f);
        const auto roles = root.value("roleBindings", json::array());
        const auto models = root.value("previewModels", json::array());
        previewModels = {"Characters/slime_pink", "Gimmicks/portal_gate"};
        for (size_t i = 0; i < previewModels.size() && i < models.size(); ++i) previewModels[i] = models[i].get<std::string>();
        for (size_t i = 0; i < roleBindings.size() && i < roles.size(); ++i) roleBindings[i] = roles[i].get<CinematicObjectBinding>();
        ReadTracks(root.value("tracks", json::array()), transformTracks, "");
        ReadTracks(root.value("vfxCues", root.value("vfxTracks", json::array())), vfxTracks, "VFX");
        ReadTracks(root.value("cameraShots", json::array()), cameraShots, "Camera Shot");
        ReadTracks(root.value("animationClips", json::array()), animationClips, "Animation Clip");
        ReadTracks(root.value("audioClips", json::array()), audioClips, "Audio Clip");
        ReadTracks(root.value("signals", json::array()), signals, "Signal");
        ReadTracks(root.value("events", json::array()), events, "");
        LoadCinematicPresentation(root, presentation);
    } catch (...) {
        Clear();
        return false;
    }
    Sort();
    return true;
}

} // namespace engine
