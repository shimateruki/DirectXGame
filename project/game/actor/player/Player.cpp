#define NOMINMAX
#include "Player.h"
#include "Model.h"
#include "CollisionConfig.h"
#include "engine/utility/math/Math.h"
#include "EventManager.h"
#include "CameraManager.h"
#include "IMoveStrategy.h"
#include "PlayerState.h"
#include "PostEffect.h"
#include "AudioPlayer.h"
#include <DebugConsole.h>
#include <algorithm>
#include <fstream>
#include <filesystem>

// =================================================================
// 初期化・更新・描画
// =================================================================

void Player::Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem)
{
    // 親クラス(Character)の初期化
    Character::Initialize(common);

    // 攻撃パラメータのロード
    LoadAttackParams();

    // 外部システムの依存注入
    inputManager_ = inputManager;
    particleSystem_ = particleSystem;

    // 自機としての基本設定
    SetClassName("Player");

    // 移動コンポーネントの構築
    mover_ = std::make_unique<PlayerMover>();
    mover_->Initialize(this, inputManager, particleSystem);

    // 前回シーンの静的な姿勢キャッシュが残らないように初期化する。
    ResetPlayerStateStatics();

    // SEのロード
    seAvoidHandle_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Player/PlayerAvoid.mp3");
    seJumpHandle_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Player/PlayerJump.mp3");
    seMoveHandle_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Player/PlayerMove.mp3");
    seSwingMiss1Handle_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Player/PlayerSwingMiss1.mp3");
    seSwingMiss2Handle_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Player/PlayerSwingMiss2.mp3");
    seSwordHandle_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Player/PlayerSword.mp3");
    seDownAttack1Handle_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Player/PlayerDownAttack1.mp3");
    seDownAttack2Handle_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Player/PlayerDownAttack2.mp3");

    // ステートマシン初期化 (待機状態からスタート)
    ChangeState(std::make_unique<PlayerStateIdle>());
}

