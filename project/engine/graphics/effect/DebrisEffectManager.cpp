#include "DebrisEffectManager.h"

#include "BaseScene.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "LightManager.h"
#include "ModelManager.h"
#include "SceneManager.h"
#include "SrvManager.h"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace {
constexpr const char* kDebrisPresetDirectory = "Resources/json/debris/";
constexpr size_t kMaxActiveDebrisPieces = 96;
constexpr bool kEnableDebrisShadowWvp = true;

engine::Vector3 ReadVector3(const json& j, const char* key, const engine::Vector3& fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) {
        return fallback;
    }
    return { j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>() };
}

engine::Vector4 ReadVector4(const json& j, const char* key, const engine::Vector4& fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 4) {
        return fallback;
    }
    return { j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>() };
}

void WriteVector3(json& j, const char* key, const engine::Vector3& value) {
    j[key] = { value.x, value.y, value.z };
}

void WriteVector4(json& j, const char* key, const engine::Vector4& value) {
    j[key] = { value.x, value.y, value.z, value.w };
}

// 最大値がほぼゼロの場合は比率1として扱います。
float SafeRatio(float value, float maxValue) {
    if (maxValue <= 0.0001f) {
        return 1.0f;
    }
    return value / maxValue;
}
}

DebrisEffectManager* DebrisEffectManager::GetInstance() {
    // 破片プールとプリセットを Scene をまたいで共有する。
    static DebrisEffectManager instance;
    return &instance;
}

DebrisEffectManager::DebrisEffectManager()
    : randomEngine_(std::random_device{}()) {
}

void DebrisEffectManager::Initialize(Object3dCommon* common) {
    // 描画共通資源が切り替わったときは旧 GPU バッファを再利用せず作り直す。
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (common_ != common) {
        activePieces_.clear();
        pooledPieces_.clear();
        common_ = common;
        cameraResource_.Reset();
        cameraData_ = nullptr;
        if (common_ && common_->GetDxCommon()) {
            cameraResource_ = common_->GetDxCommon()->CreateBufferResource(sizeof(MeshRenderer::CameraForGPU));
            if (!cameraResource_) {
                DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[DebrisEffect] camera buffer creation failed.");
                return;
            }

            HRESULT hr = cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));
            if (FAILED(hr) || !cameraData_) {
                DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[DebrisEffect] camera buffer map failed.");
                cameraResource_.Reset();
                cameraData_ = nullptr;
                return;
            }
            cameraData_->worldPosition = { 0.0f, 0.0f, 0.0f };
        }
    }
}

Object3dCommon* DebrisEffectManager::ResolveCommon() {
    // 現在 Scene の描画資源へ追従し、前 Scene のリソース参照を残さない。
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    Object3dCommon* sceneCommon = scene ? scene->GetObject3dCommon() : nullptr;
    if (sceneCommon && sceneCommon != common_) {
        Initialize(sceneCommon);
    }
    return common_;
}

