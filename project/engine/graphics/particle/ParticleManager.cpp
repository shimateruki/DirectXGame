#include "ParticleManager.h"
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <algorithm>
// プロジェクト内のパスに合わせて調整してください
#include "externals/nlohmann/json.hpp"

using json = nlohmann::json;

ParticleManager* ParticleManager::GetInstance() {
    static ParticleManager instance;
    return &instance;
}

void ParticleManager::Initialize(ParticleSystem* particleSystem) {
    particleSystem_ = particleSystem;
    // 起動時に全データ読み込み
    LoadAllParams();
}

// ---------------------------------------------------------
// JSON変換ヘルパー関数
// ---------------------------------------------------------
namespace engine {

// Vector3
void to_json(json& j, const engine::Vector3& v) { j = json{ {"x", v.x}, {"y", v.y}, {"z", v.z} }; }
void from_json(const json& j, engine::Vector3& v) { j.at("x").get_to(v.x); j.at("y").get_to(v.y); j.at("z").get_to(v.z); }
// Vector4
void to_json(json& j, const engine::Vector4& v) { j = json{ {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} }; }
void from_json(const json& j, engine::Vector4& v) { j.at("x").get_to(v.x); j.at("y").get_to(v.y); j.at("z").get_to(v.z); j.at("w").get_to(v.w); }

} // namespace engine

namespace {
// 時刻と値のキー列をParticle保存JSONへ変換します。
json WriteFloatCurve(const VFXFloatCurve& curve) {
    json result = json::array();
    for (const VFXFloatCurveKey& key : curve.keys) {
        result.push_back({
            { "time", key.time },
            { "value", key.value },
            { "easing", static_cast<int>(key.easing) },
        });
    }
    return result;
}

// 時刻と色のキー列をParticle保存JSONへ変換します。
json WriteColorGradient(const VFXColorGradient& gradient) {
    json result = json::array();
    for (const VFXColorGradientKey& key : gradient.keys) {
        result.push_back({
            { "time", key.time },
            { "color", key.color },
            { "easing", static_cast<int>(key.easing) },
        });
    }
    return result;
}

// 保存済みの数値カーブキーをEmitter設定へ復元します。
void ReadFloatCurve(const json& source, VFXFloatCurve& curve) {
    // 保存値を曲線キーへ戻し、時刻順や境界値はNormalizeで整える。
    if (!source.is_array()) return;
    curve.keys.clear();
    for (const json& item : source) {
        if (!item.is_object()) continue;
        VFXFloatCurveKey key;
        key.time = item.value("time", 0.0f);
        key.value = item.value("value", 1.0f);
        key.easing = static_cast<VFXCurveEasing>(
            std::clamp(item.value("easing", 0), 0, 4));
        curve.keys.push_back(key);
    }
    curve.Normalize();
}

// 保存済みの色グラデーションキーをEmitter設定へ復元します。
void ReadColorGradient(const json& source, VFXColorGradient& gradient) {
    // 色キーと補間方法を読み戻し、不正な補間番号は有効範囲へ丸める。
    if (!source.is_array()) return;
    gradient.keys.clear();
    for (const json& item : source) {
        if (!item.is_object()) continue;
        VFXColorGradientKey key;
        key.time = item.value("time", 0.0f);
        if (item.contains("color")) {
            item.at("color").get_to(key.color);
        }
        key.easing = static_cast<VFXCurveEasing>(
            std::clamp(item.value("easing", 0), 0, 4));
        gradient.keys.push_back(key);
    }
    gradient.Normalize();
}

// VFXの距離別詳細度設定を保存JSONへ変換します。
json WriteLod(const VFXLodSettings& lod) {
    return {
        { "enabled", lod.enabled },
        { "nearDistance", lod.nearDistance },
        { "farDistance", lod.farDistance },
        { "farEmissionScale", lod.farEmissionScale },
        { "maxAliveParticles", lod.maxAliveParticles },
    };
}

// 保存JSONで指定された距離別詳細度設定を復元します。
void ReadLod(const json& source, VFXLodSettings& lod) {
    if (!source.is_object()) return;
    lod.enabled = source.value("enabled", false);
    lod.nearDistance = source.value("nearDistance", 12.0f);
    lod.farDistance = source.value("farDistance", 45.0f);
    lod.farEmissionScale = source.value("farEmissionScale", 0.25f);
    lod.maxAliveParticles = source.value("maxAliveParticles", 0);
    lod.Sanitize();
}
}

