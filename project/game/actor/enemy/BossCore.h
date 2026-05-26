#pragma once
#include "BaseEnemy.h"
#include "GhostDirector.h"
#include <memory>
#include <string>
#include "BossAttack/BaseBossAttack.h"
#include "GPUParticleEmitter.h"
#include "PostEffect.h"

class SceneManager; // 前方宣言
class MapBlock;

using json = nlohmann::json;

// 攻撃IDと確率の重みのペア
struct AttackWeight {
    int id = 1;       // 攻撃ID (1:突進, 2:弾幕, 3:ハンマー, 4:壁, 5:人型, 6:極太レーザー, 7:吸収, 9:ファンネル, 10:スポーン)
    int weight = 30;  // 抽選時の重み
};

// ボスの攻撃パラメータ（JSON保存・ImGui調整用）
struct BossAttackParams {
    float damageRush = 20.0f;      // 突進 (Attack 1)
    float damageShoot = 10.0f;     // 弾幕 (Attack 2)
    float damageHammer = 25.0f;    // ハンマー (Attack 3)
    float damageWall = 30.0f;      // 壁挟み込み (Attack 4)
    float damageHumanoid = 35.0f;  // 巨像攻撃 (Attack 5)
    float damageLaser = 40.0f;     // 極太レーザー (Attack 6)
    float damageAbsorb = 15.0f;    // 吸収弾幕 (Attack 7)
    float damageFinal = 50.0f;     // 最終メテオ (Attack 8)
    float damageFunnels = 12.0f;   // ファンネル (Attack 9)
    float damageSlime = 8.0f;      // スライム体当たり
    float damageBomb = 30.0f;      // ボム爆発
    float damageBombReflect = 20.0f; // ボム跳ね返しボスダメージ
    float maxBarrierHp = 100.0f;   // ボス最大バリアHP
    float maxArmorBlockHp = 100.0f; // ボス最大装甲ブロックHP
    float stunDuration = 6.0f;     // スタン時間（秒）
    float crashStunDuration = 3.0f; // 突進がマップブロックに衝突した時のスタン時間（秒）
    int lowArmorThreshold = 3;     // 残り装甲ブロック数がこの個数以下の場合に発動
    int lowArmorAbsorbRate = 90;   // 低装甲時の吸収攻撃(ID: 7)の発動確率（0〜100%）

    std::vector<AttackWeight> phase1Attacks; // 第1形態 of 攻撃抽選テーブル (HP > 50%)
    std::vector<AttackWeight> phase2Attacks; // 第2形態 of 攻撃抽選テーブル (HP <= 50%)

