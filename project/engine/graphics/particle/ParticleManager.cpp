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
// Vector3
void to_json(json& j, const Vector3& v) { j = json{ {"x", v.x}, {"y", v.y}, {"z", v.z} }; }
void from_json(const json& j, Vector3& v) { j.at("x").get_to(v.x); j.at("y").get_to(v.y); j.at("z").get_to(v.z); }
// Vector4
void to_json(json& j, const Vector4& v) { j = json{ {"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w} }; }
void from_json(const json& j, Vector4& v) { j.at("x").get_to(v.x); j.at("y").get_to(v.y); j.at("z").get_to(v.z); j.at("w").get_to(v.w); }

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
}

void from_json(const json& j, ParticleSystem::EmitterParams& p) {
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

void ParticleManager::Emit(const std::string& name, const Vector3& position, float& timer) {
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
