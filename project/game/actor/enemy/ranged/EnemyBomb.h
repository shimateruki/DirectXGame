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

    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;

    void SetCarried(bool isCarried) override;
    void ExecuteAbility(class Player* player) override;
    // 導火線を指定秒数から開始し、点滅と膨縮の演出も先頭へ戻す。
    void Ignite(float fuseTime = 3.0f);
    // 所有者に応じて敵・プレイヤーへの当たり判定マスクを切り替える。
    void SetPlayerOwned(bool playerOwned);
    bool IsPlayerOwned() const { return playerOwned_; }
    void ApplyManagedScale(const Vector3& scale) override;

  private:
    void UpdateChase(float deltaTime);
    void UpdateIgnited(float deltaTime);
    void Explode();
    void ResolveSweptCollision(const Vector3& previousPosition);

  private:
    State state_ = State::Chase;

    // 導火線と警告演出は運搬中も更新するため、移動状態とは別に保持します。
    float fuseTimer_ = 2.0f;  // 爆発までの残り秒数
    float flashTimer_ = 0.0f; // 赤い点滅の切り替え間隔
    bool flashState_ = false; // 現在の点灯状態
    float pulseTimer_ = 0.0f; // 膨張・収縮の位相

    bool isThrown_ = false;          // 投擲後、接地して停止するまで true
    bool isAbilityExecuted_ = false; // 自爆能力の多重発動防止
    bool playerOwned_ = false;
    Vector3 managedBaseScale_ = {0.16f, 0.16f, 0.16f};
};
