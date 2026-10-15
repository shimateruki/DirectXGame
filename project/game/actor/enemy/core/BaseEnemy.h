#pragma once
#include "Character.h"
#include "game/actor/enemy/combat/AttackTelegraph.h"
#include "game/actor/enemy/combat/EnemyAttackProfile.h"
#include <cstdint>
#include <memory>
#include <cstddef>

class BaseScene;

// 敵固有の移動・攻撃姿勢へ最後に重ねる、短い被弾リアクションの見た目です。
// scaleは倍率、rotationとoffsetはローカル空間の加算値です。
struct EnemyVisualReactionPose {
    engine::Vector3 scale = {1.0f, 1.0f, 1.0f};
    engine::Vector3 rotation = {0.0f, 0.0f, 0.0f};
    engine::Vector3 offset = {0.0f, 0.0f, 0.0f};
    float weight = 0.0f;
};

// BaseEnemyは、敵共通の追跡対象、徘徊、被弾、投げ物理、撃破演出を持つ基底クラスです。
class BaseEnemy : public Character {
    friend class EnemyAttackAction; // 共通攻撃部品だけが予告表示の開始・終了を操作します。
  public:
    /// <summary>敵固有のリソースを破棄できるよう、派生型を通じて破棄する。</summary>
    virtual ~BaseEnemy() = default;

    /// <summary>敵共通のモデル、Collider、色、攻撃予兆を初期化する。</summary>
    virtual void Initialize(Object3dCommon* common, const std::string& modelName);

    /// <summary>被弾タイマー、投げ物理、撃破演出など敵共通の状態を進める。</summary>
    virtual void Update(float deltaTime) override;
    /// <summary>敵本体と共通の表示物をライト情報付きで描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    /// <summary>床の不透明描画後に攻撃予兆を重ねて描画する。</summary>
    void DrawAttackTelegraph(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);

    /// <summary>弾、地形、プレイヤーとの接触に応じた共通処理を行う。</summary>
    /// <returns>衝突を処理した場合はtrue。</returns>
    virtual bool OnCollision(engine::Object3d* other) override;

    /// <summary>追跡と攻撃の対象を借用する。切替時はボスの交戦状態を解除する。</summary>
    void SetTarget(engine::Object3d* target) {
        if (target_ != target) {
            bossTargetEngaged_ = false;
        }
        target_ = target;
    }

    /// <summary>生成・復元後にシーン側の非所有参照を接続する。</summary>
    /// <remarks>召喚する敵の追加設定は派生側で行う。</remarks>
    virtual void BindSceneSpawnContext(BaseScene* scene);

    /// <summary>シーン演出中などにAI、重力、衝突を一時停止する。</summary>
    void SetDormant(bool dormant);
    /// <summary>敵が休止中か返す。</summary>
    bool IsDormant() const { return isDormant_; }

    /// <summary>ワールド単位の検知距離を取得・設定し、発見演出にも使う。</summary>
    void SetDetectionRange(float range) {
        detectionRange_ = range;
        noticeDetectionRange_ = range;
    }
    float GetDetectionRange() const { return detectionRange_; }
    /// <summary>プレイヤーに保持されている状態を設定する。</summary>
    virtual void SetCarried(bool isCarried);
    /// <summary>プレイヤーに保持されているか返す。</summary>
    bool IsCarried() const { return isCarried_; }
    /// <summary>ワールド空間の初速度を受け取り、投げ物理状態へ移る。</summary>
    virtual void BeginThrown(const engine::Vector3& initialVelocity);
    /// <summary>投げ物理が進行中か返す。</summary>
    bool IsThrownPhysics() const { return isThrownPhysics_; }
    /// <summary>撃破時の消滅演出が進行中か返す。</summary>
    bool IsDefeatEffectPlaying() const { return isDefeatEffectPlaying_; }
    /// <summary>有効な体当たり技の威力を返す。通常接触では0。</summary>
    float GetActiveBodyAttackDamage() const;
    /// <summary>フック命中後の持ち運びを拒否する敵はtrueを返す。</summary>
    virtual bool IsPullImmune() const { return false; }
    /// <summary>DamageEventを受けた敵に、攻撃方向に応じた潰れと反発を開始する。</summary>
    void PlayDamageReaction(engine::Object3d* attacker, const engine::Vector3& knockbackVelocity, float damage);
    /// <summary>被弾リアクションの現在の強さを返す。</summary>
    float GetDamageReactionWeight() const;
    /// <summary>保持中のプレイヤーから敵固有の能力を発動する。</summary>
    /// <remarks>playerの所有権は移さない。</remarks>
    virtual void ExecuteAbility(class Player* player);
    /// <summary>プレイヤーに保持されている間の能力を更新する。</summary>
    /// <param name="deltaTime">前フレームからの経過秒数。</param>
    virtual void UpdateCarriedAbility(class Player* player, float deltaTime);

