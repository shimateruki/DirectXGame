#include "GPUParticleManager.h"
#include <filesystem>
#include <fstream>
#include "json.hpp"
#include "DebugConsole.h"
#include "SRVManager.h"
#include <TextureManager.h>
#include "CameraManager.h"
#include <d3d12.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
std::string MakeParticleSystemKey(const GPUParticleConfig& config) {
    std::ostringstream key;
    key << config.texturePath
        << "_blend" << config.blendModeIndex
        << "_sheet" << config.spriteSheetColumns
        << "x" << config.spriteSheetRows
        << "_frames" << config.spriteSheetFrameCount
        << "_fps" << config.spriteSheetFps
        << "_loop" << config.spriteSheetLoop
        << "_rand" << config.spriteSheetRandomStart
        << "_align" << config.alignToVelocity
        << "_stretch" << config.velocityStretch
        << "_type" << config.particleType
        << "_trail" << config.trailLength
        << "_light" << config.receiveLighting
        << "_lightStrength" << config.lightingStrength
        << "_lightDir" << config.lightDirection.x << ',' << config.lightDirection.y << ',' << config.lightDirection.z
        << "_lightColor" << config.lightColor.x << ',' << config.lightColor.y << ',' << config.lightColor.z
        << "_capacity" << GPUParticleManager::ResolveParticleCapacity(config);
    return key.str();
}
}

GPUParticleManager* GPUParticleManager::GetInstance() {
    static GPUParticleManager instance;
    return &instance;
}

uint32_t GPUParticleManager::ResolveParticleCapacity(const GPUParticleConfig& config) {
    const auto applyLodLimit = [&config](uint32_t capacity) {
        if (!config.lod.enabled || config.lod.maxAliveParticles <= 0) {
            return capacity;
        }
        const uint32_t lodCapacity = static_cast<uint32_t>(std::clamp(
            config.lod.maxAliveParticles,
            static_cast<int>(GPUParticleSystem::kMinParticles),
            static_cast<int>(GPUParticleSystem::kMaxParticles)));
        return (std::min)(capacity, lodCapacity);
    };

    if (config.maxParticles > 0) {
        return applyLodLimit(static_cast<uint32_t>(std::clamp(
            config.maxParticles,
            static_cast<int>(GPUParticleSystem::kMinParticles),
            static_cast<int>(GPUParticleSystem::kMaxParticles))));
    }

    const uint64_t emitCount = static_cast<uint64_t>((std::max)(config.emitCount, 1));
    uint64_t requiredCapacity = 0;
    if (config.isLooping) {
        const float interval = (std::max)(config.emitInterval, 1.0f / 60.0f);
        const float life = (std::max)(config.emitLife, 0.0f);
        const uint64_t overlappingEmits = static_cast<uint64_t>(std::ceil(life / interval)) + 2u;
        requiredCapacity = emitCount * overlappingEmits;
    }
    else {
        // 同じTexture/Blendの単発演出が同時に6回重なっても溢れにくい余裕を持たせる。
        requiredCapacity = emitCount * 6u;
    }

    requiredCapacity = (std::max)(
        requiredCapacity,
        static_cast<uint64_t>(GPUParticleSystem::kMinParticles));
    if (requiredCapacity >= GPUParticleSystem::kMaxParticles) {
        return applyLodLimit(GPUParticleSystem::kMaxParticles);
    }

    uint32_t capacity = GPUParticleSystem::kMinParticles;
    while (capacity < requiredCapacity && capacity < GPUParticleSystem::kMaxParticles) {
        capacity *= 2u;
    }
    return applyLodLimit((std::min)(capacity, GPUParticleSystem::kMaxParticles));
}

void GPUParticleManager::Initialize(DirectXCommon* dxCommon) {
    dxCommon_ = dxCommon;
    // テクスチャはScenePreloaderの転送バッチで準備します。
    // Scene初期化のたびに全PNGを同期ロードするとLoadingSceneが停止するため、ここでは読み込みません。
    if (presets_.empty()) {
        LoadAllPresets("Resources/json/gpu_particles/");
    }
}

void GPUParticleManager::BeginFrame() {
    updatedThisFrame_ = false;
}

