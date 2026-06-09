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

Vector3 ReadVector3(const json& j, const char* key, const Vector3& fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3) {
        return fallback;
    }
    return { j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>() };
}

Vector4 ReadVector4(const json& j, const char* key, const Vector4& fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 4) {
        return fallback;
    }
    return { j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>(), j[key][3].get<float>() };
}

void WriteVector3(json& j, const char* key, const Vector3& value) {
    j[key] = { value.x, value.y, value.z };
}

void WriteVector4(json& j, const char* key, const Vector4& value) {
    j[key] = { value.x, value.y, value.z, value.w };
}

float SafeRatio(float value, float maxValue) {
    if (maxValue <= 0.0001f) {
        return 1.0f;
    }
    return value / maxValue;
}
}

DebrisEffectManager* DebrisEffectManager::GetInstance() {
    static DebrisEffectManager instance;
    return &instance;
}

DebrisEffectManager::DebrisEffectManager()
    : randomEngine_(std::random_device{}()) {
}

void DebrisEffectManager::Initialize(Object3dCommon* common) {
    if (common_ != common) {
        activePieces_.clear();
        pooledPieces_.clear();
        common_ = common;
        cameraResource_.Reset();
        cameraData_ = nullptr;
        if (common_ && common_->GetDxCommon()) {
            cameraResource_ = common_->GetDxCommon()->CreateBufferResource(sizeof(MeshRenderer::CameraForGPU));
            cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));
            cameraData_->worldPosition = { 0.0f, 0.0f, 0.0f };
        }
    }
}

Object3dCommon* DebrisEffectManager::ResolveCommon() {
    SceneManager* sceneManager = SceneManager::GetInstance();
    BaseScene* scene = sceneManager ? sceneManager->GetCurrentScene() : nullptr;
    Object3dCommon* sceneCommon = scene ? scene->GetObject3dCommon() : nullptr;
    if (sceneCommon && sceneCommon != common_) {
        Initialize(sceneCommon);
    }
    return common_;
}

void DebrisEffectManager::Update(float deltaTime) {
    if (activePieces_.empty()) {
        return;
    }

    Math math;
    Matrix4x4 viewProjection = Math::MakeIdentity4x4();
    Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (camera) {
        viewProjection = math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
        if (cameraData_) {
            cameraData_->worldPosition = camera->GetEye();
        }
    }
    Matrix4x4 lightViewProjection = LightManager::GetInstance()->GetDirectionalLight().lightViewProj;

    for (size_t index = 0; index < activePieces_.size();) {
        DebrisPiece& piece = activePieces_[index];
        piece.age += deltaTime;
        if (piece.age >= piece.lifetime) {
            RecyclePiece(index);
            continue;
        }

        float lifeRatio = SafeRatio(piece.age, piece.lifetime);
        if (!piece.sleeping) {
            piece.velocity.y -= piece.gravity * deltaTime;
            float drag = (std::max)(0.0f, 1.0f - piece.airDrag * deltaTime);
            piece.velocity.x *= drag;
            piece.velocity.y *= drag;
            piece.velocity.z *= drag;

            Vector3 position = piece.position + piece.velocity * deltaTime;
            Vector3 rotation = piece.rotation + piece.angularVelocity * deltaTime;

            if (piece.collideGround && position.y <= piece.groundY && piece.velocity.y < 0.0f) {
                position.y = piece.groundY;
                piece.velocity.y = -piece.velocity.y * piece.restitution;
                piece.velocity.x *= piece.friction;
                piece.velocity.z *= piece.friction;
                piece.angularVelocity = piece.angularVelocity * piece.friction;
                if (std::abs(piece.velocity.y) < 0.7f && Math::Length(piece.velocity) < 2.0f) {
                    piece.sleeping = true;
                }
            }

            piece.position = position;
            piece.rotation = rotation;
        }

        float scale = piece.baseScale;
        if (piece.shrinkOnFade && lifeRatio > piece.fadeStartRatio) {
            float fadeT = (lifeRatio - piece.fadeStartRatio) / (std::max)(0.001f, 1.0f - piece.fadeStartRatio);
            scale *= (std::max)(0.001f, 1.0f - fadeT);
        }
        piece.currentScale = scale;
        UpdatePieceMatrix(piece, viewProjection, lightViewProjection);
        ++index;
    }
}

void DebrisEffectManager::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
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
    while (!activePieces_.empty()) {
        RecyclePiece(activePieces_.size() - 1);
    }
}

void DebrisEffectManager::LoadAllPresets(const std::string& directoryPath) {
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
    if (config.modelNames.empty()) {
        config.modelNames.push_back("Primitives/cube");
    }
    if (j.contains("spawnCount")) config.spawnCount = j["spawnCount"].get<int>();
    config.spawnOffset = ReadVector3(j, "spawnOffset", config.spawnOffset);
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
    if (j.contains("emissive")) config.emissive = j["emissive"].get<float>();
    if (j.contains("collideGround")) config.collideGround = j["collideGround"].get<bool>();
    if (j.contains("shrinkOnFade")) config.shrinkOnFade = j["shrinkOnFade"].get<bool>();

    outConfig = config;
    return true;
}

bool DebrisEffectManager::SaveConfig(const std::string& filePath, const DebrisEffectConfig& config) const {
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(filePath).parent_path());

    json j;
    j["name"] = config.name;
    j["modelNames"] = config.modelNames;
    j["spawnCount"] = config.spawnCount;
    WriteVector3(j, "spawnOffset", config.spawnOffset);
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
    presets_[presetName] = config;
}

