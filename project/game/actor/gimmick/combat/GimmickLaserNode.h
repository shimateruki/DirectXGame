#pragma once
#include "BaseGimmick.h"

// 始点と対象を結ぶレーザーを描画し、線分上の対象に周期ダメージを与えます。
class GimmickLaserNode : public BaseGimmick {
  public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    bool OnCollision(Object3d* other) override;
    // 現在の起動状態を反転する。returnOnOff が無効なら停止要求は無視する。
    void OnTrigger() override;
    // スイッチイベントから状態を指定する。停止可否は returnOnOff に従う。
    void OnSwitchEvent(bool active) override;

    // 起動後は保存した基準位置、未起動時は現在のワールド位置を返す。
    Vector3 GetLaserAnchorPosition() const;

    std::unique_ptr<Object3d> Clone() const override;

  private:
    bool FindTargetPosition(Vector3& outTarget) const;
    // 表示線分と判定線分を同じ両端座標で更新します。
    void ApplyBeamTransform(const Vector3& source, const Vector3& target);
    void UpdateBeamDamage(const Vector3& source, const Vector3& target);
    float CalcDistancePointToSegment(const Vector3& point, const Vector3& start, const Vector3& end) const;
    Quaternion MakeYAxisToDirectionQuaternion(const Vector3& direction) const;
    float GetDamage() const;
    float GetDamageInterval() const;
    float GetThickness() const;

    std::unique_ptr<Object3d> beamVisual_;
    Vector3 basePosition_ = {0.0f, 0.0f, 0.0f};
    Vector3 baseRotation_ = {0.0f, 0.0f, 0.0f};
    Vector3 baseScale_ = {1.0f, 1.0f, 1.0f};
    bool initializedForPlay_ = false;
    bool active_ = true;
    float damageCooldownTimer_ = 0.0f;
    float pulseTimer_ = 0.0f;
};
