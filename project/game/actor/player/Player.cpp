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
#include "GameDataManager.h"
#include "SceneManager.h"
#include"Winapp.h"
#include <DebugConsole.h>
#include <algorithm>
#include <CollisionManager.h>
#include "DirectXCommon.h"

// =================================================================
// 初期化・更新・描画
// =================================================================

void Player::Initialize(Object3dCommon* common, InputManager* inputManager, ParticleSystem* particleSystem, SpriteCommon* spriteCommon)
{
    // 親クラス(Character)の初期化
    Character::Initialize(common);

    // 外部システムの依存注入
    inputManager_ = inputManager;
    particleSystem_ = particleSystem;

    // 自機としての基本設定
    SetClassName("Player");
    jumpCount_ = 0;

    // 移動コンポーネントの構築
    mover_ = std::make_unique<PlayerMover>();
    mover_->Initialize(this, inputManager, particleSystem);

    // ステートマシン初期化 (待機状態からスタート)
    ChangeState(std::make_unique<PlayerStateIdle>());

    // フック用の到達地点マーカーを初期化
    if (common) {
        hookMarker_ = std::make_unique<Object3d>();
        hookMarker_->Initialize(common);
        hookMarker_->SetModel("slimeBody.gltf");
        // スライムっぽい半透明な緑色
        hookMarker_->SetColor({ 0.3f, 1.0f, 0.5f, 0.6f });
        hookMarker_->SetIsVisible(false);
        // 当たり判定は不要
        hookMarker_->SetCollisionAttribute(0);
        hookMarker_->SetCollisionMask(0);
        // マーカーサイズを少し小さめに
        hookMarker_->GetTransform()->scale = { 0.5f, 0.5f, 0.5f };
    }
}

