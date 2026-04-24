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

    // シーン切り替え時などに全てのエフェクトを消す
    void Clear() { activeEffects_.clear(); }
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