void DebrisEffectManager::Update(float deltaTime) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    deltaTime *= timeScale_;

    if (activePieces_.empty()) {
        return;
    }

    engine::Math math;
    engine::Matrix4x4 viewProjection = engine::Math::MakeIdentity4x4();
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (camera) {
        viewProjection = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
        if (cameraData_) {
            cameraData_->worldPosition = camera->GetEye();
        }
    }
    engine::Matrix4x4 lightViewProjection = LightManager::GetInstance()->GetDirectionalLight().lightViewProj;
    const engine::Matrix4x4* lightViewProjectionPtr = kEnableDebrisShadowWvp ? &lightViewProjection : nullptr;

    for (size_t index = 0; index < activePieces_.size();) {
        DebrisPiece& piece = activePieces_[index];
        piece.age += deltaTime;
        if (piece.age >= piece.lifetime) {
            // RecyclePieceは末尾の要素を現在位置へ移すため、ここではindexを進めない。
            RecyclePiece(index);
            continue;
        }

        float lifeRatio = SafeRatio(piece.age, piece.lifetime);
        bool transformDirty = false;
        if (!piece.sleeping) {
            piece.velocity.y -= piece.gravity * deltaTime;
            float drag = (std::max)(0.0f, 1.0f - piece.airDrag * deltaTime);
            piece.velocity.x *= drag;
            piece.velocity.y *= drag;
            piece.velocity.z *= drag;

            engine::Vector3 position = piece.position + piece.velocity * deltaTime;
            engine::Vector3 rotation = piece.rotation + piece.angularVelocity * deltaTime;

            if (piece.collideGround && position.y <= piece.groundY && piece.velocity.y < 0.0f) {
                position.y = piece.groundY;
                piece.velocity.y = -piece.velocity.y * piece.restitution;
                piece.velocity.x *= piece.friction;
                piece.velocity.z *= piece.friction;
                piece.angularVelocity = piece.angularVelocity * piece.friction;
                if (std::abs(piece.velocity.y) < 0.7f && engine::Math::Length(piece.velocity) < 2.0f) {
                    // 小さな跳ね返りをいつまでも計算しないよう、十分遅くなった破片を休止する。
                    piece.sleeping = true;
                }
            }

            piece.position = position;
            piece.rotation = rotation;
            transformDirty = true;
        }

        float scale = piece.baseScale;
        if (piece.shrinkOnFade && lifeRatio > piece.fadeStartRatio) {
            float fadeT = (lifeRatio - piece.fadeStartRatio) / (std::max)(0.001f, 1.0f - piece.fadeStartRatio);
            scale *= (std::max)(0.001f, 1.0f - fadeT);
        }
        if (std::abs(scale - piece.currentScale) > 0.0001f) {
            transformDirty = true;
        }
        piece.currentScale = scale;
        piece.matrixDirty = piece.matrixDirty || transformDirty;
        UpdatePieceMatrix(piece, viewProjection, lightViewProjectionPtr);
        piece.matrixDirty = false;
        ++index;
    }
}

void DebrisEffectManager::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // 破片ごとの WVP・材質を渡し、共通のカメラとライトでメッシュ描画する。
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (activePieces_.empty() || !common_ || !cameraResource_) {
        return;
    }

    common_->SetGraphicsCommand();
    common_->SetPipelineState(BlendMode::kNormal);
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    uint32_t shadowMapSrvHandle = common_->GetDxCommon()->GetShadowMapSrvHandle();

    for (auto& piece : activePieces_) {
        if (!piece.model || !piece.wvpResource || !piece.materialResource) {
            continue;
        }
        if (piece.shadowWvpResource) {
            commandList->SetGraphicsRootConstantBufferView(11, piece.shadowWvpResource->GetGPUVirtualAddress());
        }
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 12, shadowMapSrvHandle);
        piece.model->Draw(
            piece.wvpResource.Get(),
            LightManager::GetInstance()->GetDirectionalLightResource(),
            cameraResource_.Get(),
            pointLightResource,
            spotLightResource,
            piece.materialResource.Get(),
            0,
            0,
            0
        );
    }
}

void DebrisEffectManager::Clear() {
    // GPU リソースを破棄せずプールへ戻し、次の発生で再利用できるようにする。
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    while (!activePieces_.empty()) {
        RecyclePiece(activePieces_.size() - 1);
    }
}

void DebrisEffectManager::ResetEditorPreview(uint32_t randomSeed) {
    // 編集中の再生を同じ乱数列で始め直し、時刻シーク時の見た目を再現する。
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Clear();
    randomEngine_.seed(randomSeed);
}

int DebrisEffectManager::GetActivePieceCount() const {
    // 更新・描画と同じロックの下で個数を読み、編集 UI への値を安定させる。
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return static_cast<int>(activePieces_.size());
}

void DebrisEffectManager::LoadAllPresets(const std::string& directoryPath) {
    // 指定フォルダ直下の JSON を名前で登録し、実ファイルの無い候補は飛ばす。
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    namespace fs = std::filesystem;
    if (!fs::exists(directoryPath)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        DebrisEffectConfig config;
        if (LoadConfig(entry.path().generic_string(), config)) {
            presets_[entry.path().stem().string()] = config;
        }
    }
}

