#pragma once

#include "engine/utility/math/Math.h"
#include <unordered_set>

namespace engine { class Object3d; }
class Player;

// 通常スライムでも敵へ対抗できる、形態に依存しない基礎攻撃を管理します。
// コピー能力の入力処理と分離し、ボス戦で能力を失っても詰まない状態を保証します。
class PlayerBaseCombatController {
  public:
    // 通常攻撃の押下エッジを渡します。継続押下を毎フレーム true にしないでください。
    void ProcessInput(Player& player, bool attackTriggered);

    // deltaTime は秒。叩きつけと押し潰し、それぞれの準備・攻撃・硬直を進めます。
    void Update(Player& player, float deltaTime);

    // 中断時に一時演出を片付け、既定ではプレイヤー操作も戻します。
    void Cancel(Player& player, bool restoreControl = true);

    bool IsActive() const { return phase_ != Phase::Idle; }

    // 実際に勢いが乗っている突進と落下区間だけゲート破壊を許す。
    bool CanBreakImpactGate() const;

    // 再生位置の復元に必要な数値状態。ポインタ集合 hitTargets_ は保存対象外です。
    struct ReplayState {
        int phase = 0;
        engine::Vector3 direction{0.0f, 0.0f, 1.0f};
        float timer = 0.0f;
        float effectTimer = 0.0f;
        float inputBufferTimer = 0.0f;
        bool pressHitEnemy = false;
    };

    // 攻撃位相・方向・入力猶予を記録し、復元後も同じタイミングから進める。
    ReplayState CaptureReplayState() const;

    void RestoreReplayState(const ReplayState& state);

  private:
    enum class Phase { Idle, BashWindup, BashActive, BashRecovery, PressWindup, PressFall, PressRecovery };

    // ロックオン方向を攻撃開始時に固定し、予備動作中の入力で判定が跳ねないようにする。
    void BeginBash(Player& player);

    // 踏みつけ開始時の方向と各ヒット状態を初期化する。
    void BeginPress(Player& player);

    // 予備動作と攻撃中の時計を分け、判定が有効な区間だけ敵へ当てる。
    void UpdateBash(Player& player, float deltaTime);

    // 落下から着地へ移る時計で、空中判定と着地衝撃を切り替える。
    void UpdatePress(Player& player, float deltaTime);

    // 前方の球状領域で敵を探し、一回の振りで同じ敵を重複して傷つけない。
    void DamageBash(Player& player);

    // 落下中と着地時で判定中心と半径を変える。
    void DamagePress(Player& player, bool landingImpact);

    // 入力猶予中の追加入力は、通常操作へ戻した後に次の攻撃へつなぐ。
    void Finish(Player& player);

    Phase phase_ = Phase::Idle;
    engine::Vector3 direction_{0.0f, 0.0f, 1.0f};
    std::unordered_set<engine::Object3d*> hitTargets_;
    float timer_ = 0.0f;
    float effectTimer_ = 0.0f;
    float inputBufferTimer_ = 0.0f;
    bool pressHitEnemy_ = false;
};
