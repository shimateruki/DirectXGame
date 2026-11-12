#pragma once
#include "game/actor/enemy/combat/EnemyAttackSelection.h"

#include "BaseEnemy.h"
#include "game/actor/combat/AttackSequence.h"
#include "game/actor/enemy/animation/BossGroundLocomotion.h"
#include "game/actor/enemy/combat/ISequencePreviewEnemy.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

class EffectObject3D;
class BossChantVisual;

// 三つの頂点と属性コアを持ち、HPに応じて攻撃属性が変わる中ボススライムです。
// 関連：BaseEnemyの戦闘基盤とAttackSequenceを使い、召喚物はシーンへ引き渡す。
// 主な入口：Initialize / Update / BindSceneSpawnContext
class EnemyPrismSlime : public BaseEnemy, public ISequencePreviewEnemy {
  public:
    /// 本体の構築。攻撃状態の実際の初期化は Initialize で行う。
    EnemyPrismSlime();

    /// 所有リソースは各メンバの破棄に委ねる。
    ~EnemyPrismSlime() override;

    /// モデル、三つの頂点、攻撃定義と遭遇状態を初期化する。
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;

    /// 遭遇状態、属性段階、各攻撃と召喚を進める。
    void Update(float deltaTime) override;

    /// 本体と所有する結晶・詠唱演出を描く。
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) override;

    void DrawOwnedTransparentVisuals(ID3D12Resource* pointLightResource,
                                     ID3D12Resource* spotLightResource) override;

    void DrawForCamera(Camera* camera, ID3D12Resource* pointLightResource,
                       ID3D12Resource* spotLightResource, int previewBufferIndex = 0) override;

    void DrawOwnedTransparentVisualsForCamera(Camera* camera, ID3D12Resource* pointLightResource,
                                              ID3D12Resource* spotLightResource,
                                              int previewBufferIndex = 0) override;

    /// 召喚した敵を追加するシーンを登録する。
    void BindSceneSpawnContext(BaseScene* scene) override;

    /// 配置用の中ボス複製を作る。
    std::unique_ptr<engine::Object3D> Clone() const override;

    /// スイッチの有効化に合わせて登場状態を切り替える。
    void OnSwitchEvent(bool active) override;

    bool IsPullImmune() const override { return true; }

    /// <summary>敵攻撃プレビューで指定した技だけを繰り返します。</summary>
    /// <param name="attackId">攻撃プロファイルのID。空文字で通常AIに戻します。</param>
    void SetDebugPreviewAttackId(const std::string& attackId);

    void SetSequencePreviewAttackId(const std::string& attackId) override { SetDebugPreviewAttackId(attackId); }

    /// 攻撃シーケンスが有効な場合、その段階名を返す。
    const char* GetSequenceStateLabel() const override;

    /// 詠唱だけを確認するプレビューへ切り替える。
    void SetDebugPreviewChantOnly(bool enabled) { debugPreviewChantOnly_ = enabled; }

    // 召喚した敵の所有権をシーンへ渡すためのコールバックです。
    void SetSpawnCallback(std::function<void(std::unique_ptr<BaseEnemy>)> callback) {
        spawnCallback_ = std::move(callback);
    }

    /// デバッグ表示向けに現在の攻撃段階名を返す。
    const char* GetDebugAttackPhaseName() const;

    // -2 は詠唱待機、-1 は生成失敗、0 は生成済みだが表示対象なしを表します。
    int GetDebugChantVisibleLayerCount() const;

    /// 配置スケールを結晶と攻撃判定の基準にする。
    void ApplyManagedScale(const engine::Vector3& scale) override;

    // 中ボス戦HUDから、出現演出と実HPを参照します。
    /// 遭遇 HUD を表示すべき状態か返す。
    bool IsEncounterHudActive() const;

    /// HUD に表示する現在 HP を返す。
    float GetEncounterCurrentHp() const;

    /// HUD の分母となる最大 HP を返す。
    float GetEncounterMaximumHp() const;

    /// 登場演出の進行率を HUD 向けに返す。
    float GetEncounterAppearanceProgress() const;

    /// 開発用に中ボスの撃破処理を発火する。
    void TriggerDebugDefeat();

  private:
    void ForEachOwnedEffect(const std::function<void(engine::Object3D*)>& visit) const;
    enum class ElementPhase {
        Neutral,
        Fire,
        Thunder,
        Wind,
    };

    enum class AttackKind {
        None,
        PrismSpikes,
        CrystalLanceVolley,
        FireFan,
        ThunderChain,
        WindWave,
        SlimeSummon,
    };

    enum class AttackState {
        Idle,
        Windup,
        Active,
        Recovery,
        Reposition,
    };

    // 地面から伸びる結晶一本の表示。完成時のスケールを基準に経過時間で形を変える。
    struct PrismSpikeVisual {
        std::unique_ptr<engine::Object3D> object;
        // 成長中も固定する結晶の根元のワールド座標。
        engine::Vector3 groundPosition{};
        // 成長が終わったときの表示倍率。
        engine::Vector3 fullScale{1.0f, 1.0f, 1.0f};
        // 出現後の経過秒数。
        float age = 0.0f;
        // 結晶ごとの演出のずれに使う番号。
        int index = 0;
    };

    // 結晶槍の予告枠・発射済み状態・飛翔位置をまとめ、表示 Object の寿命を所有する。
    struct CrystalLanceVisual {
        std::unique_ptr<engine::Object3D> object;
        // 現在のワールド座標。発射後は速度で更新する。
        engine::Vector3 position{};
        // 発射後のワールド速度。
        engine::Vector3 velocity{};
        // 生成後の経過秒数。
        float age = 0.0f;
        // 軌跡粒子を出す間隔の管理に使う。
        float trailTimer = 0.0f;
        // 溜め中の左右位置や立ち上がり順を決める番号。
        int slotIndex = 0;
        // 本体近くで溜める状態を終え、飛行に移ったか。
        bool launched = false;
    };

    enum class SummonSlimeKind {
        Fire,
        Bomber,
        Pink,
        Thunder,
    };

    // 召喚口の表示と出現させる敵種。spawned により同じ口からの重複生成を防ぐ。
    struct SummonPortalVisual {
        // 召喚口の本体表示。
        std::unique_ptr<EffectObject3D> effect;
        // 本体に重ねる発光表示。
        std::unique_ptr<EffectObject3D> glow;
        // 召喚口と敵の出現位置の基準になるワールド座標。
        engine::Vector3 groundPosition{};
        engine::Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
        SummonSlimeKind slimeKind = SummonSlimeKind::Pink;
        // 召喚口を作ってからの経過秒数。
        float age = 0.0f;
        // 次の粒子発生までの残り時間を管理する。
        float moteTimer = 0.0f;
        // 敵を出してからの経過秒数。召喚口のフェードに使う。
        float spawnedAge = 0.0f;
        bool spawned = false;
    };

    // 風渦の移動状態と複数の輪の表示を所有する。方向と表示部品を一緒に更新する。
    struct WindVortexVisual {
        // 風渦の移動を計算する起点。
        engine::Vector3 origin{};
        // 輪と粒子を追従させる現在のワールド座標。
        engine::Vector3 position{};
        // 風渦の移動方向。
        engine::Vector3 direction{};
        // 風渦が次に移動方向を変えるまでの残り秒数。
        float driftTimer = 0.0f;
        std::unique_ptr<EffectObject3D> circle;
        // 足元の輪に重ねる発光表示。
        std::unique_ptr<EffectObject3D> circleGlow;
        // 外周で回転する輪の表示。
        std::array<std::unique_ptr<EffectObject3D>, 4> outer;
        // 内側の輪の表示。
        std::array<std::unique_ptr<EffectObject3D>, 1> inner;
    };

    enum class EncounterState {
        Normal,
        Dormant,
        Appearing,
        Active,
    };

    // 遭遇演出。編集時に配置された座標とスケールは退避し、再生終了時に復元します。
    // 遭遇演出中は通常AIを止め、登場完了までの状態遷移を進めます。
    bool UpdateEncounterState(float deltaTime);

    /// Editor 配置時の位置と大きさを登場演出用に退避する。
    void CaptureEncounterAuthoredState(bool refreshVisualTransform = false);

    /// Play 開始時のスイッチ状態から遭遇段階を設定する。
    void InitializeEncounterState();

    /// Editor に戻るとき配置と見た目を復元する。
    void ResetEncounterStateForEditor();

    /// 休眠状態から登場演出を開始する。
    void BeginEncounterAppearance();

    /// ボスを休眠させ、攻撃表示を隠す。
    void ApplyDormantEncounterState();

    /// 登場演出を終えて戦闘状態へ移る。
    void FinishEncounterAppearance();

    /// スイッチで起動する遭遇制御の対象か返す。
    bool IsEncounterControlled() const;

    /// 現在の登場演出の継続時間を返す。
    float GetEncounterAppearanceDuration() const;

    /// 行動不能中の状態を処理し、通常 AI を止める場合は true を返す。
    bool UpdateInactiveState(float deltaTime);

    // 属性と攻撃状態。velocity は呼び出し元の移動処理へ渡す速度です。
    /// 未設定なら配置スケールを演出と判定の基準として記録する。
    void EnsureBaseScale();

    /// HP に応じて炎・雷・風の属性段階を切り替える。
    void UpdateElementPhase();

    /// 対象距離と攻撃段階から移動速度を決める。
    void UpdateBehavior(float deltaTime, const engine::Vector3& direction, float distance, engine::Vector3& velocity);
    void UpdateLocomotion(float deltaTime, const engine::Vector3& targetDirection, engine::Vector3& velocity);

    /// <summary>攻撃の対象方向・距離を固定し、予備動作を開始します。</summary>
    /// <param name="kind">開始する技の種類。</param>
    /// <param name="direction">攻撃開始時の標的方向。</param>
    /// <param name="distance">攻撃開始時の標的距離。</param>
    void StartAttack(AttackKind kind, const engine::Vector3& direction, float distance);

    // 予備動作、攻撃、硬直の順に更新し、終了後に再配置へ移ります。
    void UpdateAttack(float deltaTime, const engine::Vector3& direction, float distance);

    /// 予備動作中の予告と詠唱を進める。
    void UpdateWindup(float deltaTime);

    /// 攻撃本体を更新し、終了時に硬直へ移す。
    void UpdateActive(float deltaTime);

    /// 攻撃後の硬直へ切り替える。
    void BeginRecovery();

    /// 現在の技を終了し、次の再配置へ進める。
    void FinishAttack();

    // 次の攻撃を見せるための移動。攻撃中の移動とは分けて管理します。
    /// 次の技を見せるための移動を開始する。
    void BeginReposition();

    /// 再配置中の移動速度を更新する。
    void UpdateReposition(float deltaTime, engine::Vector3& velocity);

    /// 再配置を終えて移動速度を戻す。
    void FinishReposition(engine::Vector3& velocity);

    /// <summary>現在の属性の未使用技から、射程内の技を優先して選びます。</summary>
    /// <param name="targetDistance">標的までの距離。</param>
    /// <returns>今回使用する技。</returns>
    AttackKind ResolveAutomaticAttack(float targetDistance);

    // 一巡後に候補を補充し、プレビュー時は指定技へ切り替えます。
    /// 一巡する攻撃候補を補充する。
    void PrepareAttackCycle();

    /// デバッグ指定中の攻撃種別を解決する。
    AttackKind ResolveDebugAttack() const;

    /// 現在の技に対応する攻撃定義を返す。
    const EnemyAttackDefinition& GetCurrentAttackDefinition() const;

    // 炎属性枠の候補をJSONから選び、開始時に複製します。実行中の編集で弾数を変えません。
    const EnemyAttackDefinition& SelectFireFanDefinition(float distance) const;

    /// 攻撃シーケンスへ渡す対象と術者の状態を構築する。
    AttackSequenceContext MakeSequenceContext();

    /// 本編とプレビューが共有する攻撃種別・IDの対応表を返す。
    std::span<const EnemyAttackSelection::Entry<AttackKind>> GetAttackEntries() const;

    /// 内部の攻撃種別を JSON プロファイルの ID へ変換する。
    const char* GetAttackId(AttackKind kind) const;

    // 晶槍の候補位置と発生後の成長・破砕を管理します。
    void PreparePrismSpikePoints();

    /// 候補位置に次の結晶棘を発生させる。
    void SpawnNextPrismSpike();

    /// 結晶棘の成長と破砕を更新する。
    void UpdatePrismSpikeVisuals(float deltaTime);

    /// 破砕した棘に合わせて粒子演出を発生させる。
    void SpawnPrismSpikeShatter(const PrismSpikeVisual& spike);

    // 飛翔する結晶槍と、その足元の魔法陣を更新します。
    void PrepareCrystalLances();

    /// 予告した次の結晶槍を発射する。
    void LaunchNextCrystalLance();

    /// 飛翔中の結晶槍を移動させて着弾を処理する。
    void UpdateCrystalLanceVisuals(float deltaTime);

    /// 結晶槍を囲む魔法陣を攻撃段階に同期する。
    void UpdateCrystalCircleVisual(float deltaTime);

    // 攻撃範囲と術者の詠唱を示す魔法陣です。
    void UpdatePrismSpikeSectorVisual(float deltaTime);

    void UpdateWindSpellVisual(float deltaTime);

    // 攻撃状態から詠唱共通部品の進行率を作り、見た目を本体に追従させる。
    void UpdateChantSigilVisual(float deltaTime);

    /// 結晶槍の着弾地点に破砕演出を出す。
    void SpawnCrystalLanceShatter(const CrystalLanceVisual& lance);

    /// 中断した攻撃の一時表示をまとめて取り除く。
    void ClearPrismAttackVisuals();

    /// 指定番号の結晶棘の表示オブジェクトを作る。
    std::unique_ptr<engine::Object3D> CreatePrismSpikeObject(int index) const;

    /// <summary>晶槍が到達した扇形の範囲に攻撃判定を適用します。</summary>
    /// <param name="reachedDistance">術者から今回の攻撃前線までの距離。</param>
    void ApplyPrismSpellDamage(float reachedDistance);

    // 炎・雷・風の攻撃本体と、発生前に確定する着弾位置です。
    /// 扇状攻撃の次の炎弾を発射する。
    void FireNextFanProjectile();

    /// 確定した次の落雷位置へ攻撃を発生させる。
    void StrikeNextThunderPoint();

    /// 風の竜巻による押し出し判定を適用する。
    void ApplyWindTornado();

    /// 落雷候補位置を攻撃開始前に確定する。
    void PrepareThunderPoints();

    // 召喚陣の生成から敵の引き渡しまでを扱います。
    void PrepareSummonPortals();

    /// 召喚口の出現から消滅までの見た目を更新する。
    void UpdateSummonPortalVisuals(float deltaTime);

    /// 次の召喚口から敵を生成する。
    void SpawnNextSummonedSlime();

    /// 生成した敵の所有権を登録先へ渡す。
    void SpawnSummonedSlime(std::unique_ptr<BaseEnemy> enemy);

    // 魔法陣と地面位置を生成・取得する共通処理です。
    std::unique_ptr<EffectObject3D> CreateSummonPortalEffect(bool glow) const;

    // 同じ JSON を通常層と加算発光層で使い、後段で別々の透明度を制御する。
    std::unique_ptr<EffectObject3D> CreateCrystalCircleEffect(bool glow) const;

    // 演出データの再生状態だけ準備し、実際の表示時刻は攻撃状態が決める。
    std::unique_ptr<EffectObject3D> CreateSpellEffect(const char* path) const;

    engine::Vector3 GetChantSigilCenter() const;

    // 予告や着弾を空中に置かないよう、周囲の地形面を探す。
    engine::Vector3 FindGroundPoint(const engine::Vector3& samplePosition) const;

    // 顔パーツと本体のアニメーションを同期します。
    /// 属性を示す顔パーツの表示を準備する。
    void InitializeFaceParts();

    // 本体に追従する子オブジェクトを作り、衝突対象からは除外する。
    std::unique_ptr<engine::Object3D> CreatePart(const std::string& name, const std::string& modelName,
                                         const engine::Vector3& localPosition, const engine::Vector3& localScale, const engine::Vector4& color,
                                         float emissive);

    /// 本体の姿勢へ顔パーツを追従させる。
    void UpdateFaceParts(float deltaTime);

    /// 現在属性の色とパーツ表示を反映する。
    void UpdatePhaseAppearance();

    /// 攻撃段階に応じた本体の伸縮を反映する。
    void ApplySlimeAnimation(float deltaTime);

    /// 指定方向へ本体を向ける。
    void UpdateFacing(const engine::Vector3& direction);

    // JSON プリセットから通常/方向付き粒子を発生させます。
    void EmitPreset(const char* presetName, const engine::Vector3& position);

    // 指向性のあるプリセットには発射方向と速度倍率を渡す。
    void EmitDirectedPreset(const char* presetName, const engine::Vector3& position, const engine::Vector3& direction,
                            float speedScale);

    void SyncCollisionRadius();

    // 現在の属性。自動攻撃の候補と本体の見た目を決める。
    ElementPhase elementPhase_ = ElementPhase::Neutral;
    // 攻撃の種類。技ごとの設定や更新処理を選ぶ。
    AttackKind currentAttack_ = AttackKind::None;
    // 現在の技が予備動作・実行・硬直のどの段階にいるか。
    AttackState attackState_ = AttackState::Idle;

    // 開始時に選んだ炎の扇の攻撃定義。再生に渡すため保持する。
    EnemyAttackDefinition selectedFireFan_;
    // シーケンスで再生する技の進捗と実行状態。
    AttackSequence attackSequence_;

    engine::Vector3 baseScale_ = {1.0f, 1.0f, 1.0f}; // 攻撃・被弾の変形を重ねる前の基準倍率。
    engine::Vector3 lockedDirection_ = {0.0f, 0.0f, 1.0f}; // 攻撃の狙いを確定した水平単位方向。
    // 狙いを固定した時点の標的までの距離。攻撃の配置計算に使う。
    float lockedTargetDistance_ = 0.0f;

    float idleTimer_ = 0.0f; // 待機中の見た目に使う経過秒数。
    // 接地移動の状態。方向転換や歩行の進行をフレーム間で保持する。
    BossGroundLocomotion locomotion_{};
    // 歩数に応じた演出の連番。左右交互の切り替えに使う。
    std::uint32_t locomotionStepSerial_ = 0;
    bool scriptedMovementThisFrame_ = false; // 技の処理で座標を直接動かしたフレームはCharacterMotorの移動を止める。

    float attackTimer_ = 0.0f; // 攻撃の段階切替までの残り秒数。
    float attackStateDuration_ = 0.0f; // 現在の攻撃段階の全長（秒）。残り時間の正規化に使う。
    float attackCooldown_ = 1.0f; // 攻撃の再使用までの残り秒数。

    float effectTimer_ = 0.0f; // 次の粒子・表示更新までの残り秒数。
    float actionTimer_ = 0.0f; // 技内の次の発生処理までの残り秒数。
    float impactPulseTimer_ = 0.0f; // 着弾の変形までの残り秒数。

    // 再配置で向かう目的地のワールド座標。
    engine::Vector3 repositionDestination_{};
    float repositionTimer_ = 0.0f; // 再配置の完了までの残り秒数。
    // 再配置の全長（秒）。残り時間と合わせて進捗を求める。
    float repositionDuration_ = 0.0f;
    // 再配置先を選ぶための乱数状態。
    std::uint32_t repositionRandomState_ = 0x51A7C39Du;
    // 再配置を始めた直後に、跳躍を一度だけ起こすためのフラグ。
    bool repositionJumpPending_ = false;

    int actionIndex_ = 0; // 技内で次に発生させる弾・着弾点の番号。
    bool hasBaseScale_ = false; // モデルの基準倍率を一度取得したか。

    bool warningTriggered_ = false; // 現在の技で予告の強調を一度だけ発生させるための印。
    // 技中の一度きりのダメージを処理済みか。
    bool damageApplied_ = false;
    // 結晶槍の一斉射全体で、すでに標的へダメージを与えたか。
    bool crystalVolleyDamageApplied_ = false;

    // デバッグプレビューから指定された技のID。
    std::string debugPreviewAttackId_;
    // デバッグ時に詠唱だけを確認するモード。
    bool debugPreviewChantOnly_ = false;

    // 連続で落雷する5箇所のワールド座標。
    std::array<engine::Vector3, 5> thunderStrikePositions_{};

    // 結晶の術式の中心。表示の位置合わせに使う。
    engine::Vector3 prismSpellCenter_{};
    // 結晶の発生パターンを計算する起点。
    engine::Vector3 prismSpellOrigin_{};
    // 順に立ち上がる結晶を置く位置の一覧。
    std::array<engine::Vector3, 89> prismSpikePositions_{};

    // 風の術式を開始した位置。風渦の配置の基準にする。
    engine::Vector3 windSpellOrigin_{};
    // 風渦の演出を開始してからの経過秒数。
    float windTornadoAge_ = 0.0f;
    // 風渦の次の移動方向を決める乱数状態。
    std::uint32_t windRandomState_ = 0xC671AE29u;

    // 出現済みの結晶を所有し、成長や消滅を個別に更新する。
    std::vector<PrismSpikeVisual> prismSpikeVisuals_;
    // 溜め中と飛行中の結晶槍を所有する。
    std::vector<CrystalLanceVisual> crystalLanceVisuals_;

    // 結晶槍を並べる魔法陣の本体。
    std::unique_ptr<EffectObject3D> crystalCircle_;
    // 結晶槍の魔法陣に重ねる発光表示。
    std::unique_ptr<EffectObject3D> crystalCircleGlow_;

    // 結晶が立ち上がる範囲を示す扇形の演出。
    std::unique_ptr<EffectObject3D> prismSpikeSector_;
    // 結晶の発生に使う複数の魔法陣。
    std::array<std::unique_ptr<EffectObject3D>, 5> prismSpikeSigil_{};

    // 同時に動く風渦3つの位置と表示部品。
    std::array<WindVortexVisual, 3> windVortices_{};

    // 本体の周囲で進む詠唱演出。
    std::unique_ptr<BossChantVisual> chantVisual_;

    // 召喚口ごとに表示と敵の生成状態を保持する。
    std::vector<SummonPortalVisual> summonPortalVisuals_;
    // 生成した敵の所有権を、呼び出し元の出現処理へ渡す。
    std::function<void(std::unique_ptr<BaseEnemy>)> spawnCallback_;
    // 召喚パターンの乱数と生成する敵の識別名に使う連番。
    std::uint32_t summonRandomSerial_ = 0;
    int automaticAttackSerial_ = 0; // 自動選択した技の通算番号。攻撃周期をまたいで進む。
    // これまでに使った召喚回数。次の攻撃候補に召喚を入れるかの判定に使う。
    int summonWavesUsed_ = 0;
    // 現在の属性で一巡する攻撃候補の並び。
    std::array<AttackKind, 3> attackCycle_{};
    // 配列のうち今回の一巡で実際に使う候補数。
    std::size_t attackCycleCount_ = 0;
    // 次の候補の位置。末尾や属性変更で候補を組み直す。
    std::size_t attackCycleIndex_ = 0;
    // 攻撃候補を作った時点の属性。属性変更の検出に使う。
    ElementPhase attackCyclePhase_ = ElementPhase::Neutral;
    // 最後にAIが選んだ技。次の候補を決めるときに連続使用を避けるため参照する。
    AttackKind lastAutomaticAttack_ = AttackKind::None;
    // AIの攻撃候補を並べ替えるための乱数生成器。
    std::mt19937 attackRandom_{std::random_device{}()};
    // 本体の姿勢に追従する左右の目。
    std::array<std::unique_ptr<engine::Object3D>, 2> eyeParts_;
    std::unique_ptr<engine::Object3D> hurtEyesPart_; // 被弾時だけ表示する左右一組の目。
    // 属性の色を反映する中心部の表示。
    std::unique_ptr<engine::Object3D> corePart_;
    // 中心部を囲み、本体に追従する枠の表示。
    std::unique_ptr<engine::Object3D> coreFramePart_;

    // 待機・登場中・戦闘中のどの段階か。通常AIを実行するかの判定にも使う。
    EncounterState encounterState_ = EncounterState::Normal;

    engine::Vector3 encounterBasePosition_{}; // Editorで設定された登場前の位置。Playの退避・復帰に使う。
    engine::Vector3 encounterBaseScale_{1.0f, 1.0f, 1.0f}; // 登場演出で縮める前の配置倍率。
    // 登場中に色を変える前の色。演出終了やEditor復帰時の復元に使う。
    engine::Vector4 encounterBaseColor_{1.0f, 1.0f, 1.0f, 1.0f};
    // 登場中の発光変化を加える前の基準値。
    float encounterBaseEmissive_ = 1.08f;

    uint32_t encounterCollisionAttribute_ = 0; // 登場待機中に無効化する前の衝突属性。
    uint32_t encounterCollisionMask_ = 0; // 登場待機中に無効化する前の衝突マスク。

    // 登場演出で材質を切り替える前に退避した材質番号。
    int encounterMaterialType_ = 27;

    // 登場を開始してからの経過秒数。演出時間で割って進捗を求める。
    float encounterTimer_ = 0.0f;

    // 現在のPlayで遭遇状態を初期化済みか。Play終了時に解除する。
    bool encounterInitializedForPlay_ = false;
    // Editorの配置状態を退避済みか。演出中の値での再退避を防ぐ。
    bool encounterAuthoredStateCaptured_ = false;
    // 外部から指定された遭遇の起動状態。Play初期化時にも参照する。
    bool encounterRequestedActive_ = false;
};