bool DebrisEffectManager::LoadConfig(const std::string& filePath, DebrisEffectConfig& outConfig) const {
    // 欠けた項目は Config の初期値を使い、古い JSON も読めるようにする。
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    json j;
    file >> j;

    DebrisEffectConfig config;
    if (j.contains("name")) config.name = j["name"].get<std::string>();
    if (j.contains("modelNames") && j["modelNames"].is_array()) {
        config.modelNames.clear();
        for (const auto& model : j["modelNames"]) {
            if (model.is_string() && !model.get<std::string>().empty()) {
                config.modelNames.push_back(model.get<std::string>());
            }
        }
    }
    // モデル配列が空でも、発生処理へ空集合を渡さない。
    if (config.modelNames.empty()) {
        config.modelNames.push_back("Primitives/cube");
    }
    if (j.contains("spawnCount")) config.spawnCount = j["spawnCount"].get<int>();
    config.spawnOffset = ReadVector3(j, "spawnOffset", config.spawnOffset);
    config.spawnBoxHalfExtents = ReadVector3(j, "spawnBoxHalfExtents", config.spawnBoxHalfExtents);
    config.baseDirection = ReadVector3(j, "baseDirection", config.baseDirection);
    if (j.contains("horizontalSpread")) config.horizontalSpread = j["horizontalSpread"].get<float>();
    if (j.contains("verticalMin")) config.verticalMin = j["verticalMin"].get<float>();
    if (j.contains("verticalMax")) config.verticalMax = j["verticalMax"].get<float>();
    if (j.contains("speedMin")) config.speedMin = j["speedMin"].get<float>();
    if (j.contains("speedMax")) config.speedMax = j["speedMax"].get<float>();
    if (j.contains("angularSpeedMin")) config.angularSpeedMin = j["angularSpeedMin"].get<float>();
    if (j.contains("angularSpeedMax")) config.angularSpeedMax = j["angularSpeedMax"].get<float>();
    if (j.contains("scaleMin")) config.scaleMin = j["scaleMin"].get<float>();
    if (j.contains("scaleMax")) config.scaleMax = j["scaleMax"].get<float>();
    if (j.contains("lifetimeMin")) config.lifetimeMin = j["lifetimeMin"].get<float>();
    if (j.contains("lifetimeMax")) config.lifetimeMax = j["lifetimeMax"].get<float>();
    if (j.contains("gravity")) config.gravity = j["gravity"].get<float>();
    if (j.contains("airDrag")) config.airDrag = j["airDrag"].get<float>();
    if (j.contains("restitution")) config.restitution = j["restitution"].get<float>();
    if (j.contains("friction")) config.friction = j["friction"].get<float>();
    if (j.contains("groundY")) config.groundY = j["groundY"].get<float>();
    if (j.contains("fadeStartRatio")) config.fadeStartRatio = j["fadeStartRatio"].get<float>();
    config.color = ReadVector4(j, "color", config.color);
    if (j.contains("materialType")) config.materialType = j["materialType"].get<int>();
    if (j.contains("roughness")) config.roughness = j["roughness"].get<float>();
    if (j.contains("metallic")) config.metallic = j["metallic"].get<float>();
    if (j.contains("enableEnvMap")) config.enableEnvMap = j["enableEnvMap"].get<bool>();
    if (j.contains("envIntensity")) config.envIntensity = j["envIntensity"].get<float>();
    if (j.contains("emissive")) config.emissive = j["emissive"].get<float>();
    if (j.contains("collideGround")) config.collideGround = j["collideGround"].get<bool>();
    if (j.contains("shrinkOnFade")) config.shrinkOnFade = j["shrinkOnFade"].get<bool>();

    outConfig = config;
    return true;
}

