#include "GpuParticleManager.h"
#include "GpuParticlePresetJsonCodec.h"
#include <filesystem>
#include <fstream>
#include "json.hpp"
#include "DebugConsole.h"
#include "SrvManager.h"
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
// GPUパーティクルの描画状態と容量から、共有可能なSystemを識別するキーを作ります。
std::string MakeParticleSystemKey(const GpuParticleConfig& config) {
    // 粒子ごとに持たない描画設定をキーに含め、同じSystemへ異なる材質状態が混ざるのを防ぐ。
    // softParticleFadeや衝突設定など、このキーに含まれない値の共有範囲にも注意する。
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
        << "_capacity" << GpuParticleManager::ResolveParticleCapacity(config);
    return key.str();
}
}

GpuParticleManager* GpuParticleManager::GetInstance() {
    static GpuParticleManager instance;
    return &instance;
}

// 最大数の明示設定を優先し、未指定なら寿命中に重なる放出回数から2の累乗で容量を見積もります。
// LOD上限とGPU Systemの許容範囲を最後に適用します。
uint32_t GpuParticleManager::ResolveParticleCapacity(const GpuParticleConfig& config) {
    const auto applyLodLimit = [&config](uint32_t capacity) {
        if (!config.lod.enabled || config.lod.maxAliveParticles <= 0) {
            return capacity;
        }

        const uint32_t lodCapacity = static_cast<uint32_t>(std::clamp(
            config.lod.maxAliveParticles,
            static_cast<int>(GpuParticleSystem::kMinParticles),
            static_cast<int>(GpuParticleSystem::kMaxParticles)));
        return (std::min)(capacity, lodCapacity);
    };

    if (config.maxParticles > 0) {
        return applyLodLimit(static_cast<uint32_t>(std::clamp(
            config.maxParticles,
            static_cast<int>(GpuParticleSystem::kMinParticles),
            static_cast<int>(GpuParticleSystem::kMaxParticles))));
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
        static_cast<uint64_t>(GpuParticleSystem::kMinParticles));
    if (requiredCapacity >= GpuParticleSystem::kMaxParticles) {
        return applyLodLimit(GpuParticleSystem::kMaxParticles);
    }

    uint32_t capacity = GpuParticleSystem::kMinParticles;

    while (capacity < requiredCapacity && capacity < GpuParticleSystem::kMaxParticles) {
        capacity *= 2u;
    }

    return applyLodLimit((std::min)(capacity, GpuParticleSystem::kMaxParticles));
}

void GpuParticleManager::Initialize(DirectXCommon* dxCommon) {
    dxCommon_ = dxCommon;

    // テクスチャはScenePreloaderの転送バッチで準備します。
    // Scene初期化のたびに全PNGを同期ロードするとLoadingSceneが停止するため、ここでは読み込みません。
    if (presets_.empty()) {
        LoadAllPresets("Resources/json/gpu_particles/");
    }
}

void GpuParticleManager::InitializeWithPresets(
    DirectXCommon* dxCommon, const std::vector<std::string>& presetNames) {
    // 呼出し側が指定したプリセットだけを登録する。空の一覧では追加読込を行わない。
    dxCommon_ = dxCommon;

    if (!presetNames.empty()) {
        LoadPresetFiles("Resources/json/gpu_particles/", presetNames);
    }
}

void GpuParticleManager::BeginFrame() {
    updatedThisFrame_ = false;
}

// 1フレーム1回だけ自動Emitterと各GPU Systemを更新し、設定された時間倍率を反映します。
void GpuParticleManager::Update(float deltaTime) {
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

    // テクスチャ・ブレンドごとの System に同じ時間倍率を渡してから更新します。
    for (auto& pair : systems_) {
        pair.second->SetTimeScale(timeScale_);
        pair.second->Update(deltaTime);
    }

    const auto cpuEnd = std::chrono::high_resolution_clock::now();
    lastUpdateCpuTimeMs_ = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
}

void GpuParticleManager::UpdateEditorPreviewStep(float deltaTime) {
    updatedThisFrame_ = false;
    Update(deltaTime);
}

