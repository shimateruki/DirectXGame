#pragma once

#include "BaseEnemy.h"

#include <array>
#include <memory>
#include <random>
#include <string>
#include <vector>

class EffectObject3d;
class BossChantVisual;

/// <summary>ステージ3の最終ボス。3形態の攻撃と王冠ギミックを管理します。</summary>
class EnemyFalseKingSlime final : public BaseEnemy {
  public:
    /// <summary>ボスの攻撃選択と形態遷移の初期状態を作る。</summary>
    EnemyFalseKingSlime();
    /// <summary>所有する攻撃演出と封印結晶を破棄する。</summary>
    ~EnemyFalseKingSlime() override;

    /// <summary>モデル、攻撃プロファイル、初期姿勢と演出資源を準備する。</summary>
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    /// <summary>登場、形態遷移、攻撃、撃破の各状態を進める。</summary>
    void Update(float deltaTime) override;
    /// <summary>ボス本体と所有する攻撃表示物を描画する。</summary>
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;
    /// <summary>指定カメラ向けにボス本体と攻撃表示物を描画する。</summary>
    void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource,
                       int previewBufferIndex = 0) override;
    /// <summary>スイッチイベントを受け、ボスの有効状態を切り替える。</summary>
    void OnSwitchEvent(bool active) override;
    /// <summary>Editor配置用にボスの設定を複製する。</summary>
    std::unique_ptr<engine::Object3d> Clone() const override;
    /// <summary>管理スケールをボス本体と関連表示へ適用する。</summary>
    void ApplyManagedScale(const engine::Vector3& scale) override;
    /// <summary>フック命中による持ち運びを拒否する。</summary>
    bool IsPullImmune() const override { return true; }

    /// <summary>特殊マテリアルで描く所有表示物があるか返す。</summary>
    bool HasOwnedSpecialMaterialVisuals() const override;
    /// <summary>所有表示物の特殊マテリアルパスを描画する。</summary>
    void DrawOwnedSpecialMaterialVisuals(uint32_t depthSrvHandle, uint32_t grabSrvHandle) override;
    /// <summary>指定カメラ向けに所有表示物の特殊マテリアルパスを描画する。</summary>
    void DrawOwnedSpecialMaterialVisualsForCamera(Camera* camera, uint32_t depthSrvHandle, uint32_t grabSrvHandle,
                                                  int previewBufferIndex = 0) override;

    /// <summary>敵攻撃プレビューで指定技だけを繰り返します。</summary>
    /// <param name="attackId">攻撃プロファイルのID。空文字で通常AIに戻します。</param>
    void SetDebugPreviewAttackId(const std::string& attackId);
    /// <summary>現在の攻撃段階をデバッグ表示用の名前で返す。</summary>
    const char* GetDebugAttackPhaseName() const;
    /// <summary>現在の攻撃本体をデバッグ表示用の名前で返す。</summary>
    const char* GetDebugAttackBodyName() const;
    /// <summary>現在表示されている攻撃演出Objectの数を返す。</summary>
    size_t GetDebugVisibleAttackVisualCount() const;

    /// <summary>戦闘HUDを表示すべき交戦段階か返す。</summary>
    bool IsEncounterHudActive() const;
    /// <summary>戦闘HUDに表示する現在HPを返す。</summary>
    float GetEncounterCurrentHp() const;
    /// <summary>戦闘HUDに表示する最大HPを返す。</summary>
    float GetEncounterMaximumHp() const;
    /// <summary>登場演出の進行率を返す。</summary>
    float GetEncounterAppearanceProgress() const;
    /// <summary>現在の戦闘形態番号を返す。</summary>
    int GetBattlePhase() const { return battlePhase_; }
    /// <summary>形態切り替え演出の進行率を返す。</summary>
    float GetPhaseTransitionProgress() const;
    /// <summary>現在形態のデバッグ撃破を要求する。</summary>
    void TriggerDebugDefeat();
    /// <summary>第2形態へデバッグ遷移できる条件を満たすか返す。</summary>
    bool CanTriggerDebugPhaseTwoTransition() const;
    /// <summary>王冠封印の幕間をデバッグ開始できるか返す。</summary>
    bool CanTriggerDebugCrownInterlude() const;
    /// <summary>最終撃破をデバッグ開始できるか返す。</summary>
    bool CanTriggerDebugFinalDefeat() const;
    /// <summary>条件を満たす場合に第2形態へデバッグ遷移する。</summary>
    void TriggerDebugPhaseTwoTransition();
    /// <summary>条件を満たす場合に王冠封印の幕間をデバッグ開始する。</summary>
    void TriggerDebugCrownInterlude();
    /// <summary>登場カット中は通常攻撃を抑止する。</summary>
    void SetIntroPresentationActive(bool active) { introPresentationActive_ = active; }
    /// <summary>形態を飛ばさないよう、HP変更前に最終戦の被ダメージを調整する。</summary>
    float FilterFinaleDamage(float damage);
    /// <summary>王冠の防護を開き、封印解除の演出へ進める。</summary>
    void OpenCrownArmor();
    /// <summary>指定位置を中心に最終戦の闘技場を開始する。</summary>
    void BeginFinalArena(const engine::Vector3& position);
    /// <summary>王冠封印から最終戦までの進行を中断・片付ける。</summary>
    void AbortCrownFinale();
    /// <summary>HPが0でも演出完了まで撃破確定を保留する。</summary>
    void HoldFinalDefeat();
    /// <summary>ムービー後に最終撃破を確定する。</summary>
    void CompleteFinalDefeat();
    /// <summary>保留中の最終撃破を取り消す。</summary>
    void CancelFinalDefeat();
    /// <summary>最終形態に入り、まだ撃破が確定していないか返す。</summary>
    bool IsFinalDefeatPending() const { return battlePhase_ == 3 && !isDead && !finalDefeatCompleted_; }
    /// <summary>王冠封印の幕間が進行中か返す。</summary>
    bool IsCrownInterlude() const { return crownInterlude_; }
    /// <summary>破壊された王冠封印の数を返す。</summary>
    int GetBrokenSealCount() const { return brokenSeals_; }
    /// <summary>最後に壊れた封印の位置を、王冠の亀裂演出用に返す。</summary>
    engine::Vector3 GetLastBrokenCrownSealPosition() const;
    /// <summary>第2形態の詠唱残り秒数をHUD向けに返す。</summary>
    float GetSealCastRemainingTime() const;
    /// <summary>最終戦演出のEditor調整UIを描画する。</summary>
    void DrawFinaleEditor();

  private:
    bool finalDefeatHeld_ = false;
    bool finalDefeatCompleted_ = false;
    enum class EncounterState {
        Normal,
        Dormant,
        Appearing,
        Active,
    };

    enum class AttackKind {
        None,
        CrownLanceRain,
        RoyalShockwave,
        KingRush,
        RoyalCross,
        CrownDominion,
        UmbraFlame,
        AstralReaver,
        NightfallLances,
    };

    enum class AttackState {
        Idle,
        Windup,
        Active,
        Recovery,
        PhaseShift,
    };

    // 槍一本の予告・飛翔・着弾・消滅を追跡する。表示 Object を所有し、各 duration は秒。
    struct CrownLance {
        std::unique_ptr<engine::Object3d> visual;
        std::unique_ptr<engine::Object3d> aura;
        std::unique_ptr<engine::Object3d> portal;
        engine::Vector3 groundPosition{};
        engine::Vector3 portalPosition{};
        engine::Vector3 flightDirection{0.0f, -1.0f, 0.0f};
        float age = 0.0f;
        float delay = 0.0f;
        float portalOpenDuration = 0.24f;
        float readyDuration = 0.22f;
        float flightDuration = 0.54f;
        float portalCloseDuration = 0.24f;
        float impactHoldDuration = 0.10f;
        float dissolveDuration = 0.42f;
        float particleTimer = 0.0f;
        bool impactPlayed = false;
        bool damageApplied = false;
    };

    // 拡大する衝撃波の表示と前回半径。hitTarget で同じ波による重複命中を抑える。
    struct RoyalWave {
        std::unique_ptr<engine::Object3d> visual;
        std::unique_ptr<engine::Object3d> glow;
        engine::Vector3 center{};
        float age = 0.0f;
        float delay = 0.0f;
        float lifetime = 1.05f;
        float startRadius = 1.0f;
        float endRadius = 28.0f;
        float previousRadius = 1.0f;
        float particleTimer = 0.0f;
        bool alternateColor = false;
        bool hitTarget = false;
    };

    // 回転ビームの表示と寿命・再ダメージ用時計。angle と angularSpeed は回転計算に使う。
    struct RoyalBeam {
        std::unique_ptr<engine::Object3d> visual;
        std::unique_ptr<engine::Object3d> glow;
        engine::Vector3 origin{};
        float age = 0.0f;
        float delay = 0.0f;
        float lifetime = 3.0f;
        float angle = 0.0f;
        float angularSpeed = 0.0f;
        float length = 31.0f;
        float width = 1.5f;
        float damageTimer = 0.0f;
    };

    // 地面の予告印と炎を所有し、全体の経過と発動後の経過・ダメージ間隔を分けて保持する。
    struct UmbraFlamePortal {
        std::unique_ptr<EffectObject3d> sigil;
        std::unique_ptr<engine::Object3d> flame;
        engine::Vector3 groundPosition{};
        float delay = 0.0f;
        float age = 0.0f;
        float activeAge = 0.0f;
        float particleTimer = 0.0f;
        float damageTimer = 0.0f;
    };

    // 飛翔エフェクト一組と始終点。launchDelay・flightDuration・trailTimer は秒で扱う。
    struct AstralReaverOrb {
        std::unique_ptr<EffectObject3d> halo;
        std::unique_ptr<EffectObject3d> blade;
        std::unique_ptr<EffectObject3d> arc;
        std::unique_ptr<EffectObject3d> core;
        engine::Vector3 origin{};
        engine::Vector3 destination{};
        float launchDelay = 0.0f;
        float flightDuration = 0.68f;
        float trailTimer = 0.0f;
    };

    // 大槍の着弾演出を構成する表示部品をまとめて所有し、攻撃終了時の片付けを揃える。
    struct NightfallLancesVisual {
        // 一本の大槍が着地した後、エネルギーを収束させて範囲攻撃を放ちます。
        std::unique_ptr<EffectObject3d> skySigil;
        std::unique_ptr<EffectObject3d> skyCounterSigil;
        std::unique_ptr<EffectObject3d> skyOuterRing;
        std::unique_ptr<EffectObject3d> skyInnerRing;
        std::unique_ptr<EffectObject3d> targetSigil;
        std::unique_ptr<EffectObject3d> targetRing;
        std::unique_ptr<EffectObject3d> innerRing;
        std::unique_ptr<EffectObject3d> spearCore;
        std::unique_ptr<EffectObject3d> spearGlow;
        std::array<std::unique_ptr<EffectObject3d>, 3> barbs;
        std::unique_ptr<EffectObject3d> impactRing;
        std::unique_ptr<EffectObject3d> impactCore;
        std::unique_ptr<EffectObject3d> groundArc;
        std::array<std::unique_ptr<EffectObject3d>, 6> groundLightning;
        std::unique_ptr<EffectObject3d> impactBeam;
        std::unique_ptr<EffectObject3d> impactBeamHalo;
        std::array<std::unique_ptr<EffectObject3d>, 3> impactLightning;
        std::unique_ptr<EffectObject3d> blastRing;
        std::unique_ptr<EffectObject3d> blastShell;
        float activeAge = 0.0f;
        float particleTimer = 0.0f;
        float skyParticleTimer = 0.0f;
        float dissolveParticleTimer = 0.0f;
        bool damageApplied = false;
    };

    // 第2形態の主槍を囲む小槍。着弾時刻をずらして回避経路を作ります。
    struct SealNightfallSatellite {
        std::unique_ptr<EffectObject3d> sigil;
        std::unique_ptr<EffectObject3d> core;
        std::unique_ptr<EffectObject3d> glow;
        engine::Vector3 ground{};
        float impactAge = 0.0f;
        bool damageApplied = false;
    };

    /// <summary>配置時の基準スケールを一度だけ保存する。</summary>
    void EnsureBaseScale();
    /// <summary>標的との距離から移動と攻撃開始を決め、速度を出力する。</summary>
    void UpdateBehavior(float deltaTime, const engine::Vector3& targetDirection, float targetDistance, engine::Vector3& velocity);

    /// <summary>技を確定し、攻撃方向を固定して予備動作を開始します。</summary>
    /// <param name="kind">今回使用する技。</param>
    /// <param name="targetDirection">攻撃開始時の標的方向。</param>
    void StartAttack(AttackKind kind, const engine::Vector3& targetDirection);

    /// <summary>攻撃の予備・発動・硬直を進め、技固有の処理へ振り分ける。</summary>
    void UpdateAttack(float deltaTime, const engine::Vector3& targetDirection, float targetDistance, engine::Vector3& velocity);
    /// <summary>攻撃前の予備動作と予告表示を更新する。</summary>
    void UpdateWindup(float deltaTime, const engine::Vector3& targetDirection);
    /// <summary>予備動作を終え、攻撃の有効期間へ移る。</summary>
    void BeginActive();
    /// <summary>現在の技の攻撃本体を更新する。</summary>
    void UpdateActive(float deltaTime, const engine::Vector3& targetDirection, engine::Vector3& velocity);
    /// <summary>攻撃の有効期間を終え、硬直へ移る。</summary>
    void BeginRecovery();
    /// <summary>攻撃一回分の状態と予告表示を片付ける。</summary>
    void FinishAttack();

    /// <summary>現在の形態の未使用技から、射程内の技を優先して選びます。</summary>
    /// <param name="targetDistance">標的までの距離。</param>
    /// <returns>今回使用する技。</returns>
    AttackKind ResolveAutomaticAttack(float targetDistance);

    /// <summary>一巡した攻撃候補の袋を次の選択用に準備する。</summary>
    void PrepareAttackCycle();
    /// <summary>プレビューで指定された攻撃種別を返す。</summary>
    AttackKind ResolveDebugAttack() const;
    /// <summary>攻撃種別に対応するプロファイルIDを返す。</summary>
    const char* GetAttackId(AttackKind kind) const;
    /// <summary>現在の攻撃に対応するプロファイル定義を返す。</summary>
    const EnemyAttackDefinition& GetCurrentAttackDefinition() const;

    /// <summary>HPと演出状態から移るべき戦闘形態を返す。</summary>
    int ResolveDesiredBattlePhase() const;
    /// <summary>指定形態への切り替え演出を開始する。</summary>
    void BeginPhaseShift(int nextPhase);
    /// <summary>形態切り替え演出を進め、その間の移動速度を調整する。</summary>
    void UpdatePhaseShift(float deltaTime, engine::Vector3& velocity);

    /// <summary>王冠槍の着弾候補位置を攻撃パターン別に用意する。</summary>
    void PrepareLanceTargets(bool dominionPattern);
    /// <summary>次の着弾候補に王冠槍を一本生成する。</summary>
    void SpawnNextCrownLance(bool dominionPattern = false);
    /// <summary>指定地点に遅延付きの王冠槍と予告表示を生成する。</summary>
    void SpawnCrownLance(const engine::Vector3& groundPosition, float delay, float readyDuration = 0.22f);
    /// <summary>王冠槍の予告、飛翔、着弾、消滅を更新する。</summary>
    void UpdateCrownLances(float deltaTime);

    /// <summary>王冠衝撃波の表示物を生成する。</summary>
    void SpawnRoyalWaves();
    /// <summary>衝撃波の拡大と命中判定を更新する。</summary>
    void UpdateRoyalWaves(float deltaTime);
    /// <summary>衝撃波を伴うジャンプを開始する。</summary>
    void BeginShockwaveJump();
    /// <summary>ジャンプの軌道と着地時刻を更新する。</summary>
    void UpdateShockwaveJump(float deltaTime, engine::Vector3& velocity);
    /// <summary>ジャンプ着地時の衝撃演出と判定を処理する。</summary>
    void FinishShockwaveLanding();
    /// <summary>段階的な突進攻撃を開始する。</summary>
    void BeginRush();
    /// <summary>指定番号の突進段階を開始する。</summary>
    void BeginRushStep(int step);
    /// <summary>突進の移動と命中判定を進める。</summary>
    void UpdateRush(float deltaTime, const engine::Vector3& targetDirection, engine::Vector3& velocity);
    /// <summary>通常またはDominion用の回転ビームを生成する。</summary>
    void SpawnRoyalBeams(bool dominionPattern);
    /// <summary>ビームの回転、寿命、再命中間隔を更新する。</summary>
    void UpdateRoyalBeams(float deltaTime);
    /// <summary>Dominion攻撃に含まれる槍とビームを更新する。</summary>
    void UpdateDominion(float deltaTime);

    /// <summary>Umbra Flamesの攻撃位置と予告表示を準備する。</summary>
    void PrepareUmbraFlames();
    /// <summary>Umbra Flamesの地面予告と炎を更新する。</summary>
    void UpdateUmbraFlames(float deltaTime, bool active);
    /// <summary>炎を結ぶ演出と攻撃判定を更新する。</summary>
    void UpdateUmbraFlameNetwork(float deltaTime, bool active);
    /// <summary>第2形態の詠唱と期限切れ時の進行を更新する。</summary>
    void UpdateSpellChant(float deltaTime);
    /// <summary>Astral Reaverの飛翔物と目標演出を準備する。</summary>
    void PrepareAstralReaver();
    /// <summary>Astral Reaverの飛翔、着弾、命中判定を更新する。</summary>
    void UpdateAstralReaver(float deltaTime, bool active);
    /// <summary>Nightfall Lancesの大槍と予告演出を準備する。</summary>
    void PrepareNightfallLances();
    /// <summary>Nightfall Lancesの着弾、範囲攻撃、消滅を更新する。</summary>
    void UpdateNightfallLances(float deltaTime, bool active);

    /// <summary>第2形態で大槍を囲む小槍を準備する。</summary>
    void PrepareSealNightfallSatellites();
    /// <summary>周囲の小槍を時間差で着弾させる。</summary>
    void UpdateSealNightfallSatellites(float deltaTime, bool active);

    /// <summary>指定モデルと材質設定の王冠攻撃用Objectを生成する。</summary>
    std::unique_ptr<engine::Object3d> CreateRoyalVisual(const std::string& name, const std::string& model, int materialType,
                                                const engine::Vector4& color, float emissive) const;
    /// <summary>指定モデルの王冠攻撃用Meshを生成する。</summary>
    std::unique_ptr<engine::Object3d> CreateRoyalMesh(const std::string& name, const std::string& model, const engine::Vector4& color,
                                              float emissive) const;
    /// <summary>指定位置の真下にある地面の着弾点を探す。</summary>
    engine::Vector3 FindGroundPoint(const engine::Vector3& samplePosition) const;

    /// <summary>地面上の円形範囲内にいる標的へ攻撃を適用します。</summary>
    /// <param name="center">判定の中心。</param>
    /// <param name="radius">水平面の判定半径。</param>
    /// <param name="verticalTolerance">中心から許容する高さの差。</param>
    /// <param name="damage">与えるダメージ。</param>
    /// <param name="damageType">攻撃属性。</param>
    /// <param name="knockback">適用するノックバック。</param>
    void DamageTargetAt(const engine::Vector3& center, float radius, float verticalTolerance, float damage,
                        DamageType damageType, const engine::Vector3& knockback);
    /// <summary>ビームの位置と再命中間隔から標的へのダメージを判定する。</summary>
    void DamageTargetOnBeam(RoyalBeam& beam, float deltaTime);

    /// <summary>指定方向へボス本体の向きを追従させる。</summary>
    void UpdateFacing(const engine::Vector3& direction, float follow = 0.12f);
    /// <summary>攻撃段階に対応する本体アニメーションを適用する。</summary>
    void ApplyBossAnimation(float deltaTime);
    /// <summary>形態と攻撃状態に合わせた発光を更新する。</summary>
    void UpdateRoyalGlow(float deltaTime);
    /// <summary>指定位置に登録済み粒子プリセットを発生させる。</summary>
    void EmitPreset(const char* presetName, const engine::Vector3& position) const;
    /// <summary>方向と速度倍率を指定して粒子プリセットを発生させる。</summary>
    void EmitDirectedPreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                            float speedScale) const;
    /// <summary>攻撃中だけ保持する表示物をまとめて破棄する。</summary>
    void ClearTransientVisuals();

    /// <summary>休止・登場・交戦の演出状態を更新する。</summary>
    /// <returns>登場演出中など、通常攻撃を止める必要がある場合はtrue。</returns>
    bool UpdateEncounterState(float deltaTime);
    /// <summary>Editorで指定された本体の姿勢と描画状態を保存する。</summary>
    void CaptureAuthoredState(bool force = false);
    /// <summary>戦闘開始前の登場・休止状態を初期化する。</summary>
    void InitializeEncounterState();
    /// <summary>Editorプレビュー用にボスの戦闘状態を初期化する。</summary>
    void ResetForEditor();
    /// <summary>休止状態を終え、登場演出を開始する。</summary>
    void BeginAppearance();
    /// <summary>登場前の休止用の姿勢と衝突設定を適用する。</summary>
    void ApplyDormantState();
    /// <summary>登場演出を完了し、通常の戦闘状態へ移る。</summary>
    void FinishAppearance();
    /// <summary>現在の姿勢とスケールに衝突範囲を同期する。</summary>
    void SyncCollisionBounds();
    /// <summary>登場演出による戦闘制御が有効か返す。</summary>
    bool IsEncounterControlled() const;
    /// <summary>登場演出の再生時間を秒で返す。</summary>
    float GetAppearanceDuration() const;

    AttackKind currentAttack_ = AttackKind::None;
    AttackState attackState_ = AttackState::Idle;
    engine::Vector3 baseScale_{1.0f, 1.0f, 1.0f};
    engine::Vector3 lockedDirection_{0.0f, 0.0f, 1.0f};
    engine::Vector3 lockedTargetPosition_{};
    engine::Vector3 attackStartPosition_{};
    engine::Vector3 attackEndPosition_{};
    std::array<engine::Vector3, 18> lanceTargets_{};
    std::vector<CrownLance> crownLances_;
    std::vector<RoyalWave> royalWaves_;
    std::vector<RoyalBeam> royalBeams_;
    std::vector<UmbraFlamePortal> umbraFlames_;
    std::unique_ptr<EffectObject3d> umbraFlameNetwork_;
    std::unique_ptr<BossChantVisual> spellChant_;
    bool umbraFlameFanHit_ = false;
    std::array<AstralReaverOrb, 3> astralReaverOrbs_;
    std::array<std::unique_ptr<EffectObject3d>, 3> astralReaverImpactRings_;
    std::unique_ptr<EffectObject3d> astralReaverTargetRing_;
    std::unique_ptr<EffectObject3d> astralReaverImpactCore_;
    float astralReaverElapsed_ = 0.0f;
    float astralReaverImpactAge_ = -1.0f;
    bool astralReaverDamageApplied_ = false;
    NightfallLancesVisual nightfallLances_;
    std::array<SealNightfallSatellite, 3> sealNightfallSatellites_;
    std::unique_ptr<engine::Object3d> shockwaveChargeVisual_;
    std::unique_ptr<engine::Object3d> rushWingsVisual_;
    std::unique_ptr<engine::Object3d> dominionSigilVisual_;

    float attackTimer_ = 0.0f;
    float attackStateDuration_ = 0.0f;
    float attackCooldown_ = 1.2f;
    float actionTimer_ = 0.0f;
    float effectTimer_ = 0.0f;
    float phaseTransitionTimer_ = 0.0f;
    float phaseTransitionDuration_ = 1.55f;
    float rushStepTimer_ = 0.0f;
    float rushStepDuration_ = 0.82f;
    float shockwaveJumpTimer_ = 0.0f;
    float shockwaveJumpDuration_ = 0.92f;
    float shockwaveJumpHeight_ = 6.4f;
    float dominionLanceTimer_ = 0.0f;
    float ambientSparkTimer_ = 0.0f;
    int actionIndex_ = 0;
    int lanceSpawnSerial_ = 0;
    int automaticAttackSerial_ = 0;
    // 一巡するまで技を再選択しないシャッフル袋です。
    std::array<AttackKind, 8> attackCycle_{};
    std::size_t attackCycleCount_ = 0;
    std::size_t attackCycleIndex_ = 0;
    int attackCyclePhase_ = 0;
    AttackKind lastAutomaticAttack_ = AttackKind::None;
    std::mt19937 attackRandom_{std::random_device{}()};
    int battlePhase_ = 1;
    int pendingBattlePhase_ = 1;
    int rushStep_ = 0;
    bool rushDamageApplied_ = false;
    bool shockwaveLanded_ = false;
    bool warningTriggered_ = false;
    bool hasBaseScale_ = false;
    std::string debugPreviewAttackId_;

    EncounterState encounterState_ = EncounterState::Normal;
    engine::Vector3 authoredPosition_{};
    engine::Vector3 authoredScale_{1.0f, 1.0f, 1.0f};
    engine::Vector3 authoredRotation_{};
    engine::Vector4 authoredColor_{1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t authoredCollisionAttribute_ = 0;
    uint32_t authoredCollisionMask_ = 0;
    int authoredMaterialType_ = 0;
    float authoredEmissive_ = 1.0f;
    float encounterTimer_ = 0.0f;
    float idleTimer_ = 0.0f;
    bool initializedForPlay_ = false;
    bool authoredStateCaptured_ = false;
    bool activationRequested_ = false;
    // 封印一基の攻撃可能な結晶と付属表示、破壊状態。破壊演出中も状態を保持する。
    struct CrownSeal {
        // 攻撃可能な結晶本体。所有権はボス、衝突登録は明示的に解除します。
        std::unique_ptr<BaseEnemy> crystal;
        std::unique_ptr<engine::Object3d> ring;
        std::unique_ptr<engine::Object3d> link;
        std::unique_ptr<engine::Object3d> gem;
        std::array<std::unique_ptr<EffectObject3d>, 3> energyPulses;
        engine::Vector3 position{};
        bool broken = false;
        float breakAge = 0.0f;
    };
    std::array<CrownSeal, 3> crownSeals_;
    int brokenSeals_ = 0;
    float sealAttackTimer_ = 0.0f;
    float sealPressureDamage_ = 0.0f;
    int sealAttackSerial_ = 0;
    enum class SealState { Reveal, Chant, Attack, Recover, Broken };
    SealState sealState_ = SealState::Reveal;
    float sealStateTimer_ = 0.0f;
    float sealVfxTimer_ = 0.0f;
    int sealAttackStage_ = 0;
    bool sealImpactPlayed_ = false;
    float armorOpenTimer_ = 0.0f;
    bool crownInterlude_ = false;
    bool finalArenaStarted_ = false;
    bool sealTargetsLocked_ = false;
    bool pendingDebugSeals_ = false;
    bool introPresentationActive_ = false;
    bool pendingDebugInterlude_ = false;
    engine::Vector3 sealLockedTarget_{};
    // 第二形態の障壁、足元の術式、全域攻撃の光をボスが所有します。
    std::unique_ptr<EffectObject3d> crownShieldRing_;
    std::unique_ptr<EffectObject3d> crownShieldShell_;
    std::unique_ptr<engine::Object3d> crownShieldBarrier_;
    std::unique_ptr<EffectObject3d> crownCastFloor_;
    std::unique_ptr<EffectObject3d> crownArenaRing_;
    std::unique_ptr<EffectObject3d> crownBarrageFlash_;
    std::unique_ptr<EffectObject3d> crownBarrageSkySigil_;
    std::unique_ptr<EffectObject3d> crownBarrageSkyCounterSigil_;
    std::unique_ptr<EffectObject3d> crownBarrageSkyOuterRing_;
    std::unique_ptr<EffectObject3d> crownBarrageSkyInnerRing_;
    // 地表の赤い蓄積光、立体オーラ、二重の全域放電、着弾光、頭上の蓄積光。
    std::array<std::unique_ptr<EffectObject3d>, 6> crownBarrageLayers_;
    // 詠唱失敗時に円形闘技場へ分散して落ちる槍です。
    struct SealBarrageSpear {
        std::unique_ptr<engine::Object3d> model;
        std::unique_ptr<EffectObject3d> glow;
        std::unique_ptr<EffectObject3d> portal;
        std::unique_ptr<EffectObject3d> impactLightning;
        std::unique_ptr<EffectObject3d> groundArc;
        std::unique_ptr<EffectObject3d> impactArc;
        std::unique_ptr<EffectObject3d> impactBeam;
        engine::Vector3 ground{};
        float delay = 0.0f;
        float size = 1.0f;
        bool hero = false;
    };
    std::vector<SealBarrageSpear> sealBarrage_;
    bool sealBarrageDamageApplied_ = false;
    // 第一形態から中央へ移動する開始位置を固定します。
    engine::Vector3 phaseShiftStartPosition_{};
    /// <summary>第2形態の王冠封印結晶と付属表示を生成する。</summary>
    void InitializeCrownSeals();
    /// <summary>封印結晶の状態と破壊演出を更新する。</summary>
    void UpdateCrownSeals(float deltaTime);
    /// <summary>着弾点と攻撃段階から封印結晶の破壊を判定する。</summary>
    void TryBreakCrownSeal(const engine::Vector3& impact, int attackStage = 0, bool force = false);
    /// <summary>封印結晶と最終戦演出の一時状態を片付ける。</summary>
    void ResetCrownFinale();
};
