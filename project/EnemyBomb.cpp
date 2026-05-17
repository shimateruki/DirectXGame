#include "EnemyBomb.h"
#include "game/actor/player/Player.h" // プレイヤーをキャストして判定する場合に備えて
#include "Event.h"          
#include "EventManager.h" 
#include "CollisionConfig.h"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <MeshEffectManager.h>

void EnemyBomb::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    state_ = State::Chase;
    igniteTimer_ = 0.0f;

    // スケールと色を記憶（BaseEnemyにdefaultColor_があるのでスケールだけ追加記録）
    if (GetTransform()) {
        defaultScale_ = GetTransform()->scale;
    }

    // JSONファイルから攻撃パラメータを読み込んで攻撃力を設定
    float bombDamage = 30.0f; // デフォルト値
    std::string filePath = "Resources/json/enemy/boss_attack_params.json";
    if (std::filesystem::exists(filePath)) {
        std::ifstream ifs(filePath);
        if (ifs.is_open()) {
            json j;
            ifs >> j;
            if (j.contains("damageBomb")) {
                bombDamage = j["damageBomb"];
            }
        }
    }
    SetAttackDamage(bombDamage);
}

void EnemyBomb::Update(float deltaTime) {
    if (!GetTransform() || !target_ || !target_->GetTransform()) {
        BaseEnemy::Update(deltaTime);
        return;
    }

    Vector3 myPos = GetTransform()->translate;
    Vector3 targetPos = target_->GetTransform()->translate;

    Vector3 dir = { targetPos.x - myPos.x, targetPos.y - myPos.y, targetPos.z - myPos.z };
    float distance = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

    switch (state_) {
    case State::Chase:
        // プレイヤーを追いかける処理
        if (distance > 0.0f) {
            dir.x /= distance; dir.y /= distance; dir.z /= distance;
            if (param_.has_value()) {
                GetTransform()->translate.x += dir.x * param_->speed;
                GetTransform()->translate.z += dir.z * param_->speed;
            }
        }

        if (distance <= triggerDistance_) {
            state_ = State::Ignited;
        }
        break;

    case State::Ignited:
        igniteTimer_ += deltaTime;

        // 【1. カラーの点滅処理】
        // 爆発間際 (全体の70%の時間が経過) は点滅を速くする
        {
            float blinkInterval = (igniteTimer_ > igniteDuration_ * 0.7f) ? 0.08f : 0.2f;
            if (std::fmod(igniteTimer_, blinkInterval) < blinkInterval / 2.0f) {
                SetColor({ 1.0f, 0.0f, 0.0f, 1.0f }); // 赤色
            }
            else {
                SetColor(defaultColor_);              // 元の色
            }
        }

        // 【2. スケールの増減 (鼓動)】
        // sin波を使って、元のスケールから最大1.3倍まで「ドクンドクン」と大きくする
        {
            // sin波の絶対値を取ることで、常にプラス方向への膨張にする
            float pulse = 0.3f * std::abs(std::sin(igniteTimer_ * 15.0f));
            SetScale({
                defaultScale_.x + pulse,
                defaultScale_.y + pulse,
                defaultScale_.z + pulse
                });
        }

        // カウントダウン終了で爆発状態へ
        if (igniteTimer_ >= igniteDuration_) {
            state_ = State::Exploded;

            // 爆発の瞬間に見た目をリセットしておく
            SetColor(defaultColor_);
            SetScale(defaultScale_);
        }
        break;
    case State::Exploded:
        if (!hasExploded_) {
            hasExploded_ = true;

            // 1. 最初はごく小さな当たり判定(0.1f)からスタート
            SetCollisionRadius(0.1f);
            SetCollisionAttribute(kEnemyAttack);
            SetCollisionMask(kPlayer); // 床との判定が切れる

            // =========================================================
            // ★床をすり抜けて落下しないように、速度と重力をゼロにする！
            // =========================================================
            velocity_ = { 0.0f, 0.0f, 0.0f };
            if (param_.has_value()) {
                param_->gravity = 0.0f;
            }

            SetIsVisible(false);

            // エフェクト発生 (thisなしに戻す)
            MeshEffectManager::GetInstance()->SpawnEffect("Resources/json/effect/effect_bakuhatu.json");
        }

        explosionTimer_ += deltaTime;

        // =========================================================
        // ★ 当たり判定を 0.1f から 3.0f へ徐々に広げる
        // =========================================================
        float t = std::min(explosionTimer_ / explosionDuration_, 1.0f);
        float currentRadius = Math::Lerp(0.1f, explosionRadius_, t);
        SetCollisionRadius(currentRadius);
        // =========================================================

        // エフェクトの終了と同時に完全に消滅
        if (explosionTimer_ >= explosionDuration_) {
            isDead = true;
            SetCollisionAttribute(0);    // 攻撃属性を消す
            SetCollisionMask(0);         // 誰とも当たらないようにする
            SetCollisionRadius(0.0f);    // サイズをゼロにする
        }
        break;
    }
    BaseEnemy::Update(deltaTime);

}
std::unique_ptr<Object3d> EnemyBomb::Clone() const {
    auto clone = std::make_unique<EnemyBomb>();
    if (this->param_.has_value()) clone->param_ = this->param_;
    return clone;
}