bool DebrisEffectManager::SaveConfig(const std::string& filePath, const DebrisEffectConfig& config) const {
    // 編集した発生範囲・物理・材質を同じ JSON キーへ戻す。
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(filePath).parent_path());

    json j;
    j["name"] = config.name;
    j["modelNames"] = config.modelNames;
    j["spawnCount"] = config.spawnCount;
    WriteVector3(j, "spawnOffset", config.spawnOffset);
    WriteVector3(j, "spawnBoxHalfExtents", config.spawnBoxHalfExtents);
    WriteVector3(j, "baseDirection", config.baseDirection);
    j["horizontalSpread"] = config.horizontalSpread;
    j["verticalMin"] = config.verticalMin;
    j["verticalMax"] = config.verticalMax;
    j["speedMin"] = config.speedMin;
    j["speedMax"] = config.speedMax;
    j["angularSpeedMin"] = config.angularSpeedMin;
    j["angularSpeedMax"] = config.angularSpeedMax;
    j["scaleMin"] = config.scaleMin;
    j["scaleMax"] = config.scaleMax;
    j["lifetimeMin"] = config.lifetimeMin;
    j["lifetimeMax"] = config.lifetimeMax;
    j["gravity"] = config.gravity;
    j["airDrag"] = config.airDrag;
    j["restitution"] = config.restitution;
    j["friction"] = config.friction;
    j["groundY"] = config.groundY;
    j["fadeStartRatio"] = config.fadeStartRatio;
    WriteVector4(j, "color", config.color);
    j["materialType"] = config.materialType;
    j["roughness"] = config.roughness;
    j["metallic"] = config.metallic;
    j["enableEnvMap"] = config.enableEnvMap;
    j["envIntensity"] = config.envIntensity;
    j["emissive"] = config.emissive;
    j["collideGround"] = config.collideGround;
    j["shrinkOnFade"] = config.shrinkOnFade;

    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }
    file << j.dump(4);
    return true;
}

void DebrisEffectManager::RegisterPreset(const std::string& presetName, const DebrisEffectConfig& config) {
    // 編集中の設定を名前で上書きし、以後の Spawn へすぐ反映する。
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    presets_[presetName] = config;
}

void DebrisEffectManager::PrewarmPreset(const std::string& presetName) {
    // 発生前にモデルと破片用定数バッファを用意し、初回フレームの遅延を抑える。
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    DebrisEffectConfig config;
    auto it = presets_.find(presetName);
    if (it != presets_.end()) {
        config = it->second;
    }
    else if (LoadConfig(ResolvePresetPath(presetName), config)) {
        presets_[presetName] = config;
    }
    else {
        DebugConsole::GetInstance()->AddLog(LogLevel::Warning, "[DebrisEffect] prewarm preset not found: " + presetName);
        return;
    }

    Object3dCommon* common = ResolveCommon();
    if (!common) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Warning, "[DebrisEffect] prewarm skipped without Object3dCommon: " + presetName);
        return;
    }

    std::vector<std::string> modelNames;
    modelNames.reserve(config.modelNames.size());
    for (const std::string& modelName : config.modelNames) {
        if (modelName.empty()) {
            continue;
        }
        if (std::find(modelNames.begin(), modelNames.end(), modelName) != modelNames.end()) {
            continue;
        }
        ModelManager::GetInstance()->LoadModel(modelName);
        modelNames.push_back(modelName);
    }
    if (modelNames.empty()) {
        ModelManager::GetInstance()->LoadModel("Primitives/cube");
    }

    // 発生数を上限で制限し、足りない分だけプールを増やす。
    int count = std::clamp(config.spawnCount, 1, static_cast<int>(kMaxActiveDebrisPieces));
    while (pooledPieces_.size() < static_cast<size_t>(count)) {
        DebrisPiece piece;
        if (!InitializePieceResources(piece, config)) {
            break;
        }
        pooledPieces_.push_back(std::move(piece));
    }
}

void DebrisEffectManager::PrewarmPresets(const std::vector<std::string>& presetNames) {
    // 同じプールを複数プリセットから共有し、順に必要量へ拡張する。
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    for (const std::string& presetName : presetNames) {
        PrewarmPreset(presetName);
    }
}

void DebrisEffectManager::Spawn(const std::string& presetName, const engine::Vector3& position) {
    // 登録済み設定を優先し、無いときだけ JSON を読んでキャッシュする。
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = presets_.find(presetName);
    if (it != presets_.end()) {
        SpawnFromConfig(it->second, position);
        return;
    }

    DebrisEffectConfig config;
    if (LoadConfig(ResolvePresetPath(presetName), config)) {
        presets_[presetName] = config;
        SpawnFromConfig(config, position);
    }
    else {
        DebugConsole::GetInstance()->AddLog(LogLevel::Warning, "[DebrisEffect] preset not found: " + presetName);
    }
}

