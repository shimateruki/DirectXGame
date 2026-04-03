#pragma once
#include "BaseEnemy.h"
#include "GhostDirector.h"
#include <memory>
#include <string>
#include "BossAttack/BaseBossAttack.h"

class SceneManager; // 前方宣言
class MapBlock;

// ボスのコア(中核)となる統合制御クラス
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

    // パーツ(ブロック)を登録する関数
    void AddArmorBlock(Object3d* block) {
        armorBlocks_.push_back(block);
        block->SetCollisionAttribute(kEnemyAttack);
        block->SetCollisionMask(kPlayer);

        blockHps_.push_back(100.0f); // 例としてHPを100に設定
        blockBroken_.push_back(false);
    }

    float GetHp() const { return param_.has_value() ? param_->hp : 1000.0f; }
    float GetMaxHp() const { return param_.has_value() ? param_->maxHp : 1000.0f; }

    // バリアHP
    float GetBarrierHp() const { return barrierHp_; }
    float GetMaxBarrierHp() const { return maxBarrierHp_; }

    // ==========================================
    // 攻撃クラスがボスの部品をいじるためのゲッター！
    // ==========================================
    std::vector<Object3d*>& GetArmorBlocks() { return armorBlocks_; }
    Object3d* GetTarget() const { return target_; }
    Object3d* GetWarningArea() const { return warningArea_; }

    struct OrbitData {
        Vector3 pos;
        Vector3 rot;
        Vector3 scale;
    };
    OrbitData GetIdleOrbit(size_t index);

    struct FlyingBlock {
        Object3d* block;
        Vector3 velocity;
        Vector3 currentRot;
        int mode;
        int originalIndex;
    };
    std::vector<FlyingBlock>& GetFlyingBlocks() { return flyingBlocks_; }

    // ==========================================
    // マップブロックを登録・取得するゲッター！
    // ==========================================
    void AddMapBlock(MapBlock* block) { mapBlocks_.push_back(block); }
    std::vector<MapBlock*>& GetMapBlocks() { return mapBlocks_; }

    // ==========================================
    // マップブロックを装甲として同化する関数と、バリア回復関数
    // ==========================================
    bool AssimilateBlock(Object3d* newBlock);
    bool IsArmorFull() const;
    int GetNeededBlockCount() const;

private:

    // 射出されたブロックのリスト
    std::vector<FlyingBlock> flyingBlocks_;
    float returnDelayTimer_ = 0.0f;

    // ==================================================
    // ステート(状態)管理メソッド
    // ==================================================
    void ChangeState(State nextState);
    void UpdateIdle(float deltaTime);
    void UpdateWeak(float deltaTime);

    // 飛んでいるブロックを専用で更新する関数
    void UpdateFlyingBlocks(float deltaTime);
    void TakeBarrierDamage(float damage, Object3d* hitBlock = nullptr);

    // ==================================================
    // 内部コンポーネント・変数
    // ==================================================
    std::unique_ptr<GhostDirector> director_; // 演出・モーション制御用の監督
    SceneManager* sceneManager_ = nullptr;    // エディタ操作/プレイ状態の判定用

    State state_ = State::Idle;               // 現在のステート
    bool isFirstFrame_ = true;                // 初回更新フラグ

    std::vector<Object3d*> armorBlocks_;

    // ==========================================
    // ★ ダウン(Weak)演出用のアニメーション変数（これだけ残す！）
    // ==========================================
    float animTimer_ = 0.0f;
    std::vector<Vector3> blockStartPos_;
    std::vector<Vector3> blockTargetPos_;

    float barrierHp_ = 100.0f;
    float maxBarrierHp_ = 100.0f;
    bool s_isTimeStopped_ = false;

    // エディターで作った予兆エリアを参照するためのポインタ
    Object3d* warningArea_ = nullptr;

    Vector4 originalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };

    // ==========================================
    // 現在実行中の新しい攻撃クラスを保持するポインタ
    // ==========================================
    std::unique_ptr<BaseBossAttack> currentAttack_ = nullptr;

    // ==========================================
    // ブロックごとのHPと破壊状態
    // ==========================================
    std::vector<float> blockHps_;
    std::vector<bool> blockBroken_;

    // マップブロックのリスト
    std::vector<MapBlock*> mapBlocks_;
};