void GPUParticleManager::Update(float deltaTime) {
    if (updatedThisFrame_) {
        return;
    }
    const auto cpuStart = std::chrono::high_resolution_clock::now();
    updatedThisFrame_ = true;

    float scaledDelta = deltaTime * timeScale_;

    // オートエミッターの処理
    for (auto& emitter : autoEmitters_) {
        auto it = presets_.find(emitter.presetName);
        if (it != presets_.end() && it->second.isLooping) {
            emitter.timer += scaledDelta;
            if (emitter.timer >= it->second.emitInterval) {
                Emit(emitter.presetName, emitter.position, emitter.transform);
                emitter.timer = 0.0f;
            }
        }
    }

    // ★ 全ての独立した部隊(System)のUpdateを呼ぶ
    for (auto& pair : systems_) {
        pair.second->SetTimeScale(timeScale_);
        pair.second->Update(deltaTime);
    }

    const auto cpuEnd = std::chrono::high_resolution_clock::now();
    lastUpdateCpuTimeMs_ = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
}

void GPUParticleManager::UpdateEditorPreviewStep(float deltaTime) {
    updatedThisFrame_ = false;
    Update(deltaTime);
}

void GPUParticleManager::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t dummy, uint32_t depthSrvHandle) {
    if (systems_.empty()) {
        lastDrawCpuTimeMs_ = 0.0f;
        return;
    }

    const auto cpuStart = std::chrono::high_resolution_clock::now();
    dxCommon_->StartGpuProfile("Particle GPU pass");

    // ★ 共通の状態設定はループの外で行う（軽量化）
    SRVManager::GetInstance()->SetDescriptorHeaps(commandList);

    for (auto& pair : systems_) {
        // 各部隊が自分のテクスチャとブレンドを使う
        pair.second->Draw(commandList, viewMatrix, projectionMatrix, dummy, depthSrvHandle);
    }

    dxCommon_->EndGpuProfile("Particle GPU pass");
    const auto cpuEnd = std::chrono::high_resolution_clock::now();
    lastDrawCpuTimeMs_ = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
}

// ====================================================================
// ★ オートルーティングの心臓部
// 新しいテクスチャやブレンドの組み合わせが来たら、勝手に新しい部隊を作る！
// ====================================================================
GPUParticleSystem* GPUParticleManager::GetOrCreateSystem(const GPUParticleConfig& config) {
    // キーの作成（例: "Resources/sprite/Fire.png_0"）
    std::string key = MakeParticleSystemKey(config);

    if (systems_.find(key) == systems_.end()) {
        auto newSystem = std::make_unique<GPUParticleSystem>();
        newSystem->Initialize(dxCommon_, ResolveParticleCapacity(config));
        systems_[key] = std::move(newSystem);
        DebugConsole::GetInstance()->AddLog("Created new Particle System for: " + key);
    }
    return systems_[key].get();
}

void GPUParticleManager::PreloadPresetSystem(const std::string& presetName) {
    auto it = presets_.find(presetName);
    if (it == presets_.end()) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Warning, "[GPUParticleManager] preload preset not found: " + presetName);
        return;
    }

    GPUParticleSystem* targetSystem = GetOrCreateSystem(it->second);
    targetSystem->SetEmitterMesh(meshVb_, meshVCount_, meshVStride_, meshBoneSrv_);
    targetSystem->SetCurrentTexture(it->second.texturePath);
    targetSystem->RequestWarmup();
}

void GPUParticleManager::PreloadPresetSystems(const std::vector<std::string>& presetNames) {
    for (const std::string& presetName : presetNames) {
        PreloadPresetSystem(presetName);
    }
}

bool GPUParticleManager::IsEmpty() const {
    for (const auto& pair : systems_) {
        if (pair.second && pair.second->IsActive()) {
            return false;
        }
    }
    return true;
}

int GPUParticleManager::GetActiveSystemCount() const {
    int activeCount = 0;
    for (const auto& pair : systems_) {
        if (pair.second && pair.second->IsActive()) {
            ++activeCount;
        }
    }
    return activeCount;
}

size_t GPUParticleManager::GetEstimatedMemoryBytesForConfig(const GPUParticleConfig& config) const {
    const auto it = systems_.find(MakeParticleSystemKey(config));
    if (it == systems_.end() || !it->second) {
        return 0;
    }
    return it->second->GetEstimatedMemoryBytes();
}

bool GPUParticleManager::RequiresSceneColorCopy() const {
    for (const auto& pair : systems_) {
        if (pair.second && pair.second->RequiresSceneColorCopy()) {
            return true;
        }
    }
    return false;
}