void Player::Update(float deltaTime)
{
    // 初回更新時に初期位置と初期回転を記録
    if (isFirstUpdate_) {
        respawnPosition_ = transform_.translate;
        baseRotation_ = transform_.rotate; // モデルの初期回転（逆さま防止など）を保存
        isFirstUpdate_ = false;
    }

    // 時間が進んでいる（ポーズ中ではない）時だけ、操作や状態を更新
    if (deltaTime > 0.0f)
    {
        // 1. 無敵タイマーの管理 (ダメージ被弾時)
        if (damageCooldownTimer_ > 0.0f) {
            damageCooldownTimer_ -= deltaTime;
            if (damageCooldownTimer_ <= 0.0f) {
                damageCooldownTimer_ = 0.0f;
                SetDamageInvincible(false); // 被弾無敵フラグを解除
            }
        }

        // 2. 移動制御の更新
        if (isControlActive_ && mover_) {
            // 右クリック中は移動入力を受け付けず、水平移動を停止させる
            if (inputManager_->IsMouseButtonPressed(1)) {
                // 水平速度をゼロにしてその場に留まる（空中浮遊感）
                Vector3 v = GetVelocity();
                SetVelocity({ 0.0f, v.y, 0.0f });

                // フックの狙いをつけるため、プレイヤーの向きをカメラの向いている方向に合わせる
                Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                if (camera) {
                    Vector3 camRot = camera->GetRotation();
                    // Z軸・X軸の「初期の補正回転（逆さま防止など）」を維持した上で、カメラのピッチとヨーを合成
                    SetRotation({ baseRotation_.x + camRot.x, camRot.y, baseRotation_.z });

                    // --- フック到達地点の計算とマーカー表示 ---
                    if (hookMarker_) {
                        Vector3 start = camera->GetEye();

                        // カメラの実際の向き(Forwardベクトル)を計算してレイを飛ばす
                        Vector3 dir;
                        dir.x = std::sin(camRot.y) * std::cos(camRot.x);
                        dir.y = -std::sin(camRot.x);
                        dir.z = std::cos(camRot.y) * std::cos(camRot.x);

                        // 念のため正規化
                        float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                        if (length > 0.0f) {
                            dir.x /= length; dir.y /= length; dir.z /= length;
                        }

                        // フックの最大距離
                        float maxDistance = 150.0f;
                        // 壁(kAllSolid)や敵(kEnemy)を対象にする
                        uint32_t mask = kAllSolid | kEnemy;

                        RaycastHit hit = CollisionManager::GetInstance()->Raycast(start, dir, maxDistance, mask);

                        if (hit.isHit) {
                            hookMarker_->SetIsVisible(true);
                            hookMarker_->GetTransform()->translate = hit.hitPoint;
                            hookMarker_->GetTransform()->scale = { 1.0f, 1.0f, 1.0f };
                            hookMarker_->GetTransform()->rotate = { 0.0f, 0.0f, 0.0f };
                            char buf[256];
                            const char* tag = (hit.hitPoint.y < -1.0f) ? "[FLOOR]" : "[WALL?]";
                            snprintf(buf, sizeof(buf), "Hook: HIT %s start(%.1f, %.1f, %.1f) dir(%.2f, %.2f, %.2f) point(%.1f, %.1f, %.1f)",
                                tag, start.x, start.y, start.z, dir.x, dir.y, dir.z, hit.hitPoint.x, hit.hitPoint.y, hit.hitPoint.z);
                            DebugConsole::GetInstance()->AddLog(buf);
                        }
                        else {
                            // 何も当たらない場合は非表示にする
                            hookMarker_->SetIsVisible(false);
                            DebugConsole::GetInstance()->AddLog("Hook: MISS in air");
                        }

                        // マーカーの行列更新
                        hookMarker_->Update(deltaTime);
                        hookMarker_->UpdateLocalMatrix();
                        hookMarker_->UpdateWorldMatrix();
                    }
                }
            }
            else {
                // 右クリック解除時にマーカーを非表示にしつつフック移動へ
                if (hookMarker_) {
                    if (hookMarker_->GetIsVisible()) {
                        Vector3 targetPos = hookMarker_->GetTransform()->translate;
                        ChangeState(std::make_unique<PlayerStateHook>(targetPos));
                    }
 /*                   hookMarker_->SetIsVisible(false);*/
                }

                // 通常移動時はX軸（ピッチ）とZ軸（ロール）の回転を初期値に戻す
                Vector3 rot = GetRotation();
                rot.x = baseRotation_.x;
                rot.z = baseRotation_.z;
                SetRotation(rot);

                mover_->Update(deltaTime);

            }
        }

        // 3. 状態(State)の更新
        if (state_) {
            state_->Update(this);
        }

        // 4. 死亡判定
        if (GetHp() <= 0.0f && !isDead) {
            isDead = true;
            deathTimer_ = 0.0f;
            ChangeState(std::make_unique<PlayerStateDead>());
            DebugConsole::GetInstance()->AddLog("Player DEAD! 死亡状態へ移行");

            PlayerDeathEvent deathEvent;
            deathEvent.player = this;
            EventManager::GetInstance()->Dispatch(deathEvent);
        }

        // 5. 落下判定
        if (transform_.translate.y < -40.0f && !isDead && isControlActive_) {
            ChangeState(std::make_unique<PlayerStateFallingOut>());
            GameDataManager::GetInstance()->SubtractLife();

            if (GameDataManager::GetInstance()->GetLives() <= 0) {
                SceneManager::GetInstance()->ChangeScene("GAMEOVER");
            }
            else {
                DebugConsole::GetInstance()->AddLog("Fell out of bounds! Life subtracted. Starting Iris Out.");
            }
        }
    }

    if (isControlActive_) {
        // 右クリック入力処理：重力を弱める
        if (inputManager_->IsMouseButtonPressed(1)) {
            SetGravity(10.0f);
        }
        else {
            SetGravity(50.0f);
        }
    }

    // 6. 親クラスの更新 (重力計算・行列計算・衝突リストのリセットなど)
    Character::Update(deltaTime);
}