void Player::Update(float deltaTime)
{
    // コンボ時間窓の更新（常に減算）
    if (deltaTime > 0.0f && comboWindowTimer_ > 0.0f)
    {
        comboWindowTimer_ -= deltaTime;
        if (comboWindowTimer_ <= 0.0f) comboWindowTimer_ = 0.0f;
    }

    // 攻撃入力バッファの更新（常に減算）
    if (deltaTime > 0.0f && attackInputBufferTimer_ > 0.0f)
    {
        attackInputBufferTimer_ -= deltaTime;
        if (attackInputBufferTimer_ <= 0.0f)
        {
            attackInputBufferTimer_ = 0.0f;
            attackInputBuffered_ = false;
            attackBufferUsedForStateStart_ = false;
        }
    }

    // 時間が進んでいる（ポーズ中ではない）時だけ、操作やアニメーションを更新
    if (deltaTime > 0.0f)
    {
        // 1. 無敵タイマーの管理 (ダメージ被弾時)
        if (damageCooldownTimer_ > 0.0f)
        {
            damageCooldownTimer_ -= deltaTime;
            if (damageCooldownTimer_ <= 0.0f)
            {
                damageCooldownTimer_ = 0.0f;
                // 被弾無敵だけを解除し、ダッシュ無敵には触れない。
                SetDamageInvincible(false);
            }
        }

        // 2. 移動制御の更新 (Strategy Pattern)
        if (isControlActive_ && mover_)
        {
            mover_->Update(deltaTime);
        }

        // 3. アニメーション・状態の更新 (State Pattern)
        if (state_)
        {
            state_->Update(this);
        }
        else
        {
            DebugConsole::GetInstance()->AddLog("[ERROR] Player state_ is NULL!");
        }
    }

    // 4. 物理フラグ有効時のみ親クラスの更新（物理演算など）を行う
    if (isPhysicsActive_)
    {
        Character::Update(deltaTime);
    }
    else
    {
        // 物理無効時は行列計算のみ更新
        Object3d::Update(deltaTime);
    }
    if (transform_.translate.y < -40.0f) {
        // 1. 指定の座標(0, 5, -55)へ座標をセット
        transform_.translate = { 0.0f, 5.0f, -55.0f };

        // 2. 落下時の勢い（速度）が残っているとワープ直後にまた落ちるため、速度をリセット
        velocity_ = { 0.0f, 0.0f, 0.0f };

        UpdateLocalMatrix();
        // 3. 座標の変更を即座に行列へ反映させる（描画や次フレームの計算のズレ防止）
        UpdateWorldMatrix();

        DebugConsole::GetInstance()->AddLog("【SYSTEM】 プレイヤーが落下したため、初期地点に復帰させました");
    }
    // =======================================================
    // 5. モデルアニメ適用後の最終上書き処理 (PostUpdate)
    // =======================================================
    if (state_ && deltaTime > 0.0f)
    {
        // Idleステートなら待機時の微細な動きを適用
        if (auto idle = dynamic_cast<PlayerStateIdle*>(state_.get()))
        {
            idle->ApplyPostUpdate(this, deltaTime);
        }
        // Runステートなら走りに合わせた腕や脚の制御を適用
        else if (auto run = dynamic_cast<PlayerStateRun*>(state_.get()))
        {
            run->ApplyPostUpdate(this, deltaTime);
        }
    }
    // =======================================================
        // 6. 瀕死・死亡エフェクト (Danger Vignette & Blackout)
        // =======================================================
    float hp = GetHp();
    float maxHp = GetMaxHp();

    if (maxHp > 0.0f) {
        float hpRatio = hp / maxHp;

        PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();

        if (postParams) {
            if (hp <= 0.0f) {
                if (!dynamic_cast<PlayerStateDead*>(state_.get())) {
                    // isDead = true; // 削除されないようにコメントアウト
                    deathTimer_ = 0.0f;
                    ChangeState(std::make_unique<PlayerStateDead>());
                    DebugConsole::GetInstance()->AddLog("Player DEAD! 死亡演出開始");
                }

                deathTimer_ += deltaTime;

                // 赤枠の濃さは固定し、視界の邪魔をしない
                postParams->dangerVignette = 0.8f;

                // 最初の3.5秒だけ点滅させる
                if (deathTimer_ <= 3.5f) {
                    // ===================================================
                    // 最初の3.5秒：鋭く不規則な黒の点滅 (Blackout Pulse)
                    // ===================================================
                    float t = deathTimer_;
                    float pulse1 = std::pow(std::max(0.0f, std::sin(t * 2.5f)), 16.0f);
                    float pulse2 = std::pow(std::max(0.0f, std::sin(t * 2.5f - 0.4f)), 16.0f);
                    float mask = std::sin(t * 1.3f) * 0.5f + 0.5f;
                    float blink = pulse1 + (pulse2 * mask);
                    postParams->blackout = std::min(blink * 0.85f, 0.85f);
                }
                else {
                    // ===================================================
                    // 3.5秒後：意識が完全に途絶え、薄暗いまま固定
                    // ===================================================
                    // GameOverテキストが見えるように 0.6f 程度の暗さに滑らかに落ち着かせる
                    postParams->blackout = std::min(postParams->blackout + (deltaTime * 0.5f), 0.6f);
                }

                // 画面の邪魔になる歪みやフラッシュは全てオフ
                postParams->wobbleIntensity = 0.0f;
                postParams->damageFlash = 0.0f;
            }
            else if (hpRatio <= 0.2f) {
                // ---------------------------------------------------
                // 瀕死時 (20%以下)
                // ---------------------------------------------------
                isDead = false;
                deathTimer_ = 0.0f;

                float dangerLevel = 1.0f - (hpRatio / 0.2f);
                postParams->dangerVignette = dangerLevel * 1.5f;

                // 生きている間は黒点滅させない
                postParams->blackout = 0.0f;

                postParams->wobbleIntensity = 0.0f;
                postParams->damageFlash = 0.0f;
  
            }
            else {
                // ---------------------------------------------------
                // 安全圏内
                // ---------------------------------------------------
                isDead = false;
                deathTimer_ = 0.0f;

                postParams->dangerVignette = 0.0f;
                postParams->blackout = 0.0f; // リセット
                postParams->wobbleIntensity = 0.0f;
                postParams->damageFlash = 0.0f;
            }
        }
    }

}
void Player::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource)
{
    Character::Draw(pointLightResource, spotLightResource);
}