void DebrisEffectManager::Spawn(const std::string& presetName, const Vector3& position) {
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

void DebrisEffectManager::SpawnFromConfig(const DebrisEffectConfig& config, const Vector3& position) {
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
        piece.position = position + config.spawnOffset;
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
        Matrix4x4 viewProjection = Math::MakeIdentity4x4();
        if (camera) {
            viewProjection = Math::Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
        }
        UpdatePieceMatrix(piece, viewProjection, LightManager::GetInstance()->GetDirectionalLight().lightViewProj);
        activePieces_.push_back(std::move(piece));
    }
}

DebrisEffectManager::DebrisPiece DebrisEffectManager::AcquirePiece() {
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
        activePieces_[index] = std::move(activePieces_[lastIndex]);
    }
    activePieces_.pop_back();
}

bool DebrisEffectManager::InitializePieceResources(DebrisPiece& piece, const DebrisEffectConfig& config) {
    if (!common_ || !common_->GetDxCommon()) {
        return false;
    }

    DirectXCommon* dxCommon = common_->GetDxCommon();
    if (!piece.wvpResource) {
        piece.wvpResource = dxCommon->CreateBufferResource(sizeof(MeshRenderer::TransformationMatrix));
        piece.wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&piece.wvpData));
    }
    if (!piece.shadowWvpResource) {
        piece.shadowWvpResource = dxCommon->CreateBufferResource(sizeof(MeshRenderer::TransformationMatrix));
        piece.shadowWvpResource->Map(0, nullptr, reinterpret_cast<void**>(&piece.shadowWvpData));
    }
    if (!piece.materialResource) {
        piece.materialResource = dxCommon->CreateBufferResource(sizeof(MeshRenderer::MaterialData));
        piece.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&piece.materialData));
    }
    if (!piece.wvpResource || !piece.shadowWvpResource || !piece.materialResource) {
        return false;
    }
    if (!piece.wvpData || !piece.shadowWvpData || !piece.materialData) {
        return false;
    }

    piece.wvpData->WVP = Math::MakeIdentity4x4();
    piece.wvpData->world = Math::MakeIdentity4x4();
    piece.wvpData->WorldInverseTranspose = Math::MakeIdentity4x4();
    piece.shadowWvpData->WVP = Math::MakeIdentity4x4();
    piece.shadowWvpData->world = Math::MakeIdentity4x4();
    piece.shadowWvpData->WorldInverseTranspose = Math::MakeIdentity4x4();

    std::memset(piece.materialData, 0, sizeof(MeshRenderer::MaterialData));
    piece.materialData->color = config.color;
    piece.materialData->enableLighting = 1;
    piece.materialData->uvTransform = Math::MakeIdentity4x4();
    piece.materialData->selectedLighting = 2;
    piece.materialData->shininess = 20.0f;
    piece.materialData->materialType = config.materialType;
    piece.materialData->roughness = 0.5f;
    piece.materialData->metallic = 0.0f;
    piece.materialData->enableNormalMap = 0;
    piece.materialData->enableEnvMap = 0;
    piece.materialData->envIntensity = 1.0f;
    piece.materialData->emissive = config.emissive;
    return true;
}

void DebrisEffectManager::UpdatePieceMatrix(DebrisPiece& piece, const Matrix4x4& viewProjection, const Matrix4x4& lightViewProjection) {
    if (!piece.wvpData || !piece.shadowWvpData) {
        return;
    }

    Vector3 scale = { piece.currentScale, piece.currentScale, piece.currentScale };
    Matrix4x4 world = Math::MakeAffineMatrix(scale, piece.rotation, piece.position);
    piece.wvpData->WVP = Math::Multiply(world, viewProjection);
    piece.wvpData->world = world;
    piece.wvpData->WorldInverseTranspose = Math::Transpose(Math::Inverse(world));

    piece.shadowWvpData->WVP = Math::Multiply(world, lightViewProjection);
    piece.shadowWvpData->world = world;
    piece.shadowWvpData->WorldInverseTranspose = piece.wvpData->WorldInverseTranspose;
}

float DebrisEffectManager::RandomRange(float minValue, float maxValue) {
    if (minValue > maxValue) {
        std::swap(minValue, maxValue);
    }
    std::uniform_real_distribution<float> distribution(minValue, maxValue);
    return distribution(randomEngine_);
}

int DebrisEffectManager::RandomIndex(int maxExclusive) {
    if (maxExclusive <= 1) {
        return 0;
    }
    std::uniform_int_distribution<int> distribution(0, maxExclusive - 1);
    return distribution(randomEngine_);
}

Vector3 DebrisEffectManager::RandomDirection(const DebrisEffectConfig& config) {
    Vector3 base = Math::Normalize(config.baseDirection);
    if (Math::Length(base) < 0.001f) {
        base = { 0.0f, 0.0f, 1.0f };
    }

    float angle = RandomRange(-3.141592f, 3.141592f);
    float spread = (std::max)(0.0f, config.horizontalSpread);
    Vector3 randomHorizontal = { std::cos(angle) * spread, 0.0f, std::sin(angle) * spread };
    Vector3 direction = {
        base.x + randomHorizontal.x,
        RandomRange(config.verticalMin, config.verticalMax),
        base.z + randomHorizontal.z
    };
    return Math::Normalize(direction);
}

std::string DebrisEffectManager::ResolvePresetPath(const std::string& presetName) const {
    if (presetName.find('/') != std::string::npos || presetName.find('\\') != std::string::npos) {
        return presetName;
    }
    return std::string(kDebrisPresetDirectory) + presetName + ".json";
}
