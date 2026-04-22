#pragma once
#include "BaseEnemy.h"
#include "GhostDirector.h"
#include <memory>
#include <string>
#include "BossAttack/BaseBossAttack.h"
#include "GPUParticleEmitter.h"

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

    // ==========================================
    // 新オブジェクト生成用に common_ を渡すゲッター
    // ==========================================
    Object3dCommon* GetCommon() const { return common_; }

    // ==========================================
    // 最終奥義が終わったことを伝えるためのセッター！
    // ==========================================
    void SetWaitingForDeath(bool waiting) { isWaitingForDeath_ = waiting; }


    // --- public: に追加 ---
    void StartDeathSequence(); // 死亡演出の開始
    void ShowCrackedCore();    // 段階2：亀裂モデルへの差し替え

    void StartBattle(); // シーン側から「戦闘開始！」の合図を送る関数
    bool IsBattleStarted() const { return isBattleStarted_; }

    void StartAppearance(); // 登場演出をスタートする関数
    bool IsAppearing() const { return isAppearing_; }

    void ActuallySpawnShards();

    bool IsCompletelyDead() const { return isCompletelyDead_; }
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
    void TakeBodyDamage(float damage);

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

    // ==========================================
    // 最終奥義とトドメに関するフラグ
    // ==========================================
    bool isFinalPhase_ = false;       // HP1になって発狂中か？
    bool isWaitingForDeath_ = false;  // 必殺技が終わってトドメ待ちか？
    bool isWaitingForFinisher_ = false; // 大技終了後のトドメ待ちモード

    // ▼ 先ほどのアクセス違反（クラッシュ）を完全に防ぐための安全装置
    bool isShardSpawnRequested_ = false;

    // ==========================================
    // パーティクルのエミッターを保持
    // ==========================================
    std::vector<std::unique_ptr<GPUParticleEmitter>> particleEmitters_;

    // ==========================================
    // ★ 破片演出用の構造体と変数
    // ==========================================
    struct CorePiece {
        Object3d* obj; // ★ 生ポインタでOK！（実体はシーンが管理する）
        Vector3 velocity;
        Vector3 rotSpeed;
    };
    std::vector<CorePiece> corePieces_;

    bool isCoreBroken_ = false;  // 割れたかどうかのフラグ
    float deathTimer_ = 0.0f;    // 割れたあとの退場タイマー

    // 演出用の関数
    void BreakCore();
    void UpdateCorePieces(float deltaTime);


    // --- private: に追加 ---
    int deathPhase_ = 0;       // 0: 生存, 1: 静止, 2: 亀裂, 3: 爆散
    float sequenceTimer_ = 0.0f; // 各フェーズの1秒を測るタイマー

    bool isBattleStarted_ = false; // 戦闘開始フラグ（最初は false）

    void UpdateAppearance(float deltaTime); // 演出中の更新処理

    bool isAppearing_ = false;  // 登場演出中かどうか
    bool isWaitingForDirector_ = false; // ディレクターのアニメーション終了待ちかどうか
    int appearancePhase_ = 0;   // 演出の進行度
    float appearanceTimer_ = 0.0f;
    bool isCompletelyDead_ = false;
    float assemblyTimer_ = 0.0f;

};