// EmitterParams のシリアライズ
void to_json(json& j, const ParticleSystem::EmitterParams& p) {
    j = json{
        {"spawnArea", p.spawnArea},
        {"initialVelocity", p.initialVelocity},
        {"velocityRandomness", p.velocityRandomness},
        {"emitCount", p.emitCount},
        {"particlesPerSecond", p.particlesPerSecond},
        {"particleLifetime", p.particleLifetime},
        {"startColor", p.startColor},
        {"endColor", p.endColor},
        {"startSize", p.startSize},
        {"endSize", p.endSize},
        {"initialRotationSpeed", p.initialRotationSpeed},
    {"rotationSpeedRandomness", p.rotationSpeedRandomness},
    {"hdrIntensity", p.hdrIntensity},
    {"emitterType", (int)p.emitterType}, 
    {"spawnRadius", p.spawnRadius},      
    {"coneAngle", p.coneAngle},
    {"acceleration", p.acceleration},
    {"textureName", p.textureName},
        {"blendMode", (int)p.blendMode}
        // 必要に応じて他のパラメータも追加
    };

    json legacyCurve = json::array();
    for (float value : p.sizeCurve) {
        legacyCurve.push_back(value);
    }
    j["legacySizeCurve"] = std::move(legacyCurve);
    j["authoring"] = {
        { "enabled", p.useAuthoringCurves },
        { "sizeOverLife", WriteFloatCurve(p.sizeOverLife) },
        { "colorOverLife", WriteColorGradient(p.colorOverLife) },
    };
    j["lod"] = WriteLod(p.lod);
}

void from_json(const json& j, ParticleSystem::EmitterParams& p) {
    // 旧JSONで欠けている項目は既定値を維持し、存在する設定だけ取り込む。
    if (j.contains("textureName")) {
        p.textureName = j.at("textureName").get<std::string>();
    }
    if (j.contains("spawnArea")) j.at("spawnArea").get_to(p.spawnArea);
    if (j.contains("initialVelocity")) j.at("initialVelocity").get_to(p.initialVelocity);
    if (j.contains("velocityRandomness")) j.at("velocityRandomness").get_to(p.velocityRandomness);
    p.emitCount = std::clamp(j.value("emitCount", 1), 1, ParticleSystem::GetMaxParticles());
    if (j.contains("particlesPerSecond")) j.at("particlesPerSecond").get_to(p.particlesPerSecond);
    if (j.contains("particleLifetime")) j.at("particleLifetime").get_to(p.particleLifetime);
    if (j.contains("startColor")) j.at("startColor").get_to(p.startColor);
    if (j.contains("endColor")) j.at("endColor").get_to(p.endColor);
    if (j.contains("startSize")) j.at("startSize").get_to(p.startSize);
    if (j.contains("endSize")) j.at("endSize").get_to(p.endSize);
    if(j.contains("initialRotationSpeed")) j.at("initialRotationSpeed").get_to(p.initialRotationSpeed);
    if (j.contains("hdrIntensity")) j.at("hdrIntensity").get_to(p.hdrIntensity);
    if (j.contains("rotationSpeedRandomness")) j.at("rotationSpeedRandomness").get_to(p.rotationSpeedRandomness);
    p.emitterType = (EmitterType)j.value("emitterType", (int)EmitterType::Box); 
    p.spawnRadius = j.value("spawnRadius", 1.0f); 
    p.coneAngle = j.value("coneAngle", 30.0f);
    if (j.contains("acceleration")) j.at("acceleration").get_to(p.acceleration);
    p.blendMode = (ParticleBlendMode)j.value("blendMode", (int)ParticleBlendMode::kAlpha);

    if (j.contains("legacySizeCurve") && j["legacySizeCurve"].is_array()) {
        // 古いサイズカーブは固定長なので配列境界で切って読み込む。
        const json& values = j["legacySizeCurve"];
        const std::size_t count = (std::min)(values.size(), static_cast<std::size_t>(10));
        for (std::size_t index = 0; index < count; ++index) {
            p.sizeCurve[index] = values[index].get<float>();
        }
    }
    if (j.contains("authoring") && j["authoring"].is_object()) {
        // 編集用カーブのキーが不足する設定は有効扱いにしない。
        const json& authoring = j["authoring"];
        p.useAuthoringCurves = authoring.value("enabled", false);
        if (authoring.contains("sizeOverLife")) {
            ReadFloatCurve(authoring["sizeOverLife"], p.sizeOverLife);
        }
        if (authoring.contains("colorOverLife")) {
            ReadColorGradient(authoring["colorOverLife"], p.colorOverLife);
        }
        if (p.sizeOverLife.keys.empty() || p.colorOverLife.keys.empty()) {
            p.useAuthoringCurves = false;
        }
    }
    if (j.contains("lod")) {
        ReadLod(j["lod"], p.lod);
    }
}