void DebrisEffectManager::SpawnOnGround(const std::string& presetName, const engine::Vector3& position, float groundY) {
    // プリセット本体は変更せず、今回の発生に限って衝突面の高さを差し替える。
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto it = presets_.find(presetName);
    if (it != presets_.end()) {
        DebrisEffectConfig config = it->second;
        config.groundY = groundY;
        SpawnFromConfig(config, position);
        return;
    }

    DebrisEffectConfig config;
    if (LoadConfig(ResolvePresetPath(presetName), config)) {
        presets_[presetName] = config;
        config.groundY = groundY;
        SpawnFromConfig(config, position);
    }
    else {
        DebugConsole::GetInstance()->AddLog(LogLevel::Warning, "[DebrisEffect] preset not found: " + presetName);
    }
}

void DebrisEffectManager::SpawnFromConfig(const DebrisEffectConfig& config, const engine::Vector3& position) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    Object3dCommon* common = ResolveCommon();
    if (!common) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[DebrisEffect] Object3dCommon is null.");
        return;
    }

    std::vector<std::string> modelNames;
    modelNames.reserve(config.modelNames.size());
    for (const std::string& modelName : config.modelNames) {
        if (modelName.empty()) {
            continue;
        }
        if (std::find(modelNames.begin(), modelNames.end(), modelName) != modelNames.end()) {
            continue;
        }
        ModelManager::GetInstance()->LoadModel(modelName);
        modelNames.push_back(modelName);
    }
    if (modelNames.empty()) {
        modelNames.push_back("Primitives/cube");
        ModelManager::GetInstance()->LoadModel(modelNames.front());
    }

    int count = std::clamp(config.spawnCount, 1, 256);
    if (static_cast<size_t>(count) > kMaxActiveDebrisPieces) {
        count = static_cast<int>(kMaxActiveDebrisPieces);
    }

    size_t requiredRoom = activePieces_.size() + static_cast<size_t>(count);
    // 同時表示数の上限を守るため、既存の破片を再利用して新しい破片の枠を確保する。
    while (requiredRoom > kMaxActiveDebrisPieces && !activePieces_.empty()) {
        RecyclePiece(0);
        --requiredRoom;
    }
    activePieces_.reserve(activePieces_.size() + static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::string& modelName = modelNames[RandomIndex(static_cast<int>(modelNames.size()))];
        Model* model = ModelManager::GetInstance()->LoadModel(modelName);
        if (!model) {
            continue;
        }

        DebrisPiece piece = AcquirePiece();
        piece.model = model;
        piece.age = 0.0f;
        piece.sleeping = false;
        piece.matrixDirty = true;
        piece.position = position + config.spawnOffset + engine::Vector3{
            RandomRange(-std::abs(config.spawnBoxHalfExtents.x), std::abs(config.spawnBoxHalfExtents.x)),
            RandomRange(-std::abs(config.spawnBoxHalfExtents.y), std::abs(config.spawnBoxHalfExtents.y)),
            RandomRange(-std::abs(config.spawnBoxHalfExtents.z), std::abs(config.spawnBoxHalfExtents.z))
        };
        piece.rotation = {
            RandomRange(-3.141592f, 3.141592f),
            RandomRange(-3.141592f, 3.141592f),
            RandomRange(-3.141592f, 3.141592f)
        };

        float scale = RandomRange(config.scaleMin, config.scaleMax);
        piece.currentScale = scale;
        piece.velocity = RandomDirection(config) * RandomRange(config.speedMin, config.speedMax);
        piece.angularVelocity = {
            RandomRange(config.angularSpeedMin, config.angularSpeedMax) * RandomRange(-1.0f, 1.0f),
            RandomRange(config.angularSpeedMin, config.angularSpeedMax) * RandomRange(-1.0f, 1.0f),
            RandomRange(config.angularSpeedMin, config.angularSpeedMax) * RandomRange(-1.0f, 1.0f)
        };
        piece.lifetime = RandomRange(config.lifetimeMin, config.lifetimeMax);
        piece.baseScale = scale;
        piece.gravity = config.gravity;
        piece.airDrag = config.airDrag;
        piece.restitution = config.restitution;
        piece.friction = config.friction;
        piece.groundY = config.groundY;
        piece.fadeStartRatio = config.fadeStartRatio;
        piece.collideGround = config.collideGround;
        piece.shrinkOnFade = config.shrinkOnFade;
        if (!InitializePieceResources(piece, config)) {
            continue;
        }

        Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
        engine::Matrix4x4 viewProjection = engine::Math::MakeIdentity4x4();
        if (camera) {
            viewProjection = engine::Math::Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
        }
        engine::Matrix4x4 lightViewProjection = LightManager::GetInstance()->GetDirectionalLight().lightViewProj;
        UpdatePieceMatrix(piece, viewProjection, kEnableDebrisShadowWvp ? &lightViewProjection : nullptr);
        piece.matrixDirty = false;
        activePieces_.push_back(std::move(piece));
    }
}

