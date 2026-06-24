#pragma once
#include "BaseEnemy.h"
#include <memory>

// 空中で距離を取り、チャージ後に直線ビームを撃つドローン敵
class EnemyBeamDrone : public BaseEnemy {
public:
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    std::unique_ptr<Object3d> Clone() const override;
    void SetCarried(bool isCarried) override;
    void ExecuteAbility(class Player* player) override;
    void UpdateCarriedAbility(class Player* player, float deltaTime) override;

private:
    // 通常時も持ち運び能力時も、待機→チャージ→ビーム発射の流れを使う
    enum class BeamState {
        Idle,
        Charge,
        Beam
    };

    void CaptureHomePosition();
    void UpdateHover(float deltaTime);
    void UpdateFacing(const Vector3& direction);
    void StartCharge();
    void FireBeam();
    void UpdateBeamVisual();
    void UpdateBeamDamage();
    void HideBeamVisuals();
    void ApplyBeamVisualTransform(Object3d* visual, const Vector3& source, const Vector3& target, float thickness, const Vector4& color, float emissive);
    bool GetVisibleBeamSegment(Vector3& outStart, Vector3& outEnd) const;
    Vector3 GetBeamMuzzlePosition(const Vector3& direction) const;
    void StartPlayerBeamCharge(class Player* player);
    void FirePlayerBeam(class Player* player);
    void UpdatePlayerBeam(class Player* player, float deltaTime);
    void UpdatePlayerBeamVisual();
    void UpdatePlayerBeamDamage(class Player* player);
    Vector3 GetPlayerBeamMuzzlePosition(class Player* player) const;
    Vector3 GetPlayerBeamDirection(class Player* player) const;
    float CalcDistancePointToSegment(const Vector3& point, const Vector3& start, const Vector3& end) const;

    std::unique_ptr<Object3d> beamVisual_;     // 外側の太い発光ビーム。
    std::unique_ptr<Object3d> beamCoreVisual_; // 中心の白いビーム芯。
    BeamState state_ = BeamState::Idle;
    Vector3 homePosition_ = { 0.0f, 0.0f, 0.0f }; // 浮遊の基準位置。
    Vector3 beamStart_ = { 0.0f, 0.0f, 0.0f };    // ビーム始点。
    Vector3 beamEnd_ = { 0.0f, 0.0f, 0.0f };      // ビーム終点。
    Vector3 beamDirection_ = { 0.0f, 0.0f, 1.0f };
    Vector3 smoothedVelocity_ = { 0.0f, 0.0f, 0.0f }; // 急な方向転換を抑えるための速度。
    float beamLength_ = 0.0f;
    float hoverTimer_ = 0.0f;
    float cooldownTimer_ = 1.0f;
    float chargeTimer_ = 0.0f;
    float beamTimer_ = 0.0f;
    float playerBeamEffectTimer_ = 0.0f;
    bool hasHomePosition_ = false;
    bool beamDamageDone_ = false;
    bool isPlayerBeamMode_ = false;
    bool playerBeamDamageDone_ = false;
};
