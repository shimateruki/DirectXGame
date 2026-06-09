#pragma once

#include "Object3dCommon.h"
#include "MeshRenderer.h"
#include "Model.h"
#include "engine/utility/math/Math.h"

#include <map>
#include <random>
#include <string>
#include <vector>
#include <wrl.h>

class Object3dCommon;
struct ID3D12Resource;

struct DebrisEffectConfig {
    std::string name = "rock_burst";
    std::vector<std::string> modelNames = { "Primitives/cube" };
    int spawnCount = 18;
    Vector3 spawnOffset = { 0.0f, 0.4f, 0.0f };
    Vector3 baseDirection = { 0.0f, 0.0f, 1.0f };
    float horizontalSpread = 1.0f;
    float verticalMin = 0.25f;
    float verticalMax = 0.9f;
    float speedMin = 7.0f;
    float speedMax = 16.0f;
    float angularSpeedMin = 3.0f;
    float angularSpeedMax = 12.0f;
    float scaleMin = 0.15f;
    float scaleMax = 0.45f;
    float lifetimeMin = 1.0f;
    float lifetimeMax = 1.8f;
    float gravity = 28.0f;
    float airDrag = 0.08f;
    float restitution = 0.35f;
    float friction = 0.78f;
    float groundY = 0.0f;
    float fadeStartRatio = 0.72f;
    Vector4 color = { 0.62f, 0.55f, 0.48f, 1.0f };
    int materialType = 0;
    float emissive = 0.0f;
    bool collideGround = true;
    bool shrinkOnFade = true;
};

class DebrisEffectManager {
public:
    static DebrisEffectManager* GetInstance();

    void Initialize(Object3dCommon* common);
    void Update(float deltaTime);
    void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr);
    void Clear();

    void LoadAllPresets(const std::string& directoryPath);
    bool LoadConfig(const std::string& filePath, DebrisEffectConfig& outConfig) const;
    bool SaveConfig(const std::string& filePath, const DebrisEffectConfig& config) const;
    void RegisterPreset(const std::string& presetName, const DebrisEffectConfig& config);
    const std::map<std::string, DebrisEffectConfig>& GetPresets() const { return presets_; }

    void Spawn(const std::string& presetName, const Vector3& position);
    void SpawnFromConfig(const DebrisEffectConfig& config, const Vector3& position);

private:
    struct DebrisPiece {
        Model* model = nullptr;
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };
        Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource;
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowWvpResource;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
        MeshRenderer::TransformationMatrix* wvpData = nullptr;
        MeshRenderer::TransformationMatrix* shadowWvpData = nullptr;
        MeshRenderer::MaterialData* materialData = nullptr;
        Vector3 velocity = { 0.0f, 0.0f, 0.0f };
        Vector3 angularVelocity = { 0.0f, 0.0f, 0.0f };
        float age = 0.0f;
        float lifetime = 1.0f;
        float baseScale = 1.0f;
        float currentScale = 1.0f;
        float gravity = 28.0f;
        float airDrag = 0.08f;
        float restitution = 0.35f;
        float friction = 0.78f;
        float groundY = 0.0f;
        float fadeStartRatio = 0.72f;
        bool collideGround = true;
        bool shrinkOnFade = true;
        bool sleeping = false;
    };

    DebrisEffectManager();

    Object3dCommon* ResolveCommon();
    DebrisPiece AcquirePiece();
    void RecyclePiece(size_t index);
    bool InitializePieceResources(DebrisPiece& piece, const DebrisEffectConfig& config);
    void UpdatePieceMatrix(DebrisPiece& piece, const Matrix4x4& viewProjection, const Matrix4x4& lightViewProjection);
    float RandomRange(float minValue, float maxValue);
    int RandomIndex(int maxExclusive);
    Vector3 RandomDirection(const DebrisEffectConfig& config);
    std::string ResolvePresetPath(const std::string& presetName) const;

    Object3dCommon* common_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    MeshRenderer::CameraForGPU* cameraData_ = nullptr;
    std::vector<DebrisPiece> activePieces_;
    std::vector<DebrisPiece> pooledPieces_;
    std::map<std::string, DebrisEffectConfig> presets_;
    mutable std::mt19937 randomEngine_;
};