void GPUParticleManager::EmitFromConfig(const GPUParticleConfig& config) {
    GPUParticleConfig resolvedConfig = config;
    if (config.lod.enabled) {
        if (const Camera* camera = CameraManager::GetInstance()->GetActiveCamera()) {
            const Vector3 eye = camera->GetEye();
            const float x = config.emitPos.x - eye.x;
            const float y = config.emitPos.y - eye.y;
            const float z = config.emitPos.z - eye.z;
            const float emissionScale = config.lod.EvaluateEmissionScale(std::sqrt(x * x + y * y + z * z));
            if (emissionScale <= 0.0001f) {
                return;
            }
            resolvedConfig.emitCount = (std::max)(
                1,
                static_cast<int>(std::lround(static_cast<float>(config.emitCount) * emissionScale)));
        }
        if (config.lod.maxAliveParticles > 0) {
            resolvedConfig.emitCount = (std::min)(
                resolvedConfig.emitCount,
                config.lod.maxAliveParticles);
        }
    }

    // どの部隊に所属するかを自動判定！
    GPUParticleSystem* targetSystem = GetOrCreateSystem(config);

    // メッシュ情報をその部隊に渡してから発生させる
    targetSystem->SetEmitterMesh(meshVb_, meshVCount_, meshVStride_, meshBoneSrv_);
    targetSystem->SetCurrentTexture(config.texturePath);
    targetSystem->EmitFromConfig(resolvedConfig);
}

void GPUParticleManager::Emit(const std::string& presetName, const Vector3& position, const Matrix4x4& emitterWorldMatrix) {
    auto it = presets_.find(presetName);
    if (it != presets_.end()) {
        GPUParticleConfig config = it->second;
        config.emitPos = position;
        config.emitterWorldMatrix = emitterWorldMatrix;
        EmitFromConfig(config);
    }
}

void GPUParticleManager::EmitDirected(
    const std::string& presetName,
    const Vector3& position,
    const Vector3& direction,
    float speedScale,
    const Matrix4x4& emitterWorldMatrix) {
    const auto it = presets_.find(presetName);
    if (it == presets_.end()) {
        return;
    }

    GPUParticleConfig config = it->second;
    config.emitPos = position;
    config.emitterWorldMatrix = emitterWorldMatrix;

    const float directionLength = Math::Length(direction);
    if (directionLength > 0.0001f) {
        const float presetSpeed = Math::Length(config.emitVelocity);
        const float resolvedSpeed = (std::max)(presetSpeed, 0.01f) * (std::max)(speedScale, 0.0f);
        config.emitVelocity = direction / directionLength * resolvedSpeed;
    }
    EmitFromConfig(config);
}

