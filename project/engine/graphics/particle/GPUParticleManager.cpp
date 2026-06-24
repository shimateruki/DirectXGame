#include "GPUParticleManager.h"
#include <filesystem>
#include <fstream>
#include "json.hpp"
#include "DebugConsole.h"
#include "SRVManager.h"
#include <TextureManager.h>
#include <d3d12.h>
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
        << "_rand" << config.spriteSheetRandomStart;
    return key.str();
}
}

GPUParticleManager* GPUParticleManager::GetInstance() {
    static GPUParticleManager instance;
    return &instance;
}

void GPUParticleManager::Initialize(DirectXCommon* dxCommon) {
    dxCommon_ = dxCommon;
    // パーティクル用のディレクトリをスキャンして、すべての画像を自動で事前ロードする
    std::string particleDir = "Resources/sprite/particle";
    if (fs::exists(particleDir)) {
        for (const auto& entry : fs::directory_iterator(particleDir)) {
            if (entry.path().extension() == ".png") {
                // Windowsのパス区切り(\)を(/)に統一してロード
                std::string path = entry.path().string();
                std::replace(path.begin(), path.end(), '\\', '/');
                TextureManager::GetInstance()->Load(path);
            }
        }
    }
    
    // 万が一フォルダが空だった場合などの最低限の保険
    TextureManager::GetInstance()->Load("Resources/sprite/particle/white.png");

    // 全てのプリセットJSONを読み込む
    LoadAllPresets("Resources/json/gpu_particles/");
}

void GPUParticleManager::BeginFrame() {
    updatedThisFrame_ = false;
}

void GPUParticleManager::Update(float deltaTime) {
    if (updatedThisFrame_) {
        return;
    }
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
}

void GPUParticleManager::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t dummy, uint32_t depthSrvHandle) {
    if (systems_.empty()) return;

    // ★ 共通の状態設定はループの外で行う（軽量化）
    SRVManager::GetInstance()->SetDescriptorHeaps(commandList);

    for (auto& pair : systems_) {
        // 各部隊が自分のテクスチャとブレンドを使う
        pair.second->Draw(commandList, viewMatrix, projectionMatrix, dummy, depthSrvHandle);
    }
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
        newSystem->Initialize(dxCommon_);
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

void GPUParticleManager::EmitFromConfig(const GPUParticleConfig& config) {
    // どの部隊に所属するかを自動判定！
    GPUParticleSystem* targetSystem = GetOrCreateSystem(config);

    // メッシュ情報をその部隊に渡してから発生させる
    targetSystem->SetEmitterMesh(meshVb_, meshVCount_, meshVStride_, meshBoneSrv_);
    targetSystem->SetCurrentTexture(config.texturePath);
    targetSystem->EmitFromConfig(config);
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

// ====================================================================
// 以下、旧コードからの移植（JSON読み込みとオートエミッター）
// ====================================================================
void GPUParticleManager::LoadAllPresets(const std::string& directoryPath) {
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
                if (j.contains("emitLife")) config.emitLife = j["emitLife"];
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
