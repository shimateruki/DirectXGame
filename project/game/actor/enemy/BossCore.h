#pragma once
#include "BaseEnemy.h"
#include "GhostDirector.h"
#include <memory>
#include <string>

class SceneManager; // 前方宣言

// ボス本体、装甲ブロック、攻撃フェーズ、バリアHPをまとめて制御する中核クラス
class BossCore : public BaseEnemy {
public:
    // ==================================================
    // 状態定義
    // ==================================================
    enum class State {
        Idle,   // 待機
        Attack, // 攻撃
        Weak    // 弱点露出(ダウン)
    };

    // ==================================================
    // 基本サイクル (BaseEnemy オーバーライド)
    // ==================================================
    void Initialize(Object3dCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    bool OnCollision(Object3d* other) override;
#ifdef USE_IMGUI
    void DrawImGui();
#endif

    // ==================================================
    // アクセッサ
    // ==================================================
    void SetSceneManager(SceneManager* manager) { sceneManager_ = manager; }

    // エディタで配置した装甲ブロックをボスの攻撃パーツとして登録する
    void AddArmorBlock (Object3d *block) { 
        armorBlocks_.push_back (block);
        block->SetCollisionAttribute(kEnemyAttack);
        block->SetCollisionMask(kPlayer);
    }

    float GetHp() const { return param_.has_value() ? param_->hp : 1000.0f; }
    float GetMaxHp() const { return param_.has_value() ? param_->maxHp : 1000.0f; }

    // バリアHP
    float GetBarrierHp() const { return barrierHp_; }
    float GetMaxBarrierHp() const { return maxBarrierHp_; }
private:
    // 射出後の装甲ブロックを、飛翔・地面待機・帰還の状態つきで管理する
    struct FlyingBlock {
        Object3d *block;
        Vector3 velocity;
        Vector3 currentRot; // 現在の回転角度。
        int mode; // 0=飛翔中, 1=地面待機, 2=帰還中, 3=回収完了
        int originalIndex; // armorBlocks_ 内での元の位置。
    };

    // 射出されたブロックのリスト
    std::vector<FlyingBlock> flyingBlocks_;
    float returnDelayTimer_ = 0.0f;

    // ==================================================
    // ステート(状態)管理メソッド
    // ==================================================
    void ChangeState(State nextState);
    void UpdateIdle(float deltaTime);
    void UpdateAttack(float deltaTime);
    void UpdateWeak(float deltaTime);
    // ボスの攻撃フェーズを animPhase_ と animTimer_ で進める
    void UpdateAnimationSequence (float deltaTime);

    // 射出された装甲ブロックの飛翔・地面待機・帰還を更新する
    void UpdateFlyingBlocks (float deltaTime);
    void TakeBarrierDamage(float damage);

    // 攻撃フェーズ中に使う一時メモ
    int animPhase_ = 0;
    float animTimer_ = 0.0f;
    Vector3 animStartPos_ = { 0,0,0 };
    Vector3 animTargetPos_ = { 0,0,0 };
    Vector3 animStartRot_ = { 0,0,0 };
    bool wasPlaying_ = false;

    int attackMode_ = 0;         // 0=待機, 1以降=各攻撃パターン。
    int shotCount_ = 0;          // 射出済みブロック数。
    float shotInterval_ = 0.0f;  // 連射インターバル計測用。

    // ==================================================
    // 内部コンポーネント・変数
    // ==================================================
    std::unique_ptr<GhostDirector> director_; // 演出・モーション制御用の監督

    SceneManager* sceneManager_ = nullptr;    // エディタ操作/プレイ状態の判定用

    State state_ = State::Idle;               // 現在のステート
    bool isFirstFrame_ = true;                // 初回更新フラグ

    std::vector<Object3d *> armorBlocks_; // ボスの周囲を構成する装甲ブロック。

    // 形態変化アニメーション用の座標メモ
    std::vector<Vector3> blockStartPos_;
    std::vector<Vector3> blockTargetPos_;
    std::vector<Vector3> blockStartScale_;
    std::vector<Vector3> blockTargetScale_;

    // ==========================================
    // Phase 50「気を付け」ポーズ遷移用のメモ
    // ==========================================
    std::vector<Vector3> attentionStartPos_;
    std::vector<Vector3> attentionStartScale_;
    std::vector<Vector3> attentionStartRot_;
    float barrierHp_ = 100.0f;
    float maxBarrierHp_ = 100.0f;

    bool s_isTimeStopped_ = false;

    // エディターで作った予兆エリアを参照するためのポインタ
    Object3d* warningArea_ = nullptr;

    Vector4 originalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };

    // 攻撃モード6のレーザービーム用円柱オブジェクト
    std::vector<std::unique_ptr<Object3d>> laserBeams_;
};