// ====================================================================
// 以下、旧コードからの移植（JSON読み込みとオートエミッター）
// ====================================================================
void GPUParticleManager::LoadAllPresets(const std::string& directoryPath) {
    std::string normalizedDirectory = directoryPath;
    std::replace(normalizedDirectory.begin(), normalizedDirectory.end(), '\\', '/');
    if (!presets_.empty() && loadedPresetDirectory_ == normalizedDirectory) {
        return;
    }
    if (!fs::exists(directoryPath)) { fs::create_directories(directoryPath); return; }
    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (entry.path().extension() == ".json") {
            std::string filename = entry.path().stem().string();
            std::ifstream file(entry.path());
            if (file.is_open()) {
                json j; file >> j;
                GPUParticleConfig config;
                // --- JSONからデータを復元 ---
                if (j.contains("emitPos")) { config.emitPos.x = j["emitPos"][0]; config.emitPos.y = j["emitPos"][1]; config.emitPos.z = j["emitPos"][2]; }
                if (j.contains("emitArea")) { config.emitArea.x = j["emitArea"][0]; config.emitArea.y = j["emitArea"][1]; config.emitArea.z = j["emitArea"][2]; }
                if (j.contains("emitVelocity")) { config.emitVelocity.x = j["emitVelocity"][0]; config.emitVelocity.y = j["emitVelocity"][1]; config.emitVelocity.z = j["emitVelocity"][2]; }
                if (j.contains("emitCount")) config.emitCount = j["emitCount"];
                if (j.contains("lod") && j["lod"].is_object()) {
                    const json& lod = j["lod"];
                    config.lod.enabled = lod.value("enabled", false);
                    config.lod.nearDistance = lod.value("nearDistance", 12.0f);
                    config.lod.farDistance = lod.value("farDistance", 45.0f);
                    config.lod.farEmissionScale = lod.value("farEmissionScale", 0.25f);
                    config.lod.maxAliveParticles = lod.value("maxAliveParticles", 0);
                    config.lod.Sanitize();
                }
                if (j.contains("emitLife")) config.emitLife = j["emitLife"];
                if (j.contains("maxParticles")) config.maxParticles = j["maxParticles"];
                if (j.contains("velocityVariance")) config.velocityVariance = j["velocityVariance"];
                if (j.contains("baseColor")) { config.baseColor.x = j["baseColor"][0]; config.baseColor.y = j["baseColor"][1]; config.baseColor.z = j["baseColor"][2]; config.baseColor.w = j["baseColor"][3]; }
                if (j.contains("endColor")) { config.endColor.x = j["endColor"][0]; config.endColor.y = j["endColor"][1]; config.endColor.z = j["endColor"][2]; config.endColor.w = j["endColor"][3]; }
                if (j.contains("baseSize")) config.baseSize = j["baseSize"];
                if (j.contains("endSize")) config.endSize = j["endSize"];
                if (j.contains("rotSpeed")) config.rotSpeed = j["rotSpeed"];
                if (j.contains("blendModeIndex")) config.blendModeIndex = j["blendModeIndex"];
                if (config.blendModeIndex >= 2) config.blendModeIndex = 1;
                if (j.contains("envGravity")) { config.envGravity.x = j["envGravity"][0]; config.envGravity.y = j["envGravity"][1]; config.envGravity.z = j["envGravity"][2]; }
                if (j.contains("envDrag")) config.envDrag = j["envDrag"];
                if (j.contains("envWind")) { config.envWind.x = j["envWind"][0]; config.envWind.y = j["envWind"][1]; config.envWind.z = j["envWind"][2]; }
                if (j.contains("envTurbulence")) config.envTurbulence = j["envTurbulence"];
                if (j.contains("fieldType")) config.fieldType = j["fieldType"];
                if (j.contains("fieldPosition")) { config.fieldPosition.x = j["fieldPosition"][0]; config.fieldPosition.y = j["fieldPosition"][1]; config.fieldPosition.z = j["fieldPosition"][2]; }
                if (j.contains("fieldStrength")) config.fieldStrength = j["fieldStrength"];
                if (j.contains("fieldRadius")) config.fieldRadius = j["fieldRadius"];
                if (j.contains("fieldFalloff")) config.fieldFalloff = j["fieldFalloff"];
                if (j.contains("isLooping")) config.isLooping = j["isLooping"];
                if (j.contains("emitInterval")) config.emitInterval = j["emitInterval"];
                if (j.contains("shapeType")) config.shapeType = j["shapeType"];
                if (j.contains("shapeRadius")) config.shapeRadius = j["shapeRadius"];
                if (j.contains("shapeAngle")) config.shapeAngle = j["shapeAngle"];
                if (j.contains("midColor")) { config.midColor.x = j["midColor"][0]; config.midColor.y = j["midColor"][1]; config.midColor.z = j["midColor"][2]; config.midColor.w = j["midColor"][3]; }
                if (j.contains("colorMidTime")) config.colorMidTime = j["colorMidTime"];
                if (j.contains("midSize")) config.midSize = j["midSize"];
                if (j.contains("sizeMidTime")) config.sizeMidTime = j["sizeMidTime"];
                if (j.contains("softParticleFade")) config.softParticleFade = j["softParticleFade"];
                if (j.contains("sizeEaseType")) config.sizeEaseType = j["sizeEaseType"];
                if (j.contains("colorEaseType")) config.colorEaseType = j["colorEaseType"];
                if (j.contains("enableCollision")) config.enableCollision = j["enableCollision"];
                if (j.contains("restitution")) config.restitution = j["restitution"];
                if (j.contains("colorIntensity")) config.colorIntensity = j["colorIntensity"];
                if (j.contains("texturePath")) config.texturePath = j["texturePath"];
                if (j.contains("spriteSheetColumns")) config.spriteSheetColumns = j["spriteSheetColumns"];
                if (j.contains("spriteSheetRows")) config.spriteSheetRows = j["spriteSheetRows"];
                if (j.contains("spriteSheetFrameCount")) config.spriteSheetFrameCount = j["spriteSheetFrameCount"];
                if (j.contains("spriteSheetFps")) config.spriteSheetFps = j["spriteSheetFps"];
                if (j.contains("spriteSheetLoop")) {
                    config.spriteSheetLoop = j["spriteSheetLoop"].is_boolean() ? (j["spriteSheetLoop"].get<bool>() ? 1 : 0) : j["spriteSheetLoop"].get<int>();
                }
                if (j.contains("spriteSheetRandomStart")) {
                    config.spriteSheetRandomStart = j["spriteSheetRandomStart"].is_boolean() ? (j["spriteSheetRandomStart"].get<bool>() ? 1 : 0) : j["spriteSheetRandomStart"].get<int>();
                }
                if (j.contains("alignToVelocity")) {
                    config.alignToVelocity = j["alignToVelocity"].is_boolean() ? (j["alignToVelocity"].get<bool>() ? 1 : 0) : j["alignToVelocity"].get<int>();
                }
                if (j.contains("velocityStretch")) config.velocityStretch = j["velocityStretch"];
                if (j.contains("particleType")) config.particleType = j["particleType"];
                if (j.contains("trailLength")) config.trailLength = j["trailLength"];
                if (j.contains("receiveLighting")) {
                    config.receiveLighting = j["receiveLighting"].is_boolean() ? (j["receiveLighting"].get<bool>() ? 1 : 0) : j["receiveLighting"].get<int>();
                }
                if (j.contains("lightDirection")) { config.lightDirection.x = j["lightDirection"][0]; config.lightDirection.y = j["lightDirection"][1]; config.lightDirection.z = j["lightDirection"][2]; }
                if (j.contains("lightColor")) { config.lightColor.x = j["lightColor"][0]; config.lightColor.y = j["lightColor"][1]; config.lightColor.z = j["lightColor"][2]; }
                if (j.contains("lightingStrength")) config.lightingStrength = j["lightingStrength"];
                if (j.contains("spriteAnimation") && j["spriteAnimation"].is_object()) {
                    const auto& anim = j["spriteAnimation"];
                    if (anim.contains("columns")) config.spriteSheetColumns = anim["columns"];
                    if (anim.contains("rows")) config.spriteSheetRows = anim["rows"];
                    if (anim.contains("frameCount")) config.spriteSheetFrameCount = anim["frameCount"];
                    if (anim.contains("fps")) config.spriteSheetFps = anim["fps"];
                    if (anim.contains("loop")) {
                        config.spriteSheetLoop = anim["loop"].is_boolean() ? (anim["loop"].get<bool>() ? 1 : 0) : anim["loop"].get<int>();
                    }
                    if (anim.contains("randomStart")) {
                        config.spriteSheetRandomStart = anim["randomStart"].is_boolean() ? (anim["randomStart"].get<bool>() ? 1 : 0) : anim["randomStart"].get<int>();
                    }
                }

                presets_[filename] = config;
                DebugConsole::GetInstance()->AddLog("Loaded Particle Preset: " + filename);
            }
        }
    }
    loadedPresetDirectory_ = normalizedDirectory;
}