// =================================================================
// 衝突処理
// =================================================================

bool Player::OnCollision(Object3d* other)
{
    if (!other) return false;

    // 衝突相手の属性を取得
    uint32_t attribute = 0;
    if (other->GetCollider())
    {
        attribute = other->GetCollider()->GetAttribute();
    }

    // ① まずプレイヤー本体の当たり判定を計算
    CollisionInfo info = CheckCollision(other);

    // ② 子オブジェクト（パーツ）の当たり判定も全てチェックする
    for (Object3d* child : GetChildren())
    {
        if (!child) continue;
        CollisionInfo childInfo = child->CheckCollision(other);
        if (childInfo.isColliding)
        {
            // パーツがぶつかっていた場合、本体よりめり込みが深ければ
            // そのパーツの押し出し情報（法線とめり込み量）を採用して親を動かす
            if (!info.isColliding || childInfo.penetration > info.penetration)
            {
                info = childInfo;
            }
        }
    }

    // 本体もパーツも当たっていなければ終了
    if (!info.isColliding)
    {
        return false;
    }

    // =======================================================
    // 1. 物理挙動の適用 (ソリッドな壁や床からの押し戻し)
    // 無敵中でも壁抜けは厳禁なので、一番最初に処理します。
    // =======================================================
    if (isPhysicsActive_ && (attribute & kAllSolid))
    {
        ApplyPhysicsCollision(info, attribute);
    }

    // 2. 敵の攻撃に接触した場合
    if (attribute & kEnemyAttack)
    {
        // タイマーと「総合的な無敵状態」の両方をチェック
        if (damageCooldownTimer_ <= 0.0f && !IsInvincible())
        {
            // ダメージイベントを発行（GameRule.cpp で HP 減少等が処理されます）
            DamageEvent dmgEvent;
            dmgEvent.target = this;
            dmgEvent.attacker = other;
            dmgEvent.damageAmount = other->GetAttackDamage();
            EventManager::GetInstance()->Dispatch(dmgEvent);

            // 無敵時間をセット
            damageCooldownTimer_ = 1.5f;

            // 被弾無敵だけを立て、回避ダッシュ側の無敵とは分けて扱う。
            SetDamageInvincible(true);

            // ノックバックと被弾モーションを再生する。
            ChangeState(std::make_unique<PlayerStateDamage>());
        }
    }
    // =======================================================
    // 3. ギミック・汎用イベントの発行
    // =======================================================
    // ワープやスイッチなど、ダメージ以外の判定のために通知します。
    PlayerHitEvent hitEvent;
    hitEvent.me = this;
    hitEvent.hitObject = other;
    hitEvent.normal = info.normal;
    EventManager::GetInstance()->Dispatch(hitEvent);

    return true;
}
// =================================================================
// 移動制御 (Strategy Pattern)
// =================================================================

void Player::SetMoveStrategy(std::unique_ptr<IMoveStrategy> strategy)
{
    if (mover_)
    {
        mover_->SetStrategy(std::move(strategy));
    }
}

// =================================================================
// アニメーション・状態管理 (State Pattern)
// =================================================================

void Player::ChangeState(std::unique_ptr<IAnimationState> newState)
{
    // 現在の状態を終了
    if (state_)
    {
        state_->Exit(this);
    }

    // 状態を切り替えて開始
    state_ = std::move(newState);

    if (state_)
    {
        state_->Enter(this);
    }
    else
    {
        DebugConsole::GetInstance()->AddLog("[ERROR] Failed to set new state.");
    }
}