// ---------------------------------------------------------
// メイン処理
// ---------------------------------------------------------

void ParticleManager::SaveParam(const std::string& name, const ParticleSystem::EmitterParams& param) {
    paramsMap_[name] = param; // メモリ上の辞書更新

    // ディレクトリ確認
    if (!std::filesystem::exists(kDirectoryPath)) {
        std::filesystem::create_directories(kDirectoryPath);
    }

    // ファイル書き出し
    std::string filePath = kDirectoryPath + name + ".json";
    std::ofstream o(filePath);
    if (o.is_open()) {
        json j = param;
        o << std::setw(4) << j << std::endl; // 綺麗に整形して保存
    }
}

void ParticleManager::LoadAllParams() {
    if (!std::filesystem::exists(kDirectoryPath)) return;

    for (const auto& entry : std::filesystem::directory_iterator(kDirectoryPath)) {
        if (entry.path().extension() == ".json") {
            // ファイル名(拡張子なし)をキーにする
            std::string name = entry.path().stem().string();
            LoadParam(name);
        }
    }
}

// 名前に対応するEmitter設定を読み、成功したものだけ登録済みパラメータへ反映します。
void ParticleManager::LoadParam(const std::string& name) {
    std::string filePath = kDirectoryPath + name + ".json";
    std::ifstream i(filePath);
    if (i.is_open()) {
        json j;
        i >> j;
        ParticleSystem::EmitterParams param;
        from_json(j, param);
        paramsMap_[name] = param;
    }
}

void ParticleManager::Emit(const std::string& name, const engine::Vector3& position, float& timer) {
    // 登録されていない名前なら何もしない
    if (paramsMap_.find(name) == paramsMap_.end()) return;

    // パラメータ取得
    const auto& params = paramsMap_[name];
    if (params.particlesPerSecond <= 0.0f) {
        return;
    }

    // 生成レート計算 (1秒間に何個出すか -> 1個出すのに何秒かかるか)
    float rate = 1.0f / params.particlesPerSecond;

    // タイマーを進める (60FPS固定と仮定。可変ならdeltaTimeを引数でもらうべき)
    timer += 1.0f / 60.0f;

    // particlesPerSecondは実粒子数/秒なので、この経路では1粒ずつ生成します。
    // emitCountはEditorの単発発生や明示的なEmitOneShot呼び出しだけに適用します。
    ParticleSystem::EmitterParams singleParticleParams = params;
    singleParticleParams.emitCount = 1;

    // 生成タイミングになったらループで放出
    while (timer >= rate) {
        timer -= rate;
        particleSystem_->EmitOneShot(singleParticleParams, position);
    }
}