void GPUParticleManager::ReloadAllPresets(const std::string& directoryPath) {
    presets_.clear();
    loadedPresetDirectory_.clear();
    LoadAllPresets(directoryPath);
}

uint32_t GPUParticleManager::PlayAutoEmitter(const std::string& presetName, const Vector3& position) {
    Matrix4x4 identity = { 1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f };
    return PlayAutoEmitter(presetName, position, identity);
}
uint32_t GPUParticleManager::PlayAutoEmitter(const std::string& presetName, const Vector3& position, const Matrix4x4& transform) {
    AutoEmitter em; em.id = nextAutoEmitterId_++; em.presetName = presetName; em.position = position; em.transform = transform; em.timer = 0.0f;
    autoEmitters_.push_back(em);
    Emit(presetName, position, transform);
    return em.id;
}
void GPUParticleManager::StopAutoEmitter(uint32_t id) {
    autoEmitters_.erase(std::remove_if(autoEmitters_.begin(), autoEmitters_.end(), [id](const AutoEmitter& e) { return e.id == id; }), autoEmitters_.end());
}
void GPUParticleManager::ClearAllAutoEmitters() { autoEmitters_.clear(); }

void GPUParticleManager::ResetSimulation() {
    autoEmitters_.clear();
    for (auto& [key, system] : systems_) {
        (void)key;
        if (system) {
            system->RequestSimulationReset();
        }
    }
    updatedThisFrame_ = false;
}

void GPUParticleManager::ClearSceneRuntime() {
    autoEmitters_.clear();
    for (auto& [key, system] : systems_) {
        (void)key;
        if (system) {
            // 旧シーンのModelバッファを次のシーンへ持ち越さない。
            system->SetEmitterMesh(nullptr, 0, 0, 0);
            system->ResetForSceneTransition();
        }
    }
    nextAutoEmitterId_ = 0;
    timeScale_ = 1.0f;
    updatedThisFrame_ = false;
    lastUpdateCpuTimeMs_ = 0.0f;
    lastDrawCpuTimeMs_ = 0.0f;
    meshVb_ = nullptr;
    meshVCount_ = 0;
    meshVStride_ = 0;
    meshBoneSrv_ = 0;
}
