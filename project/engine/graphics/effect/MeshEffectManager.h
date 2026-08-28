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

// エフェクトを動的に発生・管理するシングルトンクラス
// MeshEffectManagerは、EffectObject3dの生成、再生、更新、描画、プリロードを管理します。
class MeshEffectManager {
public:
    using EffectScopeId = uint64_t;
    static constexpr EffectScopeId kInvalidEffectScope = 0;
    // シングルトンインスタンスの取得
        // エンジン全体で共有するメッシュエフェクト管理インスタンスを取得します。
static MeshEffectManager* GetInstance();

    // 初期化（ゲーム開始時・シーン初期化時に1回だけ呼ぶ）
        // メッシュエフェクト生成に必要なObject3dCommonを保持します。
void Initialize(Object3dCommon* common);
        // 1フレーム内の更新済み判定など、フレーム単位の状態を初期化します。
void BeginFrame();
        // 再生前にエフェクトプリセットを読み込み、初回生成の負荷を抑えます。
void PreloadEffect(const std::string& jsonFilePath);

    // 毎フレームの更新（寿命が切れたエフェクトの自動削除も行う）
    void Update(float deltaTime);
    void UpdateEditorPreviewStep(float deltaTime);
    void SetTimeScale(float timeScale) { timeScale_ = (std::max)(0.0f, timeScale); }
    float GetTimeScale() const { return timeScale_; }

    // 描画
    void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr);
    bool RequiresSceneColorCopy() const;

    // 能力などの一時的な所有者ごとに、生成したエフェクトだけを安全に回収するためのIDです。
    EffectScopeId CreateEffectScope();
    void StopEffectScope(EffectScopeId scopeId);

        // 対象Object3dに追従するメッシュエフェクトを生成します。
void SpawnEffect(const std::string& jsonFilePath, Object3d* baseObject = nullptr, const Vector3& extOffset = { 0,0,0 }, const Vector3& extRot = { 0,0,0 }, const Vector3& extScale = { 1,1,1 });

    // ワールド座標を直接指定してエフェクトを配置する（TrailEmitter用）
    // jsonのPosition/Rotationフィールドを無視し、worldPos/worldRotをそのまま使う
        // ワールド座標を直接指定してメッシュエフェクトを生成します。
void SpawnEffectAt(const std::string& jsonFilePath, const Vector3& worldPos, const Vector3& worldRot, const Vector3& scale = { 1,1,1 });
    // 指定スコープへ所属させて生成し、能力終了時にまとめて停止できるようにします。
    void SpawnEffectAtScoped(EffectScopeId scopeId, const std::string& jsonFilePath,
        const Vector3& worldPos, const Vector3& worldRot, const Vector3& scale = { 1,1,1 });

    // 課題用: 手動コードでRing波紋エフェクト(gradationLine.png)を発生させる
    void SpawnRingWaveEffect(const Vector3& position);

    // ★課題: Cylinderを使った横UVスクロール・色アニメのポータルエフェクト
    void SpawnPortalEffect(const Vector3& position, float lifetime = 3.0f);

    // エディターの時間シーク用に、初期化状態とプリロードを維持したまま再生中のエフェクトだけを消します。
    void ClearActiveEffects() { activeEffects_.clear(); effectScopes_.clear(); previewEffectForDebug_ = nullptr; }

    // シーン切り替え時などに全てのエフェクトを消す
    // common_ も一緒にリセットし、次のSpawn時に自己修復させる
    void Clear() { activeEffects_.clear(); effectScopes_.clear(); preloadedEffects_.clear(); common_ = nullptr; }
    const std::vector<std::unique_ptr<EffectObject3d>>& GetActiveEffects() const { return activeEffects_; }
    void SetPreviewEffectForDebug(EffectObject3d* effect) { previewEffectForDebug_ = effect; }
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