    /// <summary>現在の敵種別に対応する攻撃プロファイルを再読み込みする。</summary>
    /// <param name="errorMessage">失敗理由を受け取る任意の出力先。</param>
    /// <returns>読み込みに成功した場合はtrue。</returns>
    bool ReloadAttackProfile(std::string* errorMessage = nullptr);
    /// <summary>現在適用中の攻撃プロファイルを返す。</summary>
    const EnemyAttackProfile& GetAttackProfile() const { return attackProfile_; }
    /// <summary>隔離プレビューの攻撃プロファイルを設定する。</summary>
    /// <remarks>通常の敵Actorへの未保存データの注入は拒否する。</remarks>
    void SetEditorPreviewProfile(const EnemyAttackProfile& profile) {
        if (IsEditorInternal()) attackProfile_ = profile;
    }
  protected:
    /// <summary>本体を使った攻撃の威力を返す。既定では攻撃しない。</summary>
    virtual float GetBodyAttackDamage() const { return 0.0f; }
    /// <summary>敵固有の実行状態をリプレイ用JSONへ書く。</summary>
    void CaptureReplayCustomState(json& state) const override;
    /// <summary>リプレイ用JSONから敵固有の実行状態を戻す。</summary>
    void RestoreReplayCustomState(const json& state) override;

    /// <summary>スポーン位置を基準に選んだ徘徊目標へ向かう速度を計算する。</summary>
    engine::Vector3 CalculateWanderVelocity(float deltaTime, float moveSpeed, float radiusScale = 0.65f,
                                    float verticalOffset = 0.0f, bool includeVertical = false);
    /// <summary>現在の徘徊目標をワールド座標で返す。</summary>
    engine::Vector3 GetWanderTargetPosition(float deltaTime, float radiusScale = 0.65f, float verticalOffset = 0.0f);
    /// <summary>徘徊の原点を現在位置へ取り直す。</summary>
    void ResetWanderOrigin();

    /// <summary>投げ物理または回転復帰中で、敵固有AIを止める必要があるか返す。</summary>
    bool IsThrowRecovering() const {
        return throwRecoveryTimer_ > 0.0f || isThrownPhysics_ || isThrowRotationRecovering_;
    }