DebrisEffectManager::DebrisPiece DebrisEffectManager::AcquirePiece() {
    // プールに残る GPU リソースを移動して取得し、空なら新規初期化へ任せる。
    if (pooledPieces_.empty()) {
        return {};
    }
    DebrisPiece piece = std::move(pooledPieces_.back());
    pooledPieces_.pop_back();
    return piece;
}

void DebrisEffectManager::RecyclePiece(size_t index) {
    if (index >= activePieces_.size()) {
        return;
    }

    activePieces_[index].model = nullptr;
    if (pooledPieces_.size() < kMaxActiveDebrisPieces) {
        pooledPieces_.push_back(std::move(activePieces_[index]));
    }

    size_t lastIndex = activePieces_.size() - 1;
    if (index != lastIndex) {
        // 順序を維持する必要がないため、末尾を詰めて削除コストを一定にする。
        activePieces_[index] = std::move(activePieces_[lastIndex]);
    }
    activePieces_.pop_back();
}

bool DebrisEffectManager::InitializePieceResources(DebrisPiece& piece, const DebrisEffectConfig& config) {
    // プールから戻った破片のバッファは再利用し、不足したバッファだけ生成する。
    if (!common_ || !common_->GetDxCommon()) {
        return false;
    }

    DirectXCommon* dxCommon = common_->GetDxCommon();
    if (!piece.wvpResource) {
        piece.wvpResource = dxCommon->CreateBufferResource(sizeof(MeshRenderer::TransformationMatrix));
        if (!piece.wvpResource) {
            return false;
        }
        HRESULT hr = piece.wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&piece.wvpData));
        if (FAILED(hr) || !piece.wvpData) {
            piece.wvpResource.Reset();
            piece.wvpData = nullptr;
            return false;
        }
    }
    if (kEnableDebrisShadowWvp && !piece.shadowWvpResource) {
        piece.shadowWvpResource = dxCommon->CreateBufferResource(sizeof(MeshRenderer::TransformationMatrix));
        if (!piece.shadowWvpResource) {
            return false;
        }
        HRESULT hr = piece.shadowWvpResource->Map(0, nullptr, reinterpret_cast<void**>(&piece.shadowWvpData));
        if (FAILED(hr) || !piece.shadowWvpData) {
            piece.shadowWvpResource.Reset();
            piece.shadowWvpData = nullptr;
            return false;
        }
    }
    if (!piece.materialResource) {
        piece.materialResource = dxCommon->CreateBufferResource(sizeof(MeshRenderer::MaterialData));
        if (!piece.materialResource) {
            return false;
        }
        HRESULT hr = piece.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&piece.materialData));
        if (FAILED(hr) || !piece.materialData) {
            piece.materialResource.Reset();
            piece.materialData = nullptr;
            return false;
        }
    }
    if (!piece.wvpResource || !piece.materialResource) {
        return false;
    }
    if (!piece.wvpData || !piece.materialData) {
        return false;
    }

    piece.wvpData->WVP = engine::Math::MakeIdentity4x4();
    piece.wvpData->world = engine::Math::MakeIdentity4x4();
    piece.wvpData->WorldInverseTranspose = engine::Math::MakeIdentity4x4();
    if (piece.shadowWvpData) {
        piece.shadowWvpData->WVP = engine::Math::MakeIdentity4x4();
        piece.shadowWvpData->world = engine::Math::MakeIdentity4x4();
        piece.shadowWvpData->WorldInverseTranspose = engine::Math::MakeIdentity4x4();
    }

    // 以前の材質値が混ざらないよう、今回のプリセット値で全体を初期化する。
    std::memset(piece.materialData, 0, sizeof(MeshRenderer::MaterialData));
    piece.materialData->color = config.color;
    piece.materialData->enableLighting = 1;
    piece.materialData->uvTransform = engine::Math::MakeIdentity4x4();
    piece.materialData->selectedLighting = 2;
    piece.materialData->shininess = 20.0f;
    piece.materialData->materialType = config.materialType;
    piece.materialData->roughness = std::clamp(config.roughness, 0.0f, 1.0f);
    piece.materialData->metallic = std::clamp(config.metallic, 0.0f, 1.0f);
    piece.materialData->enableNormalMap = 0;
    piece.materialData->enableEnvMap = config.enableEnvMap ? 1 : 0;
    piece.materialData->envIntensity = (std::max)(0.0f, config.envIntensity);
    piece.materialData->emissive = config.emissive;
    return true;
}