void Player::DrawUI()
{
}
void Player::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource)
{
    Character::Draw(pointLightResource, spotLightResource);
    // ※フックマーカーはプレイヤーがカメラ外（フラスタムカリング）になった時でも
    // 描画されるように、GamePlayScene 側で直接描画するように変更しました。
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
            // そのパーツの押し出し情報（法線とめり込み量）を採用して親を動かす！
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
    if (attribute & kAllSolid)
    {
        ApplyPhysicsCollision(info, attribute);
    }

    // =======================================================
    // 2. ダメージ処理 / 踏みつけ攻撃 / 突進攻撃
    // =======================================================
    if (attribute & (kEnemy | kEnemyAttack))
    {
        // ★ 突進（タックル）判定
        if ((attribute & kEnemy) && mover_->IsDashing())
        {
            // 敵に大ダメージ
            DamageEvent tackleDmg;
            tackleDmg.target = other;
            tackleDmg.attacker = this;
            tackleDmg.damageAmount = 30.0f; // 踏みつけより強力！

            // 吹き飛ばしベクトル：向いている方向に斜め上に弾き飛ばす
            float yaw = transform_.rotate.y;
            Vector3 pushDir = { std::sin(yaw), 0.4f, std::cos(yaw) };
            tackleDmg.knockbackVelocity = pushDir * 50.0f; // 結構な勢いで飛ばす

            EventManager::GetInstance()->Dispatch(tackleDmg);

            DebugConsole::GetInstance()->AddLog("Slime Tackle! Enemy Blasted.");
            return true; // 突進中はダメージを受けず、相手を倒す
        }

        // ★ 踏みつけ判定: 敵の本体(kEnemy)かつ、上方向から衝突したか
        // 判定を安定させるため、法線のしきい値を0.5(60度)に広げ、速度条件も緩和します
        bool isAbove = GetWorldPosition().y > other->GetWorldPosition().y;
        if ((attribute & kEnemy) && info.normal.y > 0.5f && isAbove)
        {
            // 敵にダメージ (踏みつけ成功！)
            DamageEvent enemyDmg;
            enemyDmg.target = other;
            enemyDmg.attacker = this;
            enemyDmg.damageAmount = 10.0f;
            EventManager::GetInstance()->Dispatch(enemyDmg);

            // プレイヤーを上に跳ね返らせる
            Vector3 v = GetVelocity();
            v.y = 15.0f; // 跳ね返り力を少し強化
            SetVelocity(v);
            ChangeState(std::make_unique<PlayerStateJump>());

            DebugConsole::GetInstance()->AddLog("Stomp Success!");
            return true; // 踏みつけ成功時は自身のダメージ処理をスキップ
        }

        // 通常のダメージ処理 (被弾)
        if (damageCooldownTimer_ <= 0.0f && !IsInvincible())
        {
            // ダメージイベントを発行 (GameRule で HP 減少が処理される)
            DamageEvent dmgEvent;
            dmgEvent.target = this;
            dmgEvent.attacker = other;
            dmgEvent.damageAmount = 1.0f; // ★ 1ダメージ
            EventManager::GetInstance()->Dispatch(dmgEvent);

            // 無敵時間をセット
            damageCooldownTimer_ = 1.0f;
            SetDamageInvincible(true);

            // ★ ノックバック状態へ移行 (衝突法線を利用)
            ChangeState(std::make_unique<PlayerStateDamage>(info.normal));
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
    Vector4 targetColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 基本は白(通常色)

    if (isDamageInvincible_) {
        targetColor = { 1.0f, 0.0f, 0.0f, 1.0f }; // ★ ダメージ中は最優先で「赤」！
    }
    else if (isDashInvincible_) {
        targetColor = { 0.0f, 0.0f, 1.0f, 1.0f }; // ★ 回避中は「青」！
    }

    // 本体と子パーツの色を一括変更
    SetColor(targetColor);
    for (Object3d* child : GetChildren()) {
        if (child) child->SetColor(targetColor);
    }
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