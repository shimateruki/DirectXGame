#include "engine/graphics/effect/VfxAssetPath.h"
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

void ParticleManager::Initialize(engine::ParticleSystem* particleSystem) {
    particleSystem_ = particleSystem;

    // 起動時に全データ読み込み
    LoadAllParams();
}

// ---------------------------------------------------------
// JSON変換ヘルパー関数
// ---------------------------------------------------------
namespace {

// 粒子のベクトルと色は成分名を持つJSONオブジェクトとして保存する。
json WriteParticleVector(const engine::Vector3& v) {
    return {{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

engine::Vector3 ReadParticleVector(const json& data) {
    return {data.at("x").get<float>(), data.at("y").get<float>(), data.at("z").get<float>()};
}

json WriteParticleColor(const engine::Vector4& color) {
    return {{"x", color.x}, {"y", color.y}, {"z", color.z}, {"w", color.w}};
}

engine::Vector4 ReadParticleColor(const json& data) {
    return {data.at("x").get<float>(), data.at("y").get<float>(),
            data.at("z").get<float>(), data.at("w").get<float>()};
}

// 時刻と値のキー列をParticle保存JSONへ変換します。
json WriteFloatCurve(const VfxFloatCurve& curve) {
    json result = json::array();

    for (const VfxFloatCurveKey& key : curve.keys) {
        result.push_back({
            { "time", key.time },
            { "value", key.value },
            { "easing", static_cast<int>(key.easing) },
        });
    }

    return result;
}

// 時刻と色のキー列をParticle保存JSONへ変換します。
json WriteColorGradient(const VfxColorGradient& gradient) {
    json result = json::array();

    for (const VfxColorGradientKey& key : gradient.keys) {
        result.push_back({
            { "time", key.time },
            { "color", WriteParticleColor(key.color) },
            { "easing", static_cast<int>(key.easing) },
        });
    }

    return result;
}

// 保存済みの数値カーブキーをEmitter設定へ復元します。
void ReadFloatCurve(const json& source, VfxFloatCurve& curve) {
    // 保存値を曲線キーへ戻し、時刻順や境界値はNormalizeで整える。
    if (!source.is_array())
        return;
    curve.keys.clear();

    for (const json& item : source) {
        if (!item.is_object())
            continue;
        VfxFloatCurveKey key;
        key.time = item.value("time", 0.0f);
        key.value = item.value("value", 1.0f);
        key.easing = static_cast<VfxCurveEasing>(
            std::clamp(item.value("easing", 0), 0, 4));
        curve.keys.push_back(key);
    }

    curve.Normalize();
}

// 保存済みの色グラデーションキーをEmitter設定へ復元します。
void ReadColorGradient(const json& source, VfxColorGradient& gradient) {
    // 色キーと補間方法を読み戻し、不正な補間番号は有効範囲へ丸める。
    if (!source.is_array())
        return;
    gradient.keys.clear();

    for (const json& item : source) {
        if (!item.is_object())
            continue;
        VfxColorGradientKey key;
        key.time = item.value("time", 0.0f);

        if (item.contains("color")) {
            key.color = ReadParticleColor(item.at("color"));
        }

        key.easing = static_cast<VfxCurveEasing>(
            std::clamp(item.value("easing", 0), 0, 4));
        gradient.keys.push_back(key);
    }

    gradient.Normalize();
}

// VFXの距離別詳細度設定を保存JSONへ変換します。
json WriteLod(const VfxLodSettings& lod) {
    return {
        { "enabled", lod.enabled },
        { "nearDistance", lod.nearDistance },
        { "farDistance", lod.farDistance },
        { "farEmissionScale", lod.farEmissionScale },
        { "maxAliveParticles", lod.maxAliveParticles },
    };
}

// 保存JSONで指定された距離別詳細度設定を復元します。
void ReadLod(const json& source, VfxLodSettings& lod) {
    if (!source.is_object())
        return;
    lod.enabled = source.value("enabled", false);
    lod.nearDistance = source.value("nearDistance", 12.0f);
    lod.farDistance = source.value("farDistance", 45.0f);
    lod.farEmissionScale = source.value("farEmissionScale", 0.25f);
    lod.maxAliveParticles = source.value("maxAliveParticles", 0);
    lod.Sanitize();
}
}

// EmitterParams のシリアライズ
namespace engine {

void to_json(json& j, const engine::ParticleSystem::EmitterParams& p) {
    j = json{
        {"spawnArea", WriteParticleVector(p.spawnArea)},
        {"initialVelocity", WriteParticleVector(p.initialVelocity)},
        {"velocityRandomness", WriteParticleVector(p.velocityRandomness)},
        {"emitCount", p.emitCount},
        {"particlesPerSecond", p.particlesPerSecond},
        {"particleLifetime", p.particleLifetime},
        {"startColor", WriteParticleColor(p.startColor)},
        {"endColor", WriteParticleColor(p.endColor)},
        {"startSize", p.startSize},
        {"endSize", p.endSize},
        {"initialRotationSpeed", p.initialRotationSpeed},
    {"rotationSpeedRandomness", p.rotationSpeedRandomness},
    {"hdrIntensity", p.hdrIntensity},
    {"emitterType", (int)p.emitterType}, 
    {"spawnRadius", p.spawnRadius},      
    {"coneAngle", p.coneAngle},
    {"acceleration", WriteParticleVector(p.acceleration)},
    {"textureName", p.textureName},
        {"blendMode", (int)p.blendMode}

        // 必要に応じて他のパラメータも追加
    };

    json legacyCurve = json::array();

    for (float value : p.sizeCurve) {
        legacyCurve.push_back(value);
    }

    // 新しい編集カーブを無効にした場合も従来の十点カーブへ戻せるよう、両方の形式を保存します。
    j["legacySizeCurve"] = std::move(legacyCurve);
    j["authoring"] = {
        { "enabled", p.useAuthoringCurves },
        { "sizeOverLife", WriteFloatCurve(p.sizeOverLife) },
        { "colorOverLife", WriteColorGradient(p.colorOverLife) },
    };

    j["lod"] = WriteLod(p.lod);
}

void from_json(const json& j, engine::ParticleSystem::EmitterParams& p) {
    // 旧JSONで欠けている項目は既定値を維持し、存在する設定だけ取り込む。
    if (j.contains("textureName")) {
        p.textureName = j.at("textureName").get<std::string>();
    }

    if (j.contains("spawnArea"))
        p.spawnArea = ReadParticleVector(j.at("spawnArea"));
    if (j.contains("initialVelocity"))
        p.initialVelocity = ReadParticleVector(j.at("initialVelocity"));
    if (j.contains("velocityRandomness"))
        p.velocityRandomness = ReadParticleVector(j.at("velocityRandomness"));
    p.emitCount = std::clamp(j.value("emitCount", 1), 1, engine::ParticleSystem::GetMaxParticles());
    if (j.contains("particlesPerSecond"))
        j.at("particlesPerSecond").get_to(p.particlesPerSecond);
    if (j.contains("particleLifetime"))
        j.at("particleLifetime").get_to(p.particleLifetime);
    if (j.contains("startColor"))
        p.startColor = ReadParticleColor(j.at("startColor"));
    if (j.contains("endColor"))
        p.endColor = ReadParticleColor(j.at("endColor"));
    if (j.contains("startSize"))
        j.at("startSize").get_to(p.startSize);
    if (j.contains("endSize"))
        j.at("endSize").get_to(p.endSize);
    if(j.contains("initialRotationSpeed"))
        j.at("initialRotationSpeed").get_to(p.initialRotationSpeed);
    if (j.contains("hdrIntensity"))
        j.at("hdrIntensity").get_to(p.hdrIntensity);
    if (j.contains("rotationSpeedRandomness"))
        j.at("rotationSpeedRandomness").get_to(p.rotationSpeedRandomness);
    p.emitterType = (EmitterType)j.value("emitterType", (int)EmitterType::Box); 
    p.spawnRadius = j.value("spawnRadius", 1.0f); 
    p.coneAngle = j.value("coneAngle", 30.0f);
    if (j.contains("acceleration"))
        p.acceleration = ReadParticleVector(j.at("acceleration"));
    p.blendMode = (ParticleBlendMode)j.value("blendMode", (int)ParticleBlendMode::Alpha);

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

} // namespace engine

// ---------------------------------------------------------
// メイン処理
// ---------------------------------------------------------

void ParticleManager::SaveParam(const std::string& name, const engine::ParticleSystem::EmitterParams& param) {
    paramsMap_[name] = param; // メモリ上の辞書更新

    // ディレクトリ確認
    if (!std::filesystem::exists(kDirectoryPath)) {
        std::filesystem::create_directories(kDirectoryPath);
    }

    // ファイル書き出し
    const auto utf8Path = kDirectoryPath + name + ".json";
    const std::filesystem::path filePath(std::u8string(utf8Path.begin(), utf8Path.end()));
    WriteParamFile(filePath, param);
}

bool ParticleManager::WriteParamFile(const std::filesystem::path& path, const engine::ParticleSystem::EmitterParams& param) {
    std::ofstream file(path);
    file << std::setw(4) << json(param) << std::endl;
    file.close();
    return file.good();
}

void ParticleManager::LoadAllParams() {
    if (!std::filesystem::exists(kDirectoryPath))
        return;

    const std::filesystem::path root = std::filesystem::path(kDirectoryPath).parent_path();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto relative = entry.path().lexically_relative(root);
        // プリセットの識別名にルート以下のフォルダーも含め、別フォルダーにある同名ファイルを区別します。
        const auto key = relative.replace_extension().generic_u8string();
        engine::ParticleSystem::EmitterParams param;
        if (ReadParamFile(entry.path(), param)) paramsMap_[std::string(key.begin(), key.end())] = std::move(param);
    }
}

// 名前に対応するEmitter設定を読み、成功したものだけ登録済みパラメータへ反映します。
void ParticleManager::LoadParam(const std::string& name) {
    const auto utf8Path = kDirectoryPath + name + ".json";
    const std::filesystem::path filePath(std::u8string(utf8Path.begin(), utf8Path.end()));
    engine::ParticleSystem::EmitterParams param;
    if (ReadParamFile(filePath, param)) paramsMap_[name] = std::move(param);
}

bool ParticleManager::ReadParamFile(const std::filesystem::path& path, engine::ParticleSystem::EmitterParams& out) {
    std::ifstream file(path);
    if (!file) return false;
    json j;
    file >> j;
    // 読み込み中は一時構造体を使い、変換が完了した設定だけ呼び出し元へ移します。JSON解析・変換の例外は呼び出し元へ伝わります。
    engine::ParticleSystem::EmitterParams loaded;
    from_json(j, loaded);
    out = std::move(loaded);
    return true;
}

void ParticleManager::Emit(const std::string& name, const engine::Vector3& position, float& timer) {
    // 登録されていない名前なら何もしない
    const auto preset = VfxAssetPath::FindPreset(paramsMap_, name);
    if (preset == paramsMap_.end())
        return;

    // パラメータ取得
    const auto& params = preset->second;

    if (params.particlesPerSecond <= 0.0f) {
        return;
    }

    // 生成レート計算 (1秒間に何個出すか -> 1個出すのに何秒かかるか)
    float rate = 1.0f / params.particlesPerSecond;

    // タイマーを進める (60FPS固定と仮定。可変ならdeltaTimeを引数でもらうべき)
    timer += 1.0f / 60.0f;

    // particlesPerSecondは実粒子数/秒なので、この経路では1粒ずつ生成します。
    // emitCountはEditorの単発発生や明示的なEmitOneShot呼び出しだけに適用します。
    engine::ParticleSystem::EmitterParams singleParticleParams = params;
    singleParticleParams.emitCount = 1;

    // 生成タイミングになったらループで放出
    // 一回の更新で複数の発生間隔を跨いだ場合はその回数分だけ生成し、端数秒を次の更新へ残します。
    while (timer >= rate) {
        timer -= rate;
        particleSystem_->EmitOneShot(singleParticleParams, position);
    }
}