    // JSON変換用
    void ToJson(json& j) const {
        j["damageRush"] = damageRush;
        j["damageShoot"] = damageShoot;
        j["damageHammer"] = damageHammer;
        j["damageWall"] = damageWall;
        j["damageHumanoid"] = damageHumanoid;
        j["damageLaser"] = damageLaser;
        j["damageAbsorb"] = damageAbsorb;
        j["damageFinal"] = damageFinal;
        j["damageFunnels"] = damageFunnels;
        j["damageSlime"] = damageSlime;
        j["damageBomb"] = damageBomb;
        j["damageBombReflect"] = damageBombReflect;
        j["maxBarrierHp"] = maxBarrierHp;
        j["maxArmorBlockHp"] = maxArmorBlockHp;
        j["stunDuration"] = stunDuration;
        j["crashStunDuration"] = crashStunDuration;
        j["lowArmorThreshold"] = lowArmorThreshold;
        j["lowArmorAbsorbRate"] = lowArmorAbsorbRate;

        json j1 = json::array();
        for (const auto& a : phase1Attacks) {
            j1.push_back({ {"id", a.id}, {"weight", a.weight} });
        }
        j["phase1Attacks"] = j1;

        json j2 = json::array();
        for (const auto& a : phase2Attacks) {
            j2.push_back({ {"id", a.id}, {"weight", a.weight} });
        }
        j["phase2Attacks"] = j2;
    }
    void FromJson(const json& j) {
        if (j.contains("damageRush")) damageRush = j["damageRush"];
        if (j.contains("damageShoot")) damageShoot = j["damageShoot"];
        if (j.contains("damageHammer")) damageHammer = j["damageHammer"];
        if (j.contains("damageWall")) damageWall = j["damageWall"];
        if (j.contains("damageHumanoid")) damageHumanoid = j["damageHumanoid"];
        if (j.contains("damageLaser")) damageLaser = j["damageLaser"];
        if (j.contains("damageAbsorb")) damageAbsorb = j["damageAbsorb"];
        if (j.contains("damageFinal")) damageFinal = j["damageFinal"];
        if (j.contains("damageFunnels")) damageFunnels = j["damageFunnels"];
        if (j.contains("damageSlime")) damageSlime = j["damageSlime"];
        if (j.contains("damageBomb")) damageBomb = j["damageBomb"];
        if (j.contains("damageBombReflect")) damageBombReflect = j["damageBombReflect"];
        if (j.contains("maxBarrierHp")) maxBarrierHp = j["maxBarrierHp"];
        if (j.contains("maxArmorBlockHp")) maxArmorBlockHp = j["maxArmorBlockHp"];
        if (j.contains("stunDuration")) stunDuration = j["stunDuration"];
        if (j.contains("crashStunDuration")) crashStunDuration = j["crashStunDuration"];
        if (j.contains("lowArmorThreshold")) lowArmorThreshold = j["lowArmorThreshold"];
        if (j.contains("lowArmorAbsorbRate")) lowArmorAbsorbRate = j["lowArmorAbsorbRate"];

        if (j.contains("phase1Attacks")) {
            phase1Attacks.clear();
            for (const auto& item : j["phase1Attacks"]) {
                AttackWeight a;
                if (item.contains("id")) a.id = item["id"];
                if (item.contains("weight")) a.weight = item["weight"];
                phase1Attacks.push_back(a);
            }
        }
        if (j.contains("phase2Attacks")) {
            phase2Attacks.clear();
            for (const auto& item : j["phase2Attacks"]) {
                AttackWeight a;
                if (item.contains("id")) a.id = item["id"];
                if (item.contains("weight")) a.weight = item["weight"];
                phase2Attacks.push_back(a);
            }
        }
    }
};

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
        block->SetCollisionAttribute(kGround);
        block->SetCollisionMask(kPlayer);
        block->SetEnemyType("BossArmor"); // 属性を明確にする

        blockHps_.push_back(attackParams_.maxArmorBlockHp); // 設定されたHPを登録
        blockBroken_.push_back(false);
        armorBreakMotions_.push_back({});
    }

    void SetArmorAttackCollisionActive(bool active, bool includeGround = false) {
        uint32_t attribute = active ? kEnemyAttack : kGround;
        if (active && includeGround) {
            attribute |= kGround;
        }
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            Object3d* block = armorBlocks_[i];
            if (i < blockBroken_.size() && blockBroken_[i]) {
                continue;
            }
            if (block) {
                block->SetCollisionAttribute(attribute);
            }
        }
    }

    float GetHp() const { return param_.has_value() ? param_->hp : 1000.0f; }
    float GetMaxHp() const { return param_.has_value() ? param_->maxHp : 1000.0f; }

    // バリアHP
    float GetBarrierHp() const { return barrierHp_; }
    float GetMaxBarrierHp() const { return maxBarrierHp_; }
    void SetBarrierHp(float hp) { barrierHp_ = hp; }

    void TakeBarrierDamage(float damage, Object3d* hitBlock = nullptr);
    void TakeBodyDamage(float damage);

    // ==========================================
    // 攻撃クラスがボスの部品をいじるためのゲッター
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
    // マップブロックを登録・取得するゲッター
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
    // 最終奥義が終わったことを伝えるためのセッター
    // ==========================================
    void SetWaitingForDeath(bool waiting) { isWaitingForDeath_ = waiting; }

    void SetWaitingForFinisher(bool waiting) { isWaitingForFinisher_ = waiting; }
    bool IsWaitingForFinisher() const { return isWaitingForFinisher_; }
    void StartFinisherFall() {
        isFinisherFalling_ = true;
        finisherFallVelocity_ = 0.0f;
        finisherBounceCount_ = 0;
    }
    bool IsFinisherFalling() const { return isFinisherFalling_; }


    void TriggerCrashStun();   // 自爆スタンの誘発
    void StartDeathSequence(); // 死亡演出の開始
    void ShowCrackedCore();    // 段階2：亀裂モデルへの差し替え

    void StartBattle(); // シーン側から「戦闘開始」の合図を送る関数
    bool IsBattleStarted() const { return isBattleStarted_; }

    void StartAppearance(); // 登場演出をスタートする関数
    bool IsAppearing() const { return isAppearing_; }

    State GetState() const { return state_; }

    void ActuallySpawnShards();
    void UpgradeToFunnel(Object3d* block); // 吸収したブロックをファンネル仕様（8分割）にアップグレードする

    bool IsCompletelyDead() const { return isCompletelyDead_; }
    bool IsDyingSequence() const { return deathPhase_ > 0; }

    bool IsHpHalfEventActive() const { return isHpHalfEventActive_; }

    // --- 攻撃力パラメータ管理 ---
    BossAttackParams& GetAttackParams() { return attackParams_; }
    void LoadAttackParams();
    void SaveAttackParams();
    float GetAttackDamage() const override { return attackParams_.damageRush; }

    void FullyRecoverBarrierAndArmor();
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


    // ==================================================
    // 内部コンポーネント・変数
    // ==================================================
    std::unique_ptr<GhostDirector> director_; // 演出・モーション制御用の監督
    SceneManager* sceneManager_ = nullptr;    // エディタ操作/プレイ状態の判定用

    State state_ = State::Idle;               // 現在のステート
    bool isFirstFrame_ = true;                // 初回更新フラグ

    std::vector<Object3d*> armorBlocks_;

    // ==========================================
    // ダウン(Weak)演出用のアニメーション変数（これだけ残す）
    // ==========================================
    float animTimer_ = 0.0f;
    std::vector<Vector3> blockStartPos_;
    std::vector<Vector3> blockTargetPos_;
    std::vector<Vector3> blockStartRot_;
    std::vector<Vector3> blockTargetRot_;
    Vector3 startBattlePos_ = { 0.0f, 4.0f, 0.0f };
    Vector3 startIdlePos_ = { 0.0f, 4.0f, 0.0f };

    float barrierHp_ = 100.0f;
    float maxBarrierHp_ = 100.0f;
    bool s_isTimeStopped_ = false;

    // エディターで作った予兆エリアを参照するためのポインタ
    Object3d* warningArea_ = nullptr;

    Vector4 originalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 greenColor_ = { 0.0f, 1.0f, 0.0f, 1.0f };
    std::vector<Vector4> savedBlockColors_;

    // ==========================================
    // 現在実行中の新しい攻撃クラスを保持するポインタ
    // ==========================================
    std::unique_ptr<BaseBossAttack> currentAttack_ = nullptr;

    // ==========================================
    // ブロックごとのHPと破壊状態
    // ==========================================
    std::vector<float> blockHps_;
    std::vector<bool> blockBroken_;

    struct ArmorBreakMotion {
        struct ChildPiece {
            Object3d* object = nullptr;
            bool landed = false;
            bool rolling = false;
            int bounceCount = 0;
            float rollTimer = 0.0f;
            float landedTimer = 0.0f;
            float groundY = 0.5f;
            Vector3 velocity = { 0.0f, 0.0f, 0.0f };
            Vector3 angularVelocity = { 0.0f, 0.0f, 0.0f };
            Vector3 position = { 0.0f, 0.0f, 0.0f };
            Vector3 rotation = { 0.0f, 0.0f, 0.0f };
            Vector3 baseScale = { 1.0f, 1.0f, 1.0f };
            Vector3 landedScale = { 1.0f, 1.0f, 1.0f };
            Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        };

        bool active = false;
        bool landed = false;
        bool rolling = false;
        float timer = 0.0f;
        float rollTimer = 0.0f;
        float landedTimer = 0.0f;
        float sparkTimer = 0.0f;
        float groundY = 0.5f;
        Vector3 velocity = { 0.0f, 0.0f, 0.0f };
        Vector3 angularVelocity = { 0.0f, 0.0f, 0.0f };
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };
        Vector3 baseScale = { 1.0f, 1.0f, 1.0f };
        Vector3 landedScale = { 1.0f, 1.0f, 1.0f };
        Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        std::vector<ChildPiece> childPieces;
    };
    std::vector<ArmorBreakMotion> armorBreakMotions_;

    // マップブロックのリスト
    std::vector<MapBlock*> mapBlocks_;

    // ==========================================
    // 最終奥義とトドメに関するフラグ
    // ==========================================
    bool isFinalPhase_ = false;       // HP1になって発狂中か？
    bool isWaitingForDeath_ = false;  // 必殺技が終わってトドメ待ちか？
    bool isWaitingForFinisher_ = false; // 大技終了後のトドメ待ちモード
    bool isFinisherFalling_ = false;
    float finisherFallVelocity_ = 0.0f;
    int finisherBounceCount_ = 0;

    // ▼ 先ほどのアクセス違反（クラッシュ）を完全に防ぐための安全装置
    bool isShardSpawnRequested_ = false;

    // ==========================================
    // パーティクルのエミッターを保持
    // ==========================================
    std::vector<std::unique_ptr<GPUParticleEmitter>> particleEmitters_;

    // コアとブロックを繋ぐエネルギーライン
    std::vector<Object3d*> tetherBeams_;
    std::vector<Vector3> prevBlockPositions_;

    // ==========================================
    // 破片演出用の構造体と変数
    // ==========================================
    struct CorePiece {
        Object3d* obj; // 生ポインタでOK（実体はシーンが管理する）
        Vector3 velocity;
        Vector3 rotSpeed;
    };
    std::vector<CorePiece> corePieces_;

    bool isCoreBroken_ = false;  // 割れたかどうかのフラグ
    float deathTimer_ = 0.0f;    // 割れたあとの退場タイマー

    // 演出用の関数
    void BreakCore();
    void UpdateCorePieces(float deltaTime);
    void StartArmorBlockBreak(size_t index);
    void UpdateBrokenArmorBlocks(float deltaTime);
    void SetBlockColor(Object3d* block, const Vector4& color);
    void SaveOriginalColors();


    int deathPhase_ = 0;       // 0: 生存, 1: 静止, 2: 亀裂, 3: 爆散
    float sequenceTimer_ = 0.0f; // 各フェーズの1秒を測るタイマー

    bool isBattleStarted_ = false; // 戦闘開始フラグ（最初は false）

    void UpdateAppearance(float deltaTime); // 演出中の更新処理
    void UpdateTethers(float deltaTime);    // 結線エフェクトの更新

    bool isAppearing_ = false;  // 登場演出中かどうか
    bool isWaitingForDirector_ = false; // ディレクターのアニメーション終了待ちかどうか
    int appearancePhase_ = 0;   // 演出の進行度
    float appearanceTimer_ = 0.0f;
    bool isCompletelyDead_ = false;
    float assemblyTimer_ = 0.0f;

    // --- HP半分時の演出用 ---
    enum class HpHalfEventPhase {
        None,
        WaitIdle,     // 1フレーム待機モーションに戻す
        Falling,      // 落下
        Lying,        // ダウン
        Recovery,     // 起き上がり・首振り
        Pulsing,      // 鼓動・強化
        Reassembling, // ブロック再集結
        Finishing     // 終了
    };
    HpHalfEventPhase hpHalfPhase_ = HpHalfEventPhase::None;
    bool isHpHalfTriggered_ = false;
    bool isHpHalfEventActive_ = false;
    float hpHalfEffectTimer_ = 0.0f;
    bool isPlayerRotated_ = false;  // 演出中に一度だけ向きを合わせるためのフラグ
    bool hasResetColorPreAttack_ = false; // 攻撃の1秒前に色を水色に戻したかどうかのフラグ
    PostEffect::Params basePostEffectParams_{};

    // 演出用の一時変数
    std::vector<Vector3> fallingBlockVelocities_;
    Vector3 originalCoreRotation_;
    Vector3 originalCorePosition_;
    
    static constexpr float kBaseSpeedMultiplier = 1.5f; // ボス固有の速度倍率
    BossAttackParams attackParams_;

    // 自爆スタン用のフラグとタイマー
    bool isCrashStun_ = false;
    float crashStunTimer_ = 0.0f;
};
