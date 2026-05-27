#include "MapBlock.h"
#include "CollisionConfig.h"
#include "DebugConsole.h"
#include <algorithm>
#include <cmath>
#include "SceneManager.h"
#include "BaseScene.h"
#include "CollisionManager.h"

// ==========================================
// 静的リスト（名簿）の実体を定義
// ==========================================
std::vector<MapBlock*> MapBlock::s_activeBlocks;

// デストラクタで自分を名簿から消す（エラー防止）
MapBlock::~MapBlock() {
    auto it = std::find(s_activeBlocks.begin(), s_activeBlocks.end(), this);
    if (it != s_activeBlocks.end()) {
        s_activeBlocks.erase(it);
    }
}

void MapBlock::Initialize(Object3dCommon* common) {
    Object3d::Initialize(common);
    SetCollisionAttribute(kMapBlock);
    SetCollisionMask(kPlayer | kEnemy);
    SetClassName("MapBlock");
    s_activeBlocks.push_back(this);

    auto laserBeam = std::make_unique<Object3d>();
    laserBeam->Initialize(common);
    laserBeam->SetName("Beam_Cylinder");
    laserBeam->SetModel("Cylinder");
    laserBeam->SetParent(this);
    laserBeam->SetScale({ 0.0f, 0.0f, 0.0f });
    laserBeam->SetCollisionAttribute(0);

    // 自分の子供名簿に登録（攻撃クラスの検索用）
    children_.push_back(laserBeam.get());

    CollisionManager::GetInstance()->AddObject(laserBeam.get());

    // レーザーはシーン管理下に置き、MapBlock側では参照だけを保持する。
    if (BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene()) {
        currentScene->AddObject(std::move(laserBeam));
    }
}

void MapBlock::Update(float deltaTime) {
    // 吸収中もレーザーの座標とスケールを更新する。
    if (laserBeam_) {
        laserBeam_->Update(deltaTime);
    }

    if (isAbsorbed_) return;

    if (breakState_ != BreakState::None) {
        UpdateBreak(deltaTime);
        Object3d::Update(deltaTime);
        return;
    }

    Object3d::Update(deltaTime);
}

void MapBlock::OnAbsorbed() {
    isAbsorbed_ = true;
    SetIsVisible(false);
    // 衝突判定も無効化する
    SetCollisionAttribute(0);
}

void MapBlock::StartBreak(const Vector3& impulse) {
    if (isAbsorbed_ || breakState_ != BreakState::None) {
        return;
    }

    breakState_ = BreakState::Falling;
    breakVelocity_ = impulse;
    if (std::abs(breakVelocity_.x) < 0.001f && std::abs(breakVelocity_.z) < 0.001f) {
        breakVelocity_.x = 2.8f;
        breakVelocity_.z = -2.1f;
    }

    breakAngularVelocity_ = { 9.0f, 7.0f, 6.0f };
    breakTimer_ = 0.0f;
    rollingTimer_ = 0.0f;
    landedTimer_ = 0.0f;
    breakSparkTimer_ = 0.0f;
    breakStartScale_ = GetScale();
    breakStartScaleY_ = breakStartScale_.y;
    breakLandedScale_ = breakStartScale_;
    breakBaseColor_ = GetColor();
    breakGroundY_ = GetTranslate().y - (std::max)(0.1f, GetScale().y * 0.5f);

    SetCollisionAttribute(0);
    SetCollisionMask(0);
}

void MapBlock::UpdateBreak(float deltaTime) {
    breakTimer_ += deltaTime;
    breakSparkTimer_ += deltaTime;

    if (breakState_ == BreakState::Falling) {
        breakVelocity_.y -= 28.0f * deltaTime;

        Vector3 pos = GetTranslate();
        pos.x += breakVelocity_.x * deltaTime;
        pos.y += breakVelocity_.y * deltaTime;
        pos.z += breakVelocity_.z * deltaTime;

        Vector3 rot = GetRotation();
        rot.x += breakAngularVelocity_.x * deltaTime;
        rot.y += breakAngularVelocity_.y * deltaTime;
        rot.z += breakAngularVelocity_.z * deltaTime;

        float halfHeight = (std::max)(0.1f, GetScale().y * 0.5f);
        float bottomY = pos.y - halfHeight;
        if (bottomY <= breakGroundY_) {
            pos.y = breakGroundY_ + halfHeight;
            breakVelocity_.y = 0.0f;
            float horizontalSpeed = std::sqrt(breakVelocity_.x * breakVelocity_.x + breakVelocity_.z * breakVelocity_.z);
            if (horizontalSpeed < 0.8f) {
                breakVelocity_.x = 1.5f;
                breakVelocity_.z = -1.2f;
            } else {
                breakVelocity_.x *= 0.9f;
                breakVelocity_.z *= 0.9f;
            }
            breakAngularVelocity_.x *= 1.1f;
            breakAngularVelocity_.y *= 1.1f;
            breakAngularVelocity_.z *= 1.1f;
            breakState_ = BreakState::Rolling;
            rollingTimer_ = 0.0f;

            breakLandedScale_ = breakStartScale_;
            breakLandedScale_.y = breakStartScaleY_ * 0.35f;
            SetScale(breakLandedScale_);
        }

        SetTranslate(pos);
        SetRotation(rot);
        GetTransform()->isQuaternionMaster = false;
    }
    else if (breakState_ == BreakState::Rolling) {
        rollingTimer_ += deltaTime;

        Vector3 pos = GetTranslate();
        pos.x += breakVelocity_.x * deltaTime;
        pos.z += breakVelocity_.z * deltaTime;
        pos.y = breakGroundY_ + (std::max)(0.1f, GetScale().y * 0.5f);

        Vector3 rot = GetRotation();
        rot.x += breakAngularVelocity_.x * deltaTime;
        rot.y += breakAngularVelocity_.y * deltaTime;
        rot.z += breakAngularVelocity_.z * deltaTime;

        float friction = std::pow(0.38f, deltaTime);
        breakVelocity_.x *= friction;
        breakVelocity_.z *= friction;
        breakAngularVelocity_.x *= friction;
        breakAngularVelocity_.y *= friction;
        breakAngularVelocity_.z *= friction;

        SetTranslate(pos);
        SetRotation(rot);
        GetTransform()->isQuaternionMaster = false;

        if (rollingTimer_ >= 0.9f) {
            breakVelocity_ = { 0.0f, 0.0f, 0.0f };
            breakAngularVelocity_ = { 0.0f, 0.0f, 0.0f };
            breakState_ = BreakState::Landed;
            landedTimer_ = 0.0f;
            SetMaterialType(4);
            SetColor({ breakBaseColor_.x, breakBaseColor_.y, breakBaseColor_.z, 1.0f });
        }
    }
    else if (breakState_ == BreakState::Landed) {
        landedTimer_ += deltaTime;
        float t = std::clamp(landedTimer_ / 1.0f, 0.0f, 1.0f);
        float shrink = 1.0f - (t * 0.9f);
        SetScale({
            breakLandedScale_.x * shrink,
            breakLandedScale_.y * shrink,
            breakLandedScale_.z * shrink
        });
        SetColor({ breakBaseColor_.x, breakBaseColor_.y, breakBaseColor_.z, 1.0f - t });

        if (t >= 1.0f) {
            SetIsVisible(false);
        }
    }
}