void DebrisEffectManager::UpdatePieceMatrix(DebrisPiece& piece, const engine::Matrix4x4& viewProjection, const engine::Matrix4x4* lightViewProjection) {
    // 通常描画と影描画へ同じ world 行列を渡し、破片の向きを一致させる。
    if (!piece.wvpData) {
        return;
    }

    engine::Vector3 scale = { piece.currentScale, piece.currentScale, piece.currentScale };
    engine::Matrix4x4 world = engine::Math::MakeAffineMatrix(scale, piece.rotation, piece.position);
    piece.wvpData->WVP = engine::Math::Multiply(world, viewProjection);
    piece.wvpData->world = world;
    piece.wvpData->WorldInverseTranspose = engine::Math::Transpose(engine::Math::Inverse(world));
    if (piece.materialData) {
        piece.materialData->time = piece.age;
    }

    if (piece.shadowWvpData && lightViewProjection) {
        piece.shadowWvpData->WVP = engine::Math::Multiply(world, *lightViewProjection);
        piece.shadowWvpData->world = world;
        piece.shadowWvpData->WorldInverseTranspose = piece.wvpData->WorldInverseTranspose;
    }
}

float DebrisEffectManager::RandomRange(float minValue, float maxValue) {
    // JSON で上下限が逆でも同じ範囲を使えるよう、分布生成前に並べ替える。
    if (minValue > maxValue) {
        std::swap(minValue, maxValue);
    }
    std::uniform_real_distribution<float> distribution(minValue, maxValue);
    return distribution(randomEngine_);
}

int DebrisEffectManager::RandomIndex(int maxExclusive) {
    // 候補が一つ以下なら乱数を消費せず、配列外参照を防ぐ。
    if (maxExclusive <= 1) {
        return 0;
    }
    std::uniform_int_distribution<int> distribution(0, maxExclusive - 1);
    return distribution(randomEngine_);
}

engine::Vector3 DebrisEffectManager::RandomDirection(const DebrisEffectConfig& config) {
    // 基準方向へ水平拡散と垂直範囲を加え、最後に単位方向へ正規化する。
    engine::Vector3 base = engine::Math::Normalize(config.baseDirection);
    if (engine::Math::Length(base) < 0.001f) {
        base = { 0.0f, 0.0f, 1.0f };
    }

    float angle = RandomRange(-3.141592f, 3.141592f);
    float spread = (std::max)(0.0f, config.horizontalSpread);
    engine::Vector3 randomHorizontal = { std::cos(angle) * spread, 0.0f, std::sin(angle) * spread };
    engine::Vector3 direction = {
        base.x + randomHorizontal.x,
        RandomRange(config.verticalMin, config.verticalMax),
        base.z + randomHorizontal.z
    };
    return engine::Math::Normalize(direction);
}

std::string DebrisEffectManager::ResolvePresetPath(const std::string& presetName) const {
    // パス指定はそのまま使い、短い名前だけ標準プリセットディレクトリへ解決する。
    if (presetName.find('/') != std::string::npos || presetName.find('\\') != std::string::npos) {
        return presetName;
    }
    return std::string(kDebrisPresetDirectory) + presetName + ".json";
}
