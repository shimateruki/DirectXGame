#include "engine/animation/cinematic/CinematicSequence.h"
#include "json.hpp"
#include <stdexcept>
#include <cmath>

namespace engine {

using json = nlohmann::json;

namespace {
engine::Vector3 V3(const json& a) { return {a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>()}; }
engine::Vector4 V4(const json& a) { return {a.at(0).get<float>(), a.at(1).get<float>(), a.at(2).get<float>(), a.at(3).get<float>()}; }
json J3(const engine::Vector3& v) { return {v.x, v.y, v.z}; }
json J4(const engine::Vector4& v) { return {v.x, v.y, v.z, v.w}; }

// 演出の配列形式をここで選ぶ。項目を省略した既存データには型の既定値を使う。
Vector3 ReadMotionVector(const json& data, const char* field, Vector3 fallback) {
    const auto item = data.find(field);
    if (item == data.end()) return fallback;
    if (!item->is_array() || item->size() != 3) throw std::runtime_error("Invalid spring vector");
    return V3(*item);
}

Vector4 ReadMotionColor(const json& data, const char* field, Vector4 fallback) {
    const auto item = data.find(field);
    if (item == data.end()) return fallback;
    if (!item->is_array() || item->size() != 4) throw std::runtime_error("Invalid visual color");
    return V4(*item);
}
} // namespace

// 区間の始終点は名前付きオブジェクト、ベクトルと色は既存の数値配列で書き出す。
void to_json(json& data, const CinematicVisualKey& key) {
    data = {
        {"positionReference", key.positionReference},
        {"position", J3(key.position)},
        {"rotation", J3(key.rotation)},
        {"scale", J3(key.scale)},
        {"color", J4(key.color)},
        {"emissive", key.emissive},
        {"visible", key.visible},
    };
}

void from_json(const json& data, CinematicVisualKey& key) {
    const CinematicVisualKey defaults;
    key.positionReference = data.value("positionReference", defaults.positionReference);
    key.position = ReadMotionVector(data, "position", defaults.position);
    key.rotation = ReadMotionVector(data, "rotation", defaults.rotation);
    key.scale = ReadMotionVector(data, "scale", defaults.scale);
    key.color = ReadMotionColor(data, "color", defaults.color);
    key.emissive = data.value("emissive", defaults.emissive);
    key.visible = data.value("visible", defaults.visible);
}

void to_json(json& data, const CinematicMotionSegment& motion) {
    data = {
        {"name", motion.name},
        {"startTime", motion.startTime},
        {"endTime", motion.endTime},
        {"startSignal", motion.startSignal},
        {"endSignal", motion.endSignal},
        {"startSignalOffset", motion.startSignalOffset},
        {"endSignalOffset", motion.endSignalOffset},
        {"from", motion.from},
        {"to", motion.to},
        {"smoothProgress", motion.smoothProgress},
        {"arcHeight", motion.arcHeight},
        {"rotationArc", J3(motion.rotationArc)},
        {"rotationCycles", motion.rotationCycles},
        {"smoothScale", motion.smoothScale},
        {"scaleWaveAmplitude", J3(motion.scaleWaveAmplitude)},
        {"scaleWaveFrequency", motion.scaleWaveFrequency},
        {"scaleWaveFadeIn", motion.scaleWaveFadeIn},
        {"scaleDecay", motion.scaleDecay},
        {"scaleCycles", motion.scaleCycles},
        {"scaleStart", motion.scaleStart},
        {"minimumScale", motion.minimumScale},
        {"bounceHeight", motion.bounceHeight},
        {"bounceDuration", motion.bounceDuration},
        {"bounceCycles", motion.bounceCycles},
        {"emissiveAmplitude", motion.emissiveAmplitude},
        {"emissiveFrequency", motion.emissiveFrequency},
    };
}

void from_json(const json& data, CinematicMotionSegment& motion) {
    const CinematicMotionSegment defaults;
    motion.name = data.value("name", defaults.name);
    motion.startTime = data.value("startTime", defaults.startTime);
    motion.endTime = data.value("endTime", defaults.endTime);
    motion.startSignal = data.value("startSignal", defaults.startSignal);
    motion.endSignal = data.value("endSignal", defaults.endSignal);
    motion.startSignalOffset = data.value("startSignalOffset", defaults.startSignalOffset);
    motion.endSignalOffset = data.value("endSignalOffset", defaults.endSignalOffset);
    motion.from = data.value("from", defaults.from);
    motion.to = data.value("to", defaults.to);
    motion.smoothProgress = data.value("smoothProgress", defaults.smoothProgress);
    motion.arcHeight = data.value("arcHeight", defaults.arcHeight);
    motion.rotationArc = ReadMotionVector(data, "rotationArc", defaults.rotationArc);
    motion.rotationCycles = data.value("rotationCycles", defaults.rotationCycles);
    motion.smoothScale = data.value("smoothScale", defaults.smoothScale);
    motion.scaleWaveAmplitude = ReadMotionVector(data, "scaleWaveAmplitude", defaults.scaleWaveAmplitude);
    motion.scaleWaveFrequency = data.value("scaleWaveFrequency", defaults.scaleWaveFrequency);
    motion.scaleWaveFadeIn = data.value("scaleWaveFadeIn", defaults.scaleWaveFadeIn);
    motion.scaleDecay = data.value("scaleDecay", defaults.scaleDecay);
    motion.scaleCycles = data.value("scaleCycles", defaults.scaleCycles);
    motion.scaleStart = data.value("scaleStart", defaults.scaleStart);
    motion.minimumScale = data.value("minimumScale", defaults.minimumScale);
    motion.bounceHeight = data.value("bounceHeight", defaults.bounceHeight);
    motion.bounceDuration = data.value("bounceDuration", defaults.bounceDuration);
    motion.bounceCycles = data.value("bounceCycles", defaults.bounceCycles);
    motion.emissiveAmplitude = data.value("emissiveAmplitude", defaults.emissiveAmplitude);
    motion.emissiveFrequency = data.value("emissiveFrequency", defaults.emissiveFrequency);
}

void to_json(json& data, const CinematicSpringSettings& settings) {
    data = {
        {"initialPositionOffset", J3(settings.initialPositionOffset)},
        {"initialRotationOffset", J3(settings.initialRotationOffset)},
        {"initialForwardVelocity", settings.initialForwardVelocity},
        {"positionStiffness", settings.positionStiffness},
        {"positionDamping", settings.positionDamping},
        {"angularStiffness", settings.angularStiffness},
        {"angularDamping", settings.angularDamping},
        {"maxPlanarOffset", settings.maxPlanarOffset},
        {"maxSink", settings.maxSink},
        {"maxLift", settings.maxLift},
        {"tiltFromOffset", settings.tiltFromOffset},
        {"tiltFromVelocity", settings.tiltFromVelocity},
        {"sourceTiltFollow", settings.sourceTiltFollow},
        {"initialVerticalVelocity", settings.initialVerticalVelocity},
        {"initialRollVelocity", settings.initialRollVelocity},
        {"compressionScale", settings.compressionScale},
    };
}

void from_json(const json& data, CinematicSpringSettings& settings) {
    const CinematicSpringSettings defaults;
    settings.initialPositionOffset = ReadMotionVector(data, "initialPositionOffset", defaults.initialPositionOffset);
    settings.initialRotationOffset = ReadMotionVector(data, "initialRotationOffset", defaults.initialRotationOffset);
    settings.initialForwardVelocity = data.value("initialForwardVelocity", defaults.initialForwardVelocity);
    settings.positionStiffness = data.value("positionStiffness", defaults.positionStiffness);
    settings.positionDamping = data.value("positionDamping", defaults.positionDamping);
    settings.angularStiffness = data.value("angularStiffness", defaults.angularStiffness);
    settings.angularDamping = data.value("angularDamping", defaults.angularDamping);
    settings.maxPlanarOffset = data.value("maxPlanarOffset", defaults.maxPlanarOffset);
    settings.maxSink = data.value("maxSink", defaults.maxSink);
    settings.maxLift = data.value("maxLift", defaults.maxLift);
    settings.tiltFromOffset = data.value("tiltFromOffset", defaults.tiltFromOffset);
    settings.tiltFromVelocity = data.value("tiltFromVelocity", defaults.tiltFromVelocity);
    settings.sourceTiltFollow = data.value("sourceTiltFollow", defaults.sourceTiltFollow);
    settings.initialVerticalVelocity = data.value("initialVerticalVelocity", defaults.initialVerticalVelocity);
    settings.initialRollVelocity = data.value("initialRollVelocity", defaults.initialRollVelocity);
    settings.compressionScale = data.value("compressionScale", defaults.compressionScale);
}

namespace {
// 非有限値や不正な配列長をGPUへ渡さず、読込時にエラーへまとめる。
void CheckRow(const json& row, size_t count) {
    if (!row.is_array() || row.size() != count)
        throw std::runtime_error("Invalid cinematic sample size");
    for (const auto& value : row)
        if (!value.is_number() || !std::isfinite(value.get<float>()))
            throw std::runtime_error("Invalid cinematic sample value");
}
}

void SaveCinematicPresentation(json& root, const CinematicPresentation& p) {
    auto& data = root["presentation"];
    data = {{"reference", p.reference}, {"lookToward", p.lookToward}, {"hideObjects", p.hideObjects},
            {"objects", json::array()}, {"view", json::array()}, {"particles", json::array()}};
    if (!p.springs.empty()) data["springs"] = p.springs;
    if (p.viewWorldSpace) data["viewWorldSpace"] = true;
    for (const auto& clip : p.objects) {
        json item = {{"name", clip.name}, {"model", clip.model}, {"source", clip.source},
            {"positionSpace", clip.positionSpace}, {"pivot", J3(clip.pivot)}, {"spawn", clip.spawn},
            {"additive", clip.additive}, {"lit", clip.lit}, {"enabled", clip.enabled},
            {"inheritScale", clip.inheritScale}, {"inheritRotation", clip.inheritRotation},
            {"clipScaleHeight", clip.clipScaleHeight}, {"floorHeight", clip.floorHeight},
            {"clipCenterOffset", clip.clipCenterOffset},
            {"clipEdgeWidth", clip.clipEdgeWidth}, {"clipDissolve", clip.clipDissolve},
            {"clipColor", J4(clip.clipColor)}, {"keys", json::array()}};
        if (!clip.motions.empty()) item["motions"] = clip.motions;
        if (!clip.animate) item["animate"] = false;
        if (clip.followPosition) item["followPosition"] = true;
        if (clip.followRotation) item["followRotation"] = true;
        if (clip.followScale) item["followScale"] = true;
        if (clip.inheritVisibility) item["inheritVisibility"] = true;
        if (clip.rotationMask.x != 1 || clip.rotationMask.y != 1 || clip.rotationMask.z != 1)
            item["rotationMask"] = J3(clip.rotationMask);
        // 位置参照を使うキーだけ末尾3項目を追加します。先頭19項目の並びは旧ファイルと共通です。
        for (const auto& k : clip.keys) {
            item["keys"].push_back({k.time, k.position.x, k.position.y, k.position.z,
                k.rotation.x, k.rotation.y, k.rotation.z, k.scale.x, k.scale.y, k.scale.z,
                k.color.x, k.color.y, k.color.z, k.color.w, k.emissive, k.clipHeight,
                k.bounce, k.visible ? 1 : 0, k.clipEnabled ? 1 : 0});
            if (!k.positionReference.empty()) {
                item["keys"].back().push_back(k.positionReference);
                item["keys"].back().push_back(k.positionTarget);
                item["keys"].back().push_back(k.positionMix);
            }
        }
        data["objects"].push_back(std::move(item));
    }
    for (const auto& k : p.view) {
        data["view"].push_back({k.time, k.focusMix, k.focusOffset.x, k.focusOffset.y, k.focusOffset.z,
            k.scaledFocusOffset.x, k.scaledFocusOffset.y, k.scaledFocusOffset.z,
            k.eyeOffset.x, k.eyeOffset.y, k.eyeOffset.z, k.jitter.x, k.jitter.y, k.jitter.z, k.fov, k.bars});
        if (!k.reference.empty() || !k.lookToward.empty() || !k.eyeReference.empty()) {
            data["view"].back().push_back(k.reference);
            data["view"].back().push_back(k.lookToward);
            if (!k.eyeReference.empty()) data["view"].back().push_back(k.eyeReference);
        }
    }
    for (const auto& clip : p.particles)
        data["particles"].push_back({{"target", clip.target}, {"preset", clip.preset},
            {"directionMode", clip.directionMode}, {"startTime", clip.startTime}, {"endTime", clip.endTime},
            {"interval", clip.interval}, {"scale", clip.scale}, {"direction", J3(clip.direction)}});
}

void LoadCinematicPresentation(const json& root, CinematicPresentation& p) {
    // 演出拡張のない旧Sequenceは呼び出し元が初期化した設定で再生できます。
    if (!root.contains("presentation"))
        return;
    const auto& data = root.at("presentation");
    p.springs = data.value("springs", std::unordered_map<std::string, CinematicSpringSettings>{});
    // 数値の有限性と係数の範囲を読込時に検査し、後段のばね積分へ不正な設定を渡さないようにします。
    for (const auto& [name, settings] : p.springs) {
        if (name.empty()) throw std::runtime_error("Empty cinematic spring name");
        const json values = settings;
        for (const auto& value : values.flatten())
            if (!std::isfinite(value.get<float>())) throw std::runtime_error("Non-finite cinematic spring setting");
        if (settings.positionStiffness < 0 || settings.positionDamping < 0 ||
            settings.angularStiffness < 0 || settings.angularDamping < 0 ||
            settings.maxPlanarOffset < 0 || settings.maxSink < 0 || settings.maxLift < 0 ||
            settings.compressionScale < 0 || settings.compressionScale > 1)
            throw std::runtime_error("Invalid cinematic spring coefficient");
    }

    p.reference = data.value("reference", "");
    p.lookToward = data.value("lookToward", "");
    p.viewWorldSpace = data.value("viewWorldSpace", false);
    p.hideObjects = data.value("hideObjects", std::vector<std::string>{});
    for (const auto& item : data.value("objects", json::array())) {
        CinematicObjectClip clip;
        clip.name = item.at("name").get<std::string>();
        clip.source = item.at("source").get<std::string>();
        clip.model = item.value("model", "");
        clip.positionSpace = item.value("positionSpace", "world");
        if (clip.positionSpace != "world" && clip.positionSpace != "model" && clip.positionSpace != "radial")
            throw std::runtime_error("Unknown cinematic position space");
        clip.pivot = V3(item.value("pivot", json::array({0, 0, 0})));
        clip.spawn = item.value("spawn", true);
        clip.additive = item.value("additive", false);
        clip.lit = item.value("lit", true);
        clip.enabled = item.value("enabled", true);
        clip.animate = item.value("animate", true);
        clip.followPosition = item.value("followPosition", false);
        clip.followRotation = item.value("followRotation", false);
        clip.followScale = item.value("followScale", false);
        clip.inheritVisibility = item.value("inheritVisibility", false);
        clip.rotationMask = V3(item.value("rotationMask", json::array({1, 1, 1})));
        clip.inheritScale = item.value("inheritScale", true);
        clip.inheritRotation = item.value("inheritRotation", true);
        clip.clipScaleHeight = item.value("clipScaleHeight", false);
        clip.floorHeight = item.value("floorHeight", -100000.0f);
        clip.clipCenterOffset = item.value("clipCenterOffset", 0.0f);
        clip.clipEdgeWidth = item.value("clipEdgeWidth", 0.075f);
        clip.clipDissolve = item.value("clipDissolve", 0.20f);
        clip.clipColor = V4(item.value("clipColor", json::array({1, 1, 1, 1})));
        for (const auto& row : item.value("keys",json::array())) {
            if (!row.is_array() || (row.size() != 19 && row.size() != 22))
                throw std::runtime_error("Invalid visual key size");
            // 旧形式と拡張形式に共通の先頭19項目は数値です。拡張形式の末尾で参照名と位置の混合率を読みます。
            CheckRow(json(row.begin(), row.begin() + 19), 19);
            CinematicVisualKey k;
            k.time = row[0]; k.position = {row[1], row[2], row[3]};
            k.rotation = {row[4], row[5], row[6]}; k.scale = {row[7], row[8], row[9]};
            k.color = {row[10], row[11], row[12], row[13]};
            k.emissive = row[14]; k.clipHeight = row[15]; k.bounce = row[16];
            k.visible = row[17].get<float>() != 0; k.clipEnabled = row[18].get<float>() != 0;
            if (row.size() == 22) {
                k.positionReference = row[19].get<std::string>();
                k.positionTarget = row[20].get<std::string>();
                k.positionMix = row[21].get<float>();
                if (k.positionReference.empty() || !std::isfinite(k.positionMix) || k.positionMix < 0 || k.positionMix > 1)
                    throw std::runtime_error("Invalid visual position binding");
            }
            clip.keys.push_back(k);
        }
        clip.motions=item.value("motions",std::vector<CinematicMotionSegment>{});
        // 動作区間と姿勢キーが同じ対象を取り合わないよう、どちらか一方の設定だけを受け付けます。
        if (!clip.motions.empty() && !clip.keys.empty()) throw std::runtime_error("Conflicting cinematic key and motion data");
        float previousEnd=-1;
        for (const auto& motion : clip.motions) {
            const json values=motion;
            for (const auto& value : values.flatten())
                if (value.is_number() && !std::isfinite(value.get<float>())) throw std::runtime_error("Non-finite motion value");
            if (motion.startTime<0 || motion.endTime<=motion.startTime || motion.startTime<previousEnd ||
                motion.scaleStart<0 || motion.scaleStart>=1 || motion.minimumScale<0 || motion.bounceDuration<=0 ||
                motion.scaleWaveFrequency<0 || motion.bounceHeight<0 || motion.bounceCycles<0 || motion.rotationCycles<0 || motion.scaleDecay<0 || motion.scaleCycles<0 || motion.from.positionReference.empty()!=motion.to.positionReference.empty())
                throw std::runtime_error("Invalid cinematic motion interval");
            previousEnd=motion.endTime;
        }
        p.objects.push_back(std::move(clip));
    }
    for (const auto& row : data.value("view", json::array())) {
        if (!row.is_array() || (row.size() != 16 && row.size() != 18 && row.size() != 19))
            throw std::runtime_error("Invalid cinematic view sample size");
        // カメラキーの先頭16項目は共通です。追加された参照名は項目数で見分け、既存ファイルも読めるようにします。
        CheckRow(json(row.begin(), row.begin() + 16), 16);
        CinematicViewKey k;
        k.time = row[0]; k.focusMix = row[1]; k.focusOffset = {row[2], row[3], row[4]};
        k.scaledFocusOffset = {row[5], row[6], row[7]}; k.eyeOffset = {row[8], row[9], row[10]};
        k.jitter = {row[11], row[12], row[13]}; k.fov = row[14]; k.bars = row[15];
        if (row.size() >= 18) {
            k.reference = row[16].get<std::string>();
            k.lookToward = row[17].get<std::string>();
            if (row.size() == 19) k.eyeReference = row[18].get<std::string>();
        }
        p.view.push_back(k);
    }
    for (const auto& item : data.value("particles", json::array())) {
        CinematicParticleClip clip;
        clip.target = item.at("target").get<std::string>(); clip.preset = item.at("preset").get<std::string>();
        clip.directionMode = item.value("directionMode", "fixed");
        clip.startTime = item.value("startTime", 0.0f); clip.endTime = item.value("endTime", 1.0f);
        clip.interval = item.value("interval", 1.0f / 9.0f); clip.scale = item.value("scale", 1.0f);
        clip.direction = V3(item.value("direction", json::array({0, 1, 0})));
        // 発生間隔と開始・終了時刻は秒です。繰り返しの刻みが極端に小さい設定や逆順の区間をここで拒否します。
        if (!std::isfinite(clip.interval) || clip.interval < 0.01f || clip.endTime < clip.startTime)
            throw std::runtime_error("Invalid cinematic particle interval");
        p.particles.push_back(clip);
    }
}

} // namespace engine
