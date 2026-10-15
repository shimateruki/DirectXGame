#pragma once

#include "engine/utility/math/Math.h"
#include "game/actor/combat/AttackSequenceData.h"

#include <string>
#include <vector>

// 攻撃の実行順。Ready は次の攻撃を開始できる待機状態です。
enum class AttackAbilityPhase {
    Ready,
    Windup,
    Active,
    Recovery,
    Cooldown,
};

/// <summary>敵の攻撃1種類を定義します。JSON、Editor、実行時で同じ値を共有します。</summary>
struct EnemyAttackDefinition {
    std::string id;
    std::string displayName;
    float selectionWeight = 1; // 重み付き抽選で使用。0なら抽選対象外。
    AttackSequenceDefinition sequence; // 共通命令で実行する攻撃。無効なら従来の専用処理を使います。

    // 射程と判定形状。位置・サイズは攻撃者のローカル空間で指定します。
    float minRange = 0.0f;
    float maxRange = 10.0f;
    std::string hitShape = "sphere";
    engine::Vector3 hitOffset = {0.0f, 0.0f, 0.0f};
    engine::Vector3 hitSize = {1.0f, 1.0f, 1.0f};
    float radius = 1.0f;

    // フェーズ時間と予告。すべて秒単位です。
    float windupDuration = 0.3f;
    float activeDuration = 0.2f;
    float recoveryDuration = 0.3f;
    float cooldown = 1.0f;
    float cancelWindowStart = 0.0f;
    float cancelWindowEnd = 0.0f;
    float warningLeadTime = 0.15f;

    // 命中時のダメージ・ノックバック・状態異常です。速度はワールド単位/秒、時間は秒です。
    float damage = 1.0f;
    engine::Vector3 knockbackVelocity = {0.0f, 0.0f, 0.0f};
    float invincibilityDuration = 0.0f;
    std::string statusEffectType;
    float statusDuration = 0.0f;
    float statusTickInterval = 0.5f;
    float statusTickDamage = 0.0f;
    std::string statusVfx;
    float minSpeed = 0.0f;
    float maxSpeed = 0.0f;
    float lifetime = 0.0f;

    // 敵攻撃プレビューで使う標的距離（ワールド単位）と再生時間（秒）です。
    float recommendedTargetDistance = 5.0f;
    float previewDuration = 4.0f;

    std::string animation;
    std::string windupVfx;
    std::string activeVfx;
    std::string impactVfx;
    std::string audioCue;
    std::string feedbackCue;

    /// <summary>指定フェーズの長さを取得します。Ready は 0 秒です。</summary>
    float GetPhaseDuration(AttackAbilityPhase phase) const;
    /// <summary>フェーズ開始からの経過秒数を 0～1 の進行率に変換します。</summary>
    float GetPhaseProgressFromElapsed(AttackAbilityPhase phase, float elapsed) const;
    /// <summary>フェーズ終了までの残り秒数を 0～1 の進行率に変換します。</summary>
    float GetPhaseProgressFromRemaining(AttackAbilityPhase phase, float remaining) const;
    /// <summary>予備動作から硬直までの合計時間を返します。</summary>
    float GetActionDuration() const;
    /// <summary>再使用待ちも含む合計時間を返します。</summary>
    float GetTotalDuration() const;
    /// <summary>攻撃開始からの経過秒数がキャンセル可能区間に入っているか調べます。</summary>
    bool IsCancelWindowOpen(float elapsedActionTime) const;
    /// <summary>ノックバックが設定されているか調べます。</summary>
    bool HasConfiguredKnockback() const;
    /// <summary>前方を基準にローカルノックバックをワールド方向へ変換します。</summary>
    engine::Vector3 ResolveKnockback(const engine::Vector3& forward, const engine::Vector3& legacyFallback) const;
    /// <summary>無敵時間が未設定なら従来値を使います。</summary>
    float ResolveInvincibilityDuration(float legacyFallback = 1.0f) const;
};

using AttackAbilityDefinition = EnemyAttackDefinition;

// 旧プロファイルと新しい弾部品を同じ規則で解決します。Editor・実行・テストで共有します。
inline AttackProjectileComponent ResolveProjectileComponent(const EnemyAttackDefinition& attack, const AttackSequenceEvent& event) {
    if (event.projectile.configured) return event.projectile;
    AttackProjectileComponent part;
    part.configured = true;
    // 旧弾は表示倍率もColliderへ掛かるため、同じワールド半径に変換して引き継ぎます。
    part.radius = attack.radius * attack.sequence.visualScale; part.lifetime = attack.lifetime;
    part.minSpeed = attack.minSpeed; part.maxSpeed = attack.maxSpeed; part.damage = attack.damage;
    part.style = attack.sequence.projectileStyle; part.visualScale = attack.sequence.visualScale;
    part.appearance = attack.sequence.fireAppearance;
    if (part.style != "fire") {
        part.trail.clear(); part.secondaryTrail.clear(); part.impact.clear(); part.secondaryImpact.clear();
        part.appearance.red = part.appearance.green = part.appearance.blue = part.appearance.alpha = part.appearance.emissive = 1;
    }
    return part;
}