    /// <summary>HPが尽きた敵に共通の撃破演出を適用するか返す。</summary>
    bool ShouldHandleDefeatEffect() const;
    /// <summary>円形の攻撃予兆を共通のAttackTelegraphへ表示する。</summary>
    void ShowAttackTelegraphCircle(const engine::Vector3& center, float radius, float progress, const engine::Vector4& color);
    /// <summary>指定テクスチャの円形デカール予兆を表示する。</summary>
    void ShowAttackTelegraphDecalCircle(const engine::Vector3& center, float radius, float progress, const engine::Vector4& color,
                                        const std::string& texturePath);
    /// <summary>中心・方向・長さ・幅から線形の攻撃予兆を表示する。</summary>
    void ShowAttackTelegraphLine(const engine::Vector3& center, const engine::Vector3& direction, float length, float width,
                                 float progress, const engine::Vector4& color);
    /// <summary>始点と方向から扇形の攻撃予兆を表示する。</summary>
    void ShowAttackTelegraphCone(const engine::Vector3& origin, const engine::Vector3& direction, float length, float startWidth,
                                 float endWidth, float progress, const engine::Vector4& color);
    /// <summary>複数の着弾位置に円形予兆を表示する。</summary>
    /// <param name="centers">centerCount個の中心座標。</param>
    void ShowAttackTelegraphImpactAreas(const engine::Vector3* centers, std::size_t centerCount, float radius, float progress,
                                        const engine::Vector4& color);
    /// <summary>視認性を高めた複数着弾位置の予兆を表示する。</summary>
    void ShowAttackTelegraphReadableImpactAreas(const engine::Vector3* centers, std::size_t centerCount, float radius,
                                                float progress, const engine::Vector4& color);
    /// <summary>指定本数と間隔で扇状のレーン予兆を表示する。</summary>
    void ShowAttackTelegraphLaneFan(const engine::Vector3& origin, const engine::Vector3& direction, float length, float width,
                                    int laneCount, float lateralSpacing, float angleStep, float progress,
                                    const engine::Vector4& color);
    /// <summary>攻撃予兆の開始合図を指定色で発生させる。</summary>
    void TriggerAttackTelegraphCue(const engine::Vector4& color = {1.0f, 0.05f, 0.02f, 1.0f});
    /// <summary>表示中の攻撃予兆を消す。</summary>
    void HideAttackTelegraph();
    /// <summary>ターゲットの発見・見失いリアクションを進める。</summary>
    /// <returns>リアクション中に通常AIを止める必要がある場合はtrue。</returns>
    bool UpdateNoticeReaction(float deltaTime, float targetDistance, float detectRange, const engine::Vector3& targetDirection);
    /// <summary>ターゲットとの距離からボスの交戦状態を更新する。</summary>
    void UpdateBossTargetEngagement(float targetDistance);
    /// <summary>ボスがターゲットと交戦中か返す。</summary>
    bool IsBossTargetEngaged() const { return bossTargetEngaged_; }
    /// <summary>進行中の発見・見失いリアクションを中断する。</summary>
    void CancelNoticeReaction();
    /// <summary>撃破時のコインを生成する。</summary>
    void SpawnDefeatCoinDrops();
    /// <summary>攻撃IDに対応する定義を現在のプロファイルから返す。</summary>
    const EnemyAttackDefinition& GetAttackDefinition(const std::string& attackId) const;
    /// <summary>現在の被弾リアクション姿勢を返す。</summary>
    EnemyVisualReactionPose GetDamageReactionPose() const;
    /// <summary>被弾リアクションを指定の姿勢と任意の位置オフセットへ重ねる。</summary>
    void ApplyDamageReactionPose(engine::Vector3& scale, engine::Vector3& rotation, engine::Vector3* offset = nullptr,
                                 float intensity = 1.0f) const;
    /// <summary>被弾色や姿勢の経過時間を更新する。</summary>
    void UpdateDamageFeedbackTimers(float deltaTime);
    /// <summary>高速落下が地面に当たった瞬間の敵固有処理を行う。</summary>
    virtual void OnSlamImpact(const engine::Vector3& impactPosition, float impactSpeed);

    engine::Object3d* target_ = nullptr;       // 追いかける対象。基本的にはプレイヤー。
    float detectionRange_ = 20.0f;     // ターゲットを検知して攻撃AIへ入る距離。
    float damageCooldownTimer_ = 0.0f; // 連続ヒットを防ぐ短い無敵時間。
    EnemyAttackProfile attackProfile_;
    engine::Vector4 defaultColor_ = {1.0f, 1.0f, 1.0f, 1.0f}; // 被弾演出後に戻す基準色。
    bool isCarried_ = false;
    float throwRecoveryTimer_ = 0.0f;

  private:
    float colorResetTimer_ = 0.0f; // 被弾時の赤色を元に戻すタイマー。
    bool isDormant_ = false;
    bool isThrownPhysics_ = false;
    enum class NoticeReactionKind : uint8_t {
        None = 0,
        Detected,
        Lost,
    };

    /// <summary>徘徊の基準位置を現在位置から保存する。</summary>
    void CaptureWanderOrigin();
    /// <summary>徘徊の半径と高さから次の目標地点を選ぶ。</summary>
    void PickWanderTarget(float radiusScale, float verticalOffset);
    /// <summary>敵ごとの徘徊乱数を一段進めて返す。</summary>
    float NextWanderRandom();
    /// <summary>基準半径へ倍率を適用した徘徊半径を返す。</summary>
    float GetWanderRadius(float radiusScale) const;

    /// <summary>投げられた敵の移動、衝突、回転を更新する。</summary>
    void UpdateThrownPhysics(float deltaTime);
    /// <summary>投げ物理中の接触を属性別に処理する。</summary>
    void HandleThrownCollision(const CollisionInfo& info, uint32_t attribute);
    /// <summary>投げ物理を終了し、必要な回転復帰へ移る。</summary>
    void EndThrownPhysics();
    /// <summary>投げ終わった敵の通常姿勢への復帰を開始する。</summary>
    void StartThrowRecovery();
    /// <summary>投げ後の回転復帰と待機時間を進める。</summary>
    void UpdateThrowRecovery(float deltaTime);

