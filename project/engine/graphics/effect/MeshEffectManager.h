#pragma once
#include "EffectObject3d.h"
#include "engine/utility/math/Math.h"
#include <vector>
#include <memory>
#include <string>

// エフェクトを動的に発生・管理するシングルトンクラス
class MeshEffectManager {
public:
    // シングルトンインスタンスの取得
    static MeshEffectManager* GetInstance();

    // 初期化（ゲーム開始時・シーン初期化時に1回だけ呼ぶ）
    void Initialize(Object3dCommon* common);

    // 毎フレームの更新（寿命が切れたエフェクトの自動削除も行う）
    void Update(float deltaTime);

    // 描画
    void Draw(ID3D12Resource* pointLightResource = nullptr, ID3D12Resource* spotLightResource = nullptr);

    void SpawnEffect(const std::string& jsonFilePath, Object3d* baseObject = nullptr, const Vector3& extOffset = { 0,0,0 }, const Vector3& extRot = { 0,0,0 }, const Vector3& extScale = { 1,1,1 });

    // ワールド座標を直接指定してエフェクトを配置する（TrailEmitter用）
    // jsonのPosition/Rotationフィールドを無視し、worldPos/worldRotをそのまま使う
    void SpawnEffectAt(const std::string& jsonFilePath, const Vector3& worldPos, const Vector3& worldRot, const Vector3& scale = { 1,1,1 });

    // 課題用: 手動コードでRing波紋エフェクト(gradationLine.png)を発生させる
    void SpawnRingWaveEffect(const Vector3& position);

    // ★課題: Cylinderを使った横UVスクロール・色アニメのポータルエフェクト
    void SpawnPortalEffect(const Vector3& position, float lifetime = 3.0f);

    // シーン切り替え時などに全てのエフェクトを消す
    // common_ も一緒にリセットし、次のSpawn時に自己修復させる
    void Clear() { activeEffects_.clear(); common_ = nullptr; }
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
    EffectObject3d* previewEffectForDebug_ = nullptr;

};