/// <summary>1フレームの更新で通過したフェーズ境界を知らせます。</summary>
struct AttackAbilityUpdateResult {
    AttackAbilityPhase previousPhase = AttackAbilityPhase::Ready;
    AttackAbilityPhase currentPhase = AttackAbilityPhase::Ready;
    bool enteredActive = false;
    bool enteredRecovery = false;
    bool enteredCooldown = false;
    bool becameReady = false;
};

/// <summary>攻撃定義を値として保持し、時間経過とフェーズ遷移を管理します。</summary>
class AttackAbilityRuntime {
  public:
    /// <summary>待機中なら攻撃を開始します。</summary>
    /// <param name="definition">実行中に保持する攻撃定義。</param>
    /// <returns>開始できた場合は true。</returns>
    bool Start(const AttackAbilityDefinition& definition);
    /// <summary>経過時間を進め、通過したフェーズ境界を返します。</summary>
    /// <param name="deltaTime">経過秒数。負数は 0 として扱います。</param>
    AttackAbilityUpdateResult Update(float deltaTime);
    /// <summary>現在の攻撃を中断し、必要なら再使用待ちへ移します。</summary>
    void Cancel(bool enterCooldown = true);
    /// <summary>攻撃定義と進行状態を破棄します。</summary>
    void Reset();

    AttackAbilityPhase GetPhase() const { return phase_; }
    float GetPhaseProgress() const;
    float GetElapsedActionTime() const;
    bool IsReady() const { return phase_ == AttackAbilityPhase::Ready; }
    bool IsHitWindowOpen() const { return phase_ == AttackAbilityPhase::Active; }
    bool CanCancel() const;
    const AttackAbilityDefinition* GetDefinition() const { return hasDefinition_ ? &definition_ : nullptr; }

  private:
    // Ready へ戻るまでの固定したフェーズ順を返します。
    AttackAbilityPhase NextPhase(AttackAbilityPhase phase) const;

    // 実行中の定義は元アセットへの参照を持たず、リロード後も有効です。
    AttackAbilityDefinition definition_{};
    AttackAbilityPhase phase_ = AttackAbilityPhase::Ready;
    float phaseTime_ = 0.0f;
    bool hasDefinition_ = false;
};

/// <summary>敵1種類の攻撃をJSONで読み書きします。読込失敗時は既定値を保持します。</summary>
class EnemyAttackProfile {
  public:
    // 敵種・入力割当から独立した攻撃内容。bindingとpriorityは利用先の値を維持します。
    static bool SaveSequenceAsset(const std::string& path, const AttackSequenceDefinition& sequence, std::string* error = nullptr);
    static bool LoadSequenceAsset(const std::string& path, AttackSequenceDefinition& sequence, std::string* error = nullptr);
    /// <summary>敵種に対応する既定の攻撃群を生成します。</summary>
    static EnemyAttackProfile CreateDefault(const std::string& enemyType);
    /// <summary>敵種名から標準JSONパスを組み立てます。</summary>
    static std::string GetDefaultPath(const std::string& enemyType);
    /// <summary>成功した読込結果を共有キャッシュから取得します。失敗時は既定値を返します。</summary>
    static bool LoadCachedForEnemy(const std::string& enemyType, EnemyAttackProfile& destination,
                                   std::string* errorMessage = nullptr);
    /// <summary>指定敵種、または全敵種のキャッシュを無効化します。</summary>
    static void InvalidateCache(const std::string& enemyType = {});

    /// <summary>標準パスから読み込み、失敗時は既定値に戻します。</summary>
    bool LoadForEnemy(const std::string& enemyType, std::string* errorMessage = nullptr);
    /// <summary>任意パスのJSONを既定値へマージして読み込みます。</summary>
    bool LoadFromFile(const std::string& path, std::string* errorMessage = nullptr);
    /// <summary>Editorで調整した攻撃群をJSONに保存します。</summary>
    bool SaveToFile(const std::string& path, std::string* errorMessage = nullptr) const;

    // ID で攻撃を探します。見つからなければ nullptr です。
    const EnemyAttackDefinition* FindAttack(const std::string& attackId) const;
    EnemyAttackDefinition* FindAttack(const std::string& attackId);
    /// <summary>範囲・時間・判定形状を実行可能な値へ補正します。</summary>
    void Sanitize();
    // rollはテスト用の[0,1]値。負なら内部の乱数を使用。プレビュー指定は抽選より優先します。
    const EnemyAttackDefinition* SelectSequence(float distance, float detectionRange,
        const std::string& previewId = {}, float roll = -1) const;

    // JSONの識別情報と攻撃一覧です。
    int version = 1;
    std::string enemyType;
    std::string selectionMode = "priority"; // priority / uniform / weighted。共通enemy攻撃の候補内だけ。
    std::string displayName;
    std::vector<EnemyAttackDefinition> attacks;
};

using AttackAbilityAsset = EnemyAttackProfile;