    /// <summary>高速で地面に当たった地点へ着地演出を出す。</summary>
    void SpawnSlamImpactEffect(const engine::Vector3& impactPosition, float impactSpeed);
    /// <summary>着地点周辺の対象へ衝撃ダメージを与える。</summary>
    void DamageSlamTargets(const engine::Vector3& impactPosition, float impactSpeed);
    /// <summary>発見または見失いのリアクションを開始する。</summary>
    void BeginNoticeReaction(NoticeReactionKind kind);
    /// <summary>発見リアクションを終え、必要なら元の見た目へ戻す。</summary>
    void EndNoticeReaction(bool restoreVisual = true);
    /// <summary>敵の発見状態を示すマークを必要に応じて生成する。</summary>
    void EnsureNoticeMarkObject();
    /// <summary>リアクション種別に合わせて発見マークを設定する。</summary>
    void ConfigureNoticeMarkForReaction();
    /// <summary>検知距離に応じてターゲットへの警戒状態を更新する。</summary>
    void UpdateNoticeAwareness(float deltaTime);
    /// <summary>発見・見失いリアクション中の姿勢を更新する。</summary>
    void UpdateNoticeReactionAnimation(float deltaTime);
    /// <summary>リアクション進行率に合わせて発見マークを更新する。</summary>
    void UpdateNoticeMark(float deltaTime, float progress);

    /// <summary>撃破時の縮小、色変化、粒子演出を開始する。</summary>
    void BeginDefeatEffect();
    /// <summary>撃破演出と、終了後の削除準備を進める。</summary>
    void UpdateDefeatEffect(float deltaTime);
    /// <summary>撃破開始時の粒子を発生させる。</summary>
    void SpawnDefeatStartParticles();
    /// <summary>撃破演出が続く間の粒子を発生させる。</summary>
    void SpawnDefeatLoopParticles();
    /// <summary>現在のシーンが使う粒子システムを返す。利用できなければnullptr。</summary>
    class ParticleSystem* GetCurrentParticleSystem() const;

    engine::Vector3 wanderOrigin_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 wanderTarget_ = {0.0f, 0.0f, 0.0f};
    float wanderWaitTimer_ = 0.0f;
    float wanderRetargetTimer_ = 0.0f;
    uint32_t wanderSeed_ = 0x12345678u;
    bool hasWanderOrigin_ = false;
    engine::Vector3 thrownAngularVelocity_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 throwRecoveryStartRotation_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 throwRecoveryTargetRotation_ = {0.0f, 0.0f, 0.0f};
    float thrownTimer_ = 0.0f;
    float thrownSettleTimer_ = 0.0f;
    float throwRecoveryRotateTimer_ = 0.0f;
    float slamImpactCooldownTimer_ = 0.0f;
    int slamImpactCount_ = 0;
    bool isThrowRotationRecovering_ = false;
    bool isDefeatEffectPlaying_ = false;
    bool isDefeatEffectFinished_ = false;
    bool hasSpawnedDefeatCoinDrops_ = false;
    std::unique_ptr<AttackTelegraph> attackTelegraph_;
    std::unique_ptr<engine::Object3d> noticeMarkObject_;
    bool wasTargetDetected_ = false;
    bool bossTargetEngaged_ = false;
    bool isNoticeReactionActive_ = false;
    NoticeReactionKind noticeReactionKind_ = NoticeReactionKind::None;
    float noticeReactionTimer_ = 0.0f;
    float noticeReactionCooldown_ = 0.0f;
    float noticeDetectionRange_ = 20.0f;
    float noticeMarkYaw_ = 0.0f;
    engine::Vector3 noticeBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 noticeBaseRotation_ = {0.0f, 0.0f, 0.0f};
    engine::Vector4 noticeBaseColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
    float defeatEffectTimer_ = 0.0f;
    float defeatEffectParticleTimer_ = 0.0f;
    float damageReactionTimer_ = 0.0f;
    float damageReactionDuration_ = 0.0f;
    float damageReactionStrength_ = 0.0f;
    engine::Vector3 damageReactionLocalDirection_ = {0.0f, 0.0f, -1.0f};
    engine::Vector3 defeatBasePosition_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 defeatBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector4 defeatBaseColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
};
