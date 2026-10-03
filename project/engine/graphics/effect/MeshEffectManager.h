#pragma once
#include "EffectObject3d.h"
#include "engine/utility/math/Math.h"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

/// <summary>EffectObject3dの生成、再生、描画、プリロードを管理する。</summary>
class MeshEffectManager {
public:
    using EffectScopeId = uint64_t;
    static constexpr EffectScopeId kInvalidEffectScope = 0;

    /// <summary>共有のメッシュエフェクト管理インスタンスを取得する。</summary>
    static MeshEffectManager* GetInstance();

    /// <summary>エフェクト生成に必要なObject3dCommonを設定する。</summary>
    /// <remarks>commonは所有しないため、利用中は呼び出し側で保持する。</remarks>
    void Initialize(Object3dCommon* common);

    /// <summary>1フレーム内の更新済み状態を初期化する。</summary>
    void BeginFrame();

    /// <summary>再生前にプリセットを読み込み、初回生成の負荷を減らす。</summary>
    void PreloadEffect(const std::string& jsonFilePath);

    /// <summary>エフェクトを更新し、寿命を終えたものを回収する。</summary>
    void Update(float deltaTime);

    /// <summary>エディタのステップ再生としてエフェクトを更新する。</summary>
    void UpdateEditorPreviewStep(float deltaTime);

    /// <summary>エフェクトの時間倍率を指定する。負値は0として扱う。</summary>
    void SetTimeScale(float timeScale) { timeScale_ = (std::max)(0.0f, timeScale); }

    /// <summary>現在の時間倍率を返す。</summary>
    float GetTimeScale() const { return timeScale_; }

    /// <summary>現在再生中のエフェクトを描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr);

    /// <summary>歪み描画のためにシーンカラーのコピーが必要かを返す。</summary>
    bool RequiresSceneColorCopy() const;

    /// <summary>能力などの所有者ごとにエフェクトをまとめるIDを作成する。</summary>
    EffectScopeId CreateEffectScope();

    /// <summary>指定スコープに属するエフェクトだけを停止する。</summary>
    void StopEffectScope(EffectScopeId scopeId);

    /// <summary>対象Object3dに追従するメッシュエフェクトを生成する。</summary>
    void SpawnEffect(const std::string& jsonFilePath, Object3d* baseObject = nullptr, const Vector3& extOffset = { 0,0,0 }, const Vector3& extRot = { 0,0,0 }, const Vector3& extScale = { 1,1,1 });

    /// <summary>JSON側の配置値に代えて、指定のワールド座標・回転で生成する。</summary>
    void SpawnEffectAt(const std::string& jsonFilePath, const Vector3& worldPos, const Vector3& worldRot, const Vector3& scale = { 1,1,1 });

    /// <summary>指定スコープに属するエフェクトをワールド座標で生成する。</summary>
    void SpawnEffectAtScoped(EffectScopeId scopeId, const std::string& jsonFilePath,
        const Vector3& worldPos, const Vector3& worldRot, const Vector3& scale = { 1,1,1 });

    /// <summary>リング状の波紋エフェクトを発生させる。</summary>
    void SpawnRingWaveEffect(const Vector3& position);

    /// <summary>円柱状のポータルエフェクトを一定時間発生させる。</summary>
    void SpawnPortalEffect(const Vector3& position, float lifetime = 3.0f);

    /// <summary>初期化状態とプリロードを維持し、再生中のエフェクトだけを消す。</summary>
    void ClearActiveEffects() { activeEffects_.clear(); effectScopes_.clear(); previewEffectForDebug_ = nullptr; }

    /// <summary>シーン切り替え時にエフェクト・プリロード・生成元参照を破棄する。</summary>
    void Clear() { activeEffects_.clear(); effectScopes_.clear(); preloadedEffects_.clear(); common_ = nullptr; }

    /// <summary>現在再生中のエフェクトを返す。</summary>
    const std::vector<std::unique_ptr<EffectObject3d>>& GetActiveEffects() const { return activeEffects_; }

    /// <summary>エディタで表示中のエフェクトを指定する。所有しない。</summary>
    void SetPreviewEffectForDebug(EffectObject3d* effect) { previewEffectForDebug_ = effect; }

    /// <summary>エディタで表示中のエフェクトを返す。</summary>
    EffectObject3d* GetPreviewEffectForDebug() const { return previewEffectForDebug_; }
private:
    MeshEffectManager() = default;
    ~MeshEffectManager() = default;
    MeshEffectManager(const MeshEffectManager&) = delete;
    MeshEffectManager& operator=(const MeshEffectManager&) = delete;

    Object3dCommon* common_ = nullptr;

    // 現在再生中のエフェクトリスト
    std::vector<std::unique_ptr<EffectObject3d>> activeEffects_;
    std::unordered_map<EffectObject3d*, EffectScopeId> effectScopes_;
    EffectScopeId nextEffectScopeId_ = 1;
    EffectObject3d* previewEffectForDebug_ = nullptr;
    std::unordered_set<std::string> preloadedEffects_;
    bool updatedThisFrame_ = false;
    float timeScale_ = 1.0f;

};
