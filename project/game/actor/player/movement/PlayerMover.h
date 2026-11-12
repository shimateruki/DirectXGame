#pragma once
#include "engine/utility/math/Math.h"
#include <memory>
#include "game/actor/player/movement/PlayerJumpRequest.h"
#include <unordered_map>

#ifdef USE_IMGUI
namespace PlayerLocomotionRuntimeProbe { struct Access; }
#endif

// 前方宣言
class Player;
namespace engine { class InputManager; }
namespace engine { class ParticleSystem; } // ジャンプエフェクト用
class IMoveStrategy;  // 戦略インターフェース
namespace engine { class Object3D; }       // 子オブジェクト用

/// <summary>
/// プレイヤーの移動制御コンポーネント
/// </summary>
// 関連：Playerの入力と移動戦略を使い、Characterの速度・接地状態へ反映する。
// 主な入口：Initialize / Update
class PlayerMover {
  public:
    PlayerMover();

    ~PlayerMover();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(Player* player, engine::InputManager* inputManager, engine::ParticleSystem* particleSystem);

    /// <summary>
    /// 毎フレームの更新（移動・回転・ジャンプ）
    /// </summary>
    void Update(float deltaTime);

    // 物理更新後の接地結果から、身体と砂煙で共有する着地強度を返します。
    float UpdateLandingFeedback(float prePhysicsVelocityY, float deltaTime, bool allowFeedback);

    // 操作を一時停止している攻撃中も、通常移動と同じ基準の入力方向を取得する。
    // 入力がない場合はゼロベクトルを返し、移動速度やプレイヤーの向きは変更しない。
    engine::Vector3 GetMoveInputDirection();

    // 移動が一時停止しているフレームも入力の寿命を進めます。
    void SampleJumpInput(float deltaTime, bool discard);

    const char* GetJumpStatus() const { return jumpStatus_; }

    void SetPlatformMotion(const engine::Vector3& velocity) { platformVelocity_ = velocity; }

    // 能力で受けた水平の押し出しを空中で短く残し、通常の移動入力と合成します。
    void SetAbilityAirImpulse(const engine::Vector3& velocity) {
        abilityAirVelocity_ = {velocity.x, 0.0f, velocity.z};
        // 非ゼロの噴射は呼び出し元が本体速度にも設定済み。次回の加速計算で二重に足さない。
        // 解除時は直前に加算した量を保持し、残った外力を通常速度として引き継がせない。
        if (velocity.x != 0.0f || velocity.z != 0.0f)
            previousAddedAirVelocity_ = abilityAirVelocity_;
    }

    /// <summary>
    /// 移動戦略（操作方法）を切り替える
    /// </summary>
    void SetStrategy(std::unique_ptr<IMoveStrategy> strategy);

    bool IsDashing() const { return isDashing_; }

    // 衝突でダッシュが途切れたら、移動速度だけでなく無敵と子の衝突属性も戻す。
    void StopDashOnImpact();

    // ワープや演出復帰時に、継続中のダッシュ・床補正・ジャンプ溜めを破棄します。
    void ResetTransientState();

    // 連続してパネルを踏んでも効果時間は短縮せず、速度だけ不足分を補う。
    void ApplyDashPanelBoost(float duration, float speedMultiplier, float turnMultiplier);

  private:
#ifdef USE_IMGUI
    friend struct PlayerLocomotionRuntimeProbe::Access;
#endif
    engine::Vector3 platformVelocity_{}; // 接地中の床から受け取る移動速度。
    engine::Vector3 platformAirVelocity_{}; // 床から離れた後も保持する床由来の速度。
    engine::Vector3 abilityAirVelocity_{}; // 空中で能力から加えられた追加速度。
    engine::Vector3 previousAddedAirVelocity_{}; // 空中攻撃の補間へ床・爆風を二重加算しないための前回加算値。
    PlayerJumpRequest jumpRequest_;
    const char* jumpStatus_ = "入力待ち";

    // プレイヤー中心ではなく接地面から発生させ、段差やジャンプ時の浮きを防ぐ。
    void EmitGroundDust(const char* presetName, float yOffset) const;

    // 参照用ポインタ
    Player* player_ = nullptr; // 更新対象のPlayerの借用参照。PlayerがこのMoverを所有する。
    engine::InputManager* inputManager_ = nullptr;
    engine::ParticleSystem* particleSystem_ = nullptr;

    // 移動戦略 (2D/3Dなど)
    std::unique_ptr<IMoveStrategy> strategy_ = nullptr;

    // --- ダッシュ回避関連 ---
    bool isDashing_ = false;
    float dashSpeed_ = 120.0f;   // ダッシュ速度
    float dashDuration_ = 0.20f; // ダッシュ継続時間（秒）
    float dashTimer_ = 0.0f;     // 残りダッシュ時間
    engine::Vector3 dashDirection_{};    // 値初期化（{0,0,0}）

    // 子オブジェクトの元の衝突属性を保存して復元するためのマップ
    std::unordered_map<engine::Object3D*, uint32_t> childOriginalAttributes_;

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
    float dashPanelSpeedMultiplier_ = 1.0f; // ダッシュパネルの速度倍率。1なら通常値。
    float dashPanelTurnMultiplier_ = 1.0f; // パネル効果中の方向転換倍率。1なら通常値。

    bool hasGroundedHistory_ = false; // 前フレームとの接地比較が可能になったか。
    bool wasGroundedLastFrame_ = false;
    float landingDustCooldown_ = 0.0f; // 着地粒子の再発生までの残り秒数。
    float airborneDuration_ = 0.0f; // 空中状態に使う経過秒数。
    float airborneMaxDownSpeed_ = 0.0f; // 着地演出の強さを決めるために記録した最大下降速度。
};
