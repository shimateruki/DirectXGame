#pragma once
#include "BaseEnemy.h"

// プレイヤーに接近して点火する爆弾型の敵。運搬・投擲後も導火線の残り時間を引き継ぎます。
class EnemyBomb : public BaseEnemy {
  public:
    enum class State {
        Chase,   // 発見後の追跡
        Ignited, // 導火線のカウントダウン
        Exploded // 爆発済み。衝突判定と描画を停止する
    };

    EnemyBomb() = default;

    ~EnemyBomb() override = default;

    // 爆弾モデル、衝突判定、追跡状態を初期化する。
    void Initialize(engine::Object3dCommon* common, const std::string& modelName) override;

    // 追跡、導火線、投擲後の移動を状態に応じて進める。
    void Update(float deltaTime) override;

    /// 投げ始めの速度を設定し、爆弾専用の接地処理を使う。
    void BeginThrown(const engine::Vector3& initialVelocity) override;

    // プレイヤーの爆弾が敵に当たったら即座に爆発し、通常の接触は基底クラスへ渡す。
    bool OnCollision(engine::Object3d* other) override;

    // 運搬時の移動状態を切り替え、自爆能力を実行する。
    void SetCarried(bool isCarried) override;

    void ExecuteAbility(class Player* player) override;

    // 導火線を指定秒数から開始し、点滅と膨縮の演出も先頭へ戻す。
    void Ignite(float fuseTime = 3.0f);

    // 所有者に応じて敵・プレイヤーへの当たり判定マスクを切り替える。
    void SetPlayerOwned(bool playerOwned);

    bool IsPlayerOwned() const { return playerOwned_; }

    bool IsExploded() const { return state_ == State::Exploded; }

    void ApplyManagedScale(const engine::Vector3& scale) override;

  private:
    // プレイヤー追跡と導火線点火後の警告演出をそれぞれ更新する。
    void UpdateChase(float deltaTime);

    void UpdateIgnited(float deltaTime);

    // 爆発状態へ移し、所有者に応じた一時的な爆発判定と演出を発生させる。
    void Explode();

    // 重力・掃引・接地減速を同じMotorで一度ずつ処理する。
    void UpdateBombPhysics(float deltaTime);

  private:
    State state_ = State::Chase;

    // 導火線と警告演出は運搬中も更新するため、移動状態とは別に保持します。
    float fuseTimer_ = 2.0f;  // 爆発までの残り秒数
    float flashTimer_ = 0.0f; // 赤い点滅の切り替え間隔
    bool flashState_ = false; // 現在の点灯状態
    float pulseTimer_ = 0.0f; // 膨張・収縮の位相

    bool isThrown_ = false;          // 投擲後、接地して停止するまで true
    float explosionRadius_ = 3.0f;  // 点火時にエフェクト設定から決め、予告と爆発で共有する。
    bool isAbilityExecuted_ = false; // 自爆能力の多重発動防止
    bool playerOwned_ = false;
    engine::Vector3 managedBaseScale_ = {0.16f, 0.16f, 0.16f};
};
