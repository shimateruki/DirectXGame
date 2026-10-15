#pragma once
#include "engine/utility/math/Math.h"
#include <memory>
#include "game/actor/player/movement/PlayerJumpRequest.h"
#include <unordered_map>

// 前方宣言
class Player;
class InputManager;
class ParticleSystem; // ジャンプエフェクト用
class IMoveStrategy;  // 戦略インターフェース
namespace engine { class Object3d; }       // 子オブジェクト用

/// <summary>
/// プレイヤーの移動制御コンポーネント
/// </summary>
class PlayerMover {
  public:
    PlayerMover();
    ~PlayerMover();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(Player* player, InputManager* inputManager, ParticleSystem* particleSystem);

    /// <summary>
    /// 毎フレームの更新（移動・回転・ジャンプ）
    /// </summary>
    void Update(float deltaTime);
    // 操作を一時停止している攻撃中も、通常移動と同じ基準の入力方向を取得する。
    // 入力がない場合はゼロベクトルを返し、移動速度やプレイヤーの向きは変更しない。
    engine::Vector3 GetMoveInputDirection();
    // 移動が一時停止しているフレームも入力の寿命を進めます。
    void SampleJumpInput(float deltaTime, bool discard);
    const char* GetJumpStatus() const { return jumpStatus_; }
    void SetPlatformMotion(const engine::Vector3& velocity) { platformVelocity_ = velocity; }

    /// <summary>
    /// 移動戦略（操作方法）を切り替える
    /// </summary>
    void SetStrategy(std::unique_ptr<IMoveStrategy> strategy);

    bool IsDashing() const { return isDashing_; }
    void StopDashOnImpact();
    // ワープや演出復帰時に、継続中のダッシュ・床補正・ジャンプ溜めを破棄します。
    void ResetTransientState();
    void ApplyDashPanelBoost(float duration, float speedMultiplier, float turnMultiplier);

  private:
    engine::Vector3 platformVelocity_{};
    engine::Vector3 platformAirVelocity_{};
    PlayerJumpRequest jumpRequest_;
    const char* jumpStatus_ = "入力待ち";
    void EmitGroundDust(const char* presetName, float yOffset) const;

    // 参照用ポインタ
    Player* player_ = nullptr;
    InputManager* inputManager_ = nullptr;
    ParticleSystem* particleSystem_ = nullptr;

    // 移動戦略 (2D/3Dなど)
    std::unique_ptr<IMoveStrategy> strategy_ = nullptr;

    // パラメータ
    float jumpPower_ = 2.0f; // ジャンプ力

    // --- ダッシュ回避関連 ---
    bool isDashing_ = false;
    float dashSpeed_ = 120.0f;   // ダッシュ速度
    float dashDuration_ = 0.20f; // ダッシュ継続時間（秒）
    float dashTimer_ = 0.0f;     // 残りダッシュ時間
    engine::Vector3 dashDirection_{};    // 値初期化（{0,0,0}）

    // 子オブジェクトの元の衝突属性を保存して復元するためのマップ
    std::unordered_map<engine::Object3d*, uint32_t> childOriginalAttributes_;

    // --- スライム挙動関連 ---
    float slimeTimer_ = 0.0f;                // 伸縮アニメーション用タイマー
    float hopTimer_ = 0.0f;                  // ホッピング（小ジャンプ）用周期タイマー
    engine::Vector3 baseScale_ = {2.0f, 2.0f, 2.0f}; // 基本スケール

    // --- チャージ（突進用）関連 ---
    bool isJumpCharging_ = false;      // 溜め中か
    float jumpChargeTimer_ = 0.0f;     // 溜め時間
    const float maxChargeTime_ = 1.0f; // 最大溜め時間
    bool hasAirDashed_ = false;        // 空中ダッシュ済みか

    // --- 動的ダッシュパラメータ ---
    float currentDashSpeed_ = 120.0f;
    float currentDashDuration_ = 0.20f;

    // --- Surface gimmick effects ---
    float dashPanelTimer_ = 0.0f;
    float dashPanelSpeedMultiplier_ = 1.0f;
    float dashPanelTurnMultiplier_ = 1.0f;

    bool hasGroundedHistory_ = false;
    bool wasGroundedLastFrame_ = false;
    float landingDustCooldown_ = 0.0f;
};
