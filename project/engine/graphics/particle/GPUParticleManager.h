#pragma once
#include "GPUParticleSystem.h"
#include "GPUParticleConfig.h"
#include <unordered_map>
#include <map>
#include <string>
#include <memory>
#include <vector>
#include <DirectXCommon.h>

// GPUParticleManagerは、GPUパーティクルシステムのプリセット、発生、更新、描画を一元管理します。
class GPUParticleManager {
public:
        // エンジン全体で共有するGPUパーティクル管理インスタンスを取得します。
static GPUParticleManager* GetInstance();

        // GPUパーティクル描画とCompute処理に必要なDirectXリソースを準備します。
void Initialize(DirectXCommon* dxCommon);
        // 1フレーム内の発生や更新状態を初期化します。
void BeginFrame();
        // 各GPUパーティクルシステムのシミュレーション時間を進めます。
void Update(float deltaTime);
        // 全GPUパーティクルをカメラ行列と深度情報に合わせて描画します。
void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix, uint32_t dummyTexture = 0, uint32_t depthSrvHandle = 0);

    void LoadAllPresets(const std::string& directoryPath);
    void PreloadPresetSystem(const std::string& presetName);
    void PreloadPresetSystems(const std::vector<std::string>& presetNames);

        // プリセット名を指定して指定位置へGPUパーティクルを発生させます。
void Emit(const std::string& presetName, const Vector3& position, const Matrix4x4& emitterWorldMatrix = Math::MakeIdentity4x4());
    void EmitFromConfig(const GPUParticleConfig& config);

    float GetTimeScale() const { return timeScale_; }
    void SetTimeScale(float timeScale) { timeScale_ = timeScale; }
    bool IsInitialized() const { return dxCommon_ != nullptr; }

    const std::map<std::string, GPUParticleConfig>& GetPresets() const { return presets_; }

        // 自動発生するGPUパーティクルエミッターを開始し、管理IDを返します。
uint32_t PlayAutoEmitter(const std::string& presetName, const Vector3& position);
    uint32_t PlayAutoEmitter(const std::string& presetName, const Vector3& position, const Matrix4x4& transform);
    void StopAutoEmitter(uint32_t id);
    void ClearAllAutoEmitters();
    bool IsEmpty() const;

    void SetEmitterMesh(ID3D12Resource* vb, uint32_t vCount, uint32_t vStride, uint32_t boneSrvIndex) {
        meshVb_ = vb; meshVCount_ = vCount; meshVStride_ = vStride; meshBoneSrv_ = boneSrvIndex;
    }

private:
    DirectXCommon* dxCommon_ = nullptr;

    // プリセットデータ（司令部が一括管理）
    std::map<std::string, GPUParticleConfig> presets_;

    // ========================================================
    // ★ ここが魔法のコア！テクスチャとブレンドモードごとに分割された部隊たち
    // Key: "TexturePath_BlendModeIndex"
    // ========================================================
    std::unordered_map<std::string, std::unique_ptr<GPUParticleSystem>> systems_;

    // 内部システム自動取得・作成用
    GPUParticleSystem* GetOrCreateSystem(const GPUParticleConfig& config);

    // オートエミッター関連
    struct AutoEmitter {
        uint32_t id;
        std::string presetName;
        Vector3 position;
        Matrix4x4 transform;
        float timer;
    };
    std::vector<AutoEmitter> autoEmitters_;
    uint32_t nextAutoEmitterId_ = 0;
    float timeScale_ = 1.0f;
    bool updatedThisFrame_ = false;

    // メッシュデータ保持用
    ID3D12Resource* meshVb_ = nullptr;
    uint32_t meshVCount_ = 0;
    uint32_t meshVStride_ = 0;
    uint32_t meshBoneSrv_ = 0;
};