// 通常Systemと表示中の制作用プレビューを、同じディスクリプタヒープで描画します。
void GpuParticleManager::Draw(ID3D12GraphicsCommandList* commandList, const engine::Matrix4x4& viewMatrix, const engine::Matrix4x4& projectionMatrix, uint32_t dummy, uint32_t depthSrvHandle) {
    if (systems_.empty() && !authoringPreviewVisible_) {
        lastDrawCpuTimeMs_ = 0.0f;
        return;
    }

    const auto cpuStart = std::chrono::high_resolution_clock::now();
    dxCommon_->StartGpuProfile("Particle GPU pass");

    // 全 System が使うディスクリプタヒープを一度だけ設定します。
    SrvManager::GetInstance()->SetDescriptorHeaps(commandList);

    for (auto& pair : systems_) {
        // 各 System が自身のテクスチャとブレンド設定を使います。
        pair.second->Draw(commandList, viewMatrix, projectionMatrix, dummy, depthSrvHandle);
    }

    if (authoringPreviewVisible_) {
        for (auto& [key, system] : authoringSystems_)
            system->Draw(commandList, viewMatrix, projectionMatrix, dummy, depthSrvHandle);
    }

    dxCommon_->EndGpuProfile("Particle GPU pass");
    const auto cpuEnd = std::chrono::high_resolution_clock::now();
    lastDrawCpuTimeMs_ = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
}

// 描画設定・容量を含むMakeParticleSystemKeyで共有し、未登録の組み合わせだけ生成する。
GpuParticleSystem* GpuParticleManager::GetOrCreateSystem(const GpuParticleConfig& config) {
    std::string key = MakeParticleSystemKey(config);

    if (systems_.find(key) == systems_.end()) {
        auto newSystem = std::make_unique<GpuParticleSystem>();
        newSystem->Initialize(dxCommon_, ResolveParticleCapacity(config));
        systems_[key] = std::move(newSystem);
        DebugConsole::GetInstance()->AddLog("Created new Particle System for: " + key);
    }

    return systems_[key].get();
}

void GpuParticleManager::PreloadPresetSystem(const std::string& presetName) {
    // 初回放出前に使用予定のGPU粒子設定と素材を準備する。
    auto it = presets_.find(presetName);

    if (it == presets_.end()) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Warning, "[GPUParticleManager] preload preset not found: " + presetName);
        return;
    }

    GpuParticleSystem* targetSystem = GetOrCreateSystem(it->second);
    targetSystem->SetEmitterMesh(meshVb_, meshVCount_, meshVStride_, meshBoneSrv_);
    targetSystem->SetCurrentTexture(it->second.texturePath);
    targetSystem->RequestWarmup();
}

// シーンが使うPresetだけを列挙してGPU Systemを事前準備します。
void GpuParticleManager::PreloadPresetSystems(const std::vector<std::string>& presetNames) {
    for (const std::string& presetName : presetNames) {
        PreloadPresetSystem(presetName);
    }
}

bool GpuParticleManager::IsEmpty() const {
    // 制作プレビューが表示中なら、通常システムが休止していても描画対象ありとする。
    if (authoringPreviewVisible_)
        return false;
    for (const auto& pair : systems_) {
        if (pair.second && pair.second->IsActive()) {
            return false;
        }
    }

    return true;
}

int GpuParticleManager::GetActiveSystemCount() const {
    // 共有済みシステムの総数ではなく、実際に粒子を保持するものだけ数える。
    int activeCount = 0;

    for (const auto& pair : systems_) {
        if (pair.second && pair.second->IsActive()) {
            ++activeCount;
        }
    }

    return activeCount;
}

size_t GpuParticleManager::GetEstimatedMemoryBytesForConfig(const GpuParticleConfig& config) const {
    const auto it = systems_.find(MakeParticleSystemKey(config));

    if (it == systems_.end() || !it->second) {
        return 0;
    }

    return it->second->GetEstimatedMemoryBytes();
}

bool GpuParticleManager::RequiresSceneColorCopy() const {
    // プレビュー系も通常系も、背景色を参照する描画が一つでもあればコピーを要求する。
    if (authoringPreviewVisible_) for (const auto& [key, system] : authoringSystems_)
        if (system && system->RequiresSceneColorCopy())
            return true;
    for (const auto& pair : systems_) {
        if (pair.second && pair.second->RequiresSceneColorCopy()) {
            return true;
        }
    }

    return false;
}