void Player::PlayAnimation(const std::string& animName, bool loop)
{
    // 既に同じアニメーションが再生中ならリセットしない (滑らかな遷移のため)
    if (animName_ != animName)
    {
        animName_ = animName;
        animationTime_ = 0.0f;
    }
    isAnimLoop_ = loop;
}

// =======================================================
// 無敵関連の実装
// =======================================================
void Player::SetDamageInvincible(bool inv) {
    isDamageInvincible_ = inv;
    UpdateColor(); // 状態が変わったら色を更新
}

void Player::SetDashInvincible(bool inv) {
    isDashInvincible_ = inv;
    UpdateColor(); // 状態が変わったら色を更新
}

void Player::UpdateColor() {
    const bool isInvincible = isDamageInvincible_ || isDashInvincible_;

    if (isInvincible && !hasSavedInvincibleColors_) {
        savedColor_ = GetColor();
        childSavedColors_.clear();
        for (Object3d* child : GetChildren()) {
            if (child) {
                childSavedColors_[child] = child->GetColor();
            }
        }
        hasSavedInvincibleColors_ = true;
    }

    if (!isInvincible) {
        if (hasSavedInvincibleColors_) {
            SetColor(savedColor_);
            for (auto& entry : childSavedColors_) {
                if (entry.first) {
                    entry.first->SetColor(entry.second);
                }
            }
            childSavedColors_.clear();
            hasSavedInvincibleColors_ = false;
        }
        return;
    }

    Vector4 invincibleColor = isDamageInvincible_
        ? Vector4{ 1.0f, 0.0f, 0.0f, 1.0f }
        : Vector4{ 0.0f, 0.45f, 1.0f, 1.0f };
    SetColor(invincibleColor);
    for (Object3d* child : GetChildren()) {
        if (child) child->SetColor(invincibleColor);
    }
    return;
}

// =======================================================
// コンボ時間窓 API 実装
// =======================================================
void Player::StartComboWindow(float duration)
{
    comboWindowTimer_ = std::max(0.0f, duration);
}

bool Player::IsComboWindowActive() const
{
    return comboWindowTimer_ > 0.0f;
}

// =======================================================
// 攻撃入力バッファ API 実装
// =======================================================
void Player::RecordAttackInput(float duration)
{
    attackInputBuffered_ = true;
    attackInputBufferTimer_ = std::max(0.0f, duration);
    attackBufferUsedForStateStart_ = false;
}

void Player::MarkAttackBufferUsedForStateStart()
{
    if (attackInputBuffered_) attackBufferUsedForStateStart_ = true;
}

bool Player::ConsumeBufferedAttackInput()
{
    if (attackInputBuffered_ && !attackBufferUsedForStateStart_)
    {
        // consume
        attackInputBuffered_ = false;
        attackInputBufferTimer_ = 0.0f;
        attackBufferUsedForStateStart_ = false;
        return true;
    }
    return false;
}
void Player::LoadAttackParams() {
    std::string filePath = "Resources/json/player/attack_params.json";
    if (!std::filesystem::exists(filePath)) {
        SaveAttackParams(); // デフォルト値で作成
        return;
    }

    std::ifstream ifs(filePath);
    if (ifs.is_open()) {
        json j;
        ifs >> j;
        attackParams_.FromJson(j);
    }
}

void Player::SaveAttackParams() {
    std::string dirPath = "Resources/json/player";
    if (!std::filesystem::exists(dirPath)) {
        std::filesystem::create_directories(dirPath);
    }

    std::string filePath = dirPath + "/attack_params.json";
    std::ofstream ofs(filePath);
    if (ofs.is_open()) {
        json j;
        attackParams_.ToJson(j);
        ofs << j.dump(4);
    }
}