// カメラ距離によるLODを放出数へ適用し、描画設定を共有するGPU Systemへ放出します。
void GpuParticleManager::EmitFromConfig(const GpuParticleConfig& config) {
    GpuParticleConfig resolvedConfig = config;

    if (config.lod.enabled) {
        if (const Camera* camera = CameraManager::GetInstance()->GetActiveCamera()) {
            const engine::Vector3 eye = camera->GetEye();
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

    // GPU側の容量と描画設定に対応するSystemを取得します。放出数だけはLOD適用後の値を渡します。
    GpuParticleSystem* targetSystem = GetOrCreateSystem(config);

    // メッシュ表面から放出するPresetに備え、現在の発生元モデル情報を渡します。
    targetSystem->SetEmitterMesh(meshVb_, meshVCount_, meshVStride_, meshBoneSrv_);
    targetSystem->SetCurrentTexture(config.texturePath);
    targetSystem->EmitFromConfig(resolvedConfig);
}

void GpuParticleManager::Emit(const std::string& presetName, const engine::Vector3& position, const engine::Matrix4x4& emitterWorldMatrix) {
    // 登録済みの設定をコピーし、今回の発生位置と親行列だけ上書きして流す。
    auto it = presets_.find(presetName);

    if (it != presets_.end()) {
        GpuParticleConfig config = it->second;
        config.emitPos = position;
        config.emitterWorldMatrix = emitterWorldMatrix;
        EmitFromConfig(config);
    }
}

void GpuParticleManager::EmitDirected(
    const std::string& presetName,
    const engine::Vector3& position,
    const engine::Vector3& direction,
    float speedScale,
    const engine::Matrix4x4& emitterWorldMatrix) {
    const auto it = presets_.find(presetName);

    if (it == presets_.end()) {
        return;
    }

    GpuParticleConfig config = it->second;
    config.emitPos = position;
    config.emitterWorldMatrix = emitterWorldMatrix;

    const float directionLength = engine::Math::Length(direction);

    if (directionLength > 0.0001f) {
        const float presetSpeed = engine::Math::Length(config.emitVelocity);
        const float resolvedSpeed = (std::max)(presetSpeed, 0.01f) * (std::max)(speedScale, 0.0f);
        config.emitVelocity = direction / directionLength * resolvedSpeed;
    }

    EmitFromConfig(config);
}

// ====================================================================
// 以下、旧コードからの移植（JSON読み込みとオートエミッター）
// ====================================================================
void GpuParticleManager::LoadAllPresets(const std::string& directoryPath) {
    LoadPresetFiles(directoryPath, {});
}

void GpuParticleManager::LoadPresetFiles(
    const std::string& directoryPath, const std::vector<std::string>& presetNames) {
    const bool loadAll = presetNames.empty();
    std::string normalizedDirectory = directoryPath;
    std::replace(normalizedDirectory.begin(), normalizedDirectory.end(), '\\', '/');

    if (loadAll && !presets_.empty() && loadedPresetDirectory_ == normalizedDirectory) {
        return;
    }

    if (!fs::exists(directoryPath)) {
        fs::create_directories(directoryPath);
        return;
    }
    std::vector<fs::path> presetPaths;

    if (loadAll) {
        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            presetPaths.push_back(entry.path());
        }
    } else {
        for (const std::string& name : presetNames) {
            presetPaths.push_back(fs::path(directoryPath) / (name + ".json"));
        }
    }

    for (const fs::path& presetPath : presetPaths) {
        if (presetPath.extension() == ".json") {
            std::string filename = presetPath.stem().string();
            std::ifstream file(presetPath);

            if (file.is_open()) {
                json j;
                file >> j;
                GpuParticleConfig config;
                GpuParticlePresetJsonCodec::Read(j, config);

                // 実行時だけの描画モード互換補正は、Editorの読込とは分けて維持する。
                if (config.blendModeIndex >= 2) {
                    config.blendModeIndex = 1;
                }

                presets_[filename] = config;
                DebugConsole::GetInstance()->AddLog("Loaded Particle Preset: " + filename);
            }
        }
    }

    if (loadAll) {
        loadedPresetDirectory_ = normalizedDirectory;
    }
}

void GpuParticleManager::ReloadAllPresets(const std::string& directoryPath) {
    presets_.clear();
    loadedPresetDirectory_.clear();
    LoadAllPresets(directoryPath);
}

uint32_t GpuParticleManager::PlayAutoEmitter(const std::string& presetName, const engine::Vector3& position) {
    engine::Matrix4x4 identity = { 1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f };
    return PlayAutoEmitter(presetName, position, identity);
}

uint32_t GpuParticleManager::PlayAutoEmitter(const std::string& presetName, const engine::Vector3& position, const engine::Matrix4x4& transform) {
    // 登録時に一回発生させ、更新ループの次回周期まで何も表示されない時間を作らない。
    AutoEmitter em;
    em.id = nextAutoEmitterId_++;
    em.presetName = presetName;
    em.position = position;
    em.transform = transform;
    em.timer = 0.0f;
    autoEmitters_.push_back(em);
    Emit(presetName, position, transform);
    return em.id;
}

void GpuParticleManager::StopAutoEmitter(uint32_t id) {
    autoEmitters_.erase(std::remove_if(autoEmitters_.begin(), autoEmitters_.end(), [id](const AutoEmitter& e) { return e.id == id; }), autoEmitters_.end());
}

void GpuParticleManager::ClearAllAutoEmitters() { autoEmitters_.clear(); }

// 自動Emitterを破棄し、各GPU Systemの初期化を次回更新へ予約します。
void GpuParticleManager::ResetSimulation() {
    // 自動エミッターを止めたうえで、各GPUシステムへ次回更新時の初期化を予約する。
    autoEmitters_.clear();

    for (auto& [key, system] : systems_) {
        (void)key;

        if (system) {
            system->RequestSimulationReset();
        }
    }

    updatedThisFrame_ = false;
}

// シーン固有のEmitterと借用メッシュ参照を解除し、GPU Systemを次シーン用に戻します。
void GpuParticleManager::ClearSceneRuntime() {
    ClearAuthoringPreview();
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

void GpuParticleManager::ClearAuthoringPreview() {
    authoringPreviewVisible_ = false;

    for (auto& [key, system] : authoringSystems_)
        system->ResetForSceneTransition();
}

// 制作用の独立SystemでPresetを指定年齢まで進め、通常再生中のSystemへ状態を混ぜません。
bool GpuParticleManager::ShowAuthoringPreview(const std::string& preset, const engine::Vector3& position,
    const engine::Vector3& direction, bool directed, float speedScale, float age) {
    const auto found = presets_.find(preset);
    if (!dxCommon_ || found == presets_.end())
        return false;
    auto config = found->second;

    // メッシュ頂点発生には所有モデルが必要です。他Objectの頂点バッファを借りて誤表示しません。
    if (config.shapeType == 3)
        return false;
    config.emitPos = position;
    config.emitterWorldMatrix = engine::Math::MakeIdentity4x4();
    if (directed && engine::Math::Length(direction) > 0.0001f)
        config.emitVelocity = direction / engine::Math::Length(direction) *
            ((std::max)(0.01f, engine::Math::Length(config.emitVelocity)) * (std::max)(0.0f, speedScale));

    // 配置では素材ごとに分離し、補助素材の設定や放出年齢が主素材へ混ざるのを防ぎます。
    const auto key = preset + "|" + MakeParticleSystemKey(config);
    auto& system = authoringSystems_[key];
    const bool created = !system;

    if (!system) {
        system = std::make_unique<GpuParticleSystem>();
        system->Initialize(dxCommon_, ResolveParticleCapacity(config));
    }

    system->SetCurrentTexture(config.texturePath);
    system->SetTimeScale(1);
    const bool firstEmission = created || !system->IsActive();
    system->EmitFromConfig(config);

    // GPU側は次のDrawで経過時間をまとめて消費します。固定刻みの再現とは区別します。
    if (firstEmission)
        system->Update(std::clamp(age, 0.0f, 5.0f));
    authoringPreviewVisible_ = true;
    return true;
}

#ifdef USE_IMGUI
void GpuParticleManager::BeginEditorSession() {
    if (editorSession_) return;
    savedEditorScale_ = timeScale_;
    savedEditorMesh_ = meshVb_; savedEditorMeshCount_ = meshVCount_; savedEditorMeshStride_ = meshVStride_; savedEditorMeshBones_ = meshBoneSrv_;
    systems_.swap(savedEditorSystems_); autoEmitters_.swap(savedEditorEmitters_);
    // 前回の確認用GPUバッファは再利用し、開始前のバッファには触れない。
    ResetSimulation(); timeScale_ = 1; updatedThisFrame_ = false; editorSession_ = true;
}
void GpuParticleManager::EndEditorSession() {
    if (!editorSession_) return;
    ResetSimulation();
    systems_.swap(savedEditorSystems_); autoEmitters_.swap(savedEditorEmitters_);
    timeScale_ = savedEditorScale_; updatedThisFrame_ = false; editorSession_ = false;
    SetEmitterMesh(savedEditorMesh_, savedEditorMeshCount_, savedEditorMeshStride_, savedEditorMeshBones_);
}
#endif
