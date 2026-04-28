#include "GimmickChikuwaBlock.h"
#include "CollisionConfig.h"
#include "Math.h"
#include <random>

void GimmickChikuwaBlock::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("ChikuwaBlock");
    
    startPos_ = GetTransform()->translate;
    
    if (!param_.has_value()) param_.emplace();

    // 当たり判定設定 (地面属性)
    SetCollisionAttribute(kGround);
}

void GimmickChikuwaBlock::Update(float deltaTime) {
    // 状態更新
    switch (state_) {
    case State::Idle:
        // 待機中は常に開始位置を最新に更新（エディタでの移動に対応するため）
        startPos_ = GetTransform()->translate;
        break;

    case State::Shaking:
        timer_ += deltaTime;
        // ランダムな微振動を加える
        {
            float intensity = 0.05f;
            float offX = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * intensity;
            float offZ = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * intensity;
            GetTransform()->translate.x = startPos_.x + offX;
            GetTransform()->translate.z = startPos_.z + offZ;
        }

        if (timer_ >= GetShakeDuration()) {
            ChangeState(State::Falling);
        }
        break;

    case State::Falling:
        timer_ += deltaTime;
        velocityY_ -= GetGravity() * deltaTime;
        GetTransform()->translate.y += velocityY_ * deltaTime;

        // 落下中は少し回転させると「落ちてる感」が出る
        GetTransform()->rotate.x += 2.0f * deltaTime;
        GetTransform()->rotate.z += 1.0f * deltaTime;

        if (timer_ >= GetFallDuration()) {
            ChangeState(State::Hidden);
        }
        break;

    case State::Hidden:
        timer_ += deltaTime;
        if (timer_ >= GetRespawnDuration()) {
            ChangeState(State::Idle);
        }
        break;
    }

    // 基底クラスの更新（行列計算など）
    BaseGimmick::Update(deltaTime);
}

bool GimmickChikuwaBlock::OnCollision(Object3d* other) {
    // 待機中のみプレイヤーの搭乗を判定
    if (state_ == State::Idle) {
        if (other->GetClassName() == "Player") {
            CollisionInfo info = CheckCollision(other);
            if (info.isColliding) {
                // プレイヤーが上に乗ったら（ブロックが押し出される法線が下向きなら）震え開始
                if (info.normal.y < -0.5f) {
                    ChangeState(State::Shaking);
                }
            }
        }
    }
    return true;
}

void GimmickChikuwaBlock::ChangeState(State newState) {
    state_ = newState;
    timer_ = 0.0f;

    switch (state_) {
    case State::Idle:
        SetIsVisible(true);
        SetCollisionAttribute(kGround); // 衝突判定を元に戻す
        GetTransform()->translate = startPos_;
        GetTransform()->rotate = { 0, 0, 0 };
        velocityY_ = 0.0f;
        break;

    case State::Shaking:
        // 震え開始
        break;

    case State::Falling:
        // 落下開始時に地面属性を消す（上に乗れなくする）
        SetCollisionAttribute(0);
        break;

    case State::Hidden:
        SetIsVisible(false);
        SetCollisionAttribute(0);
        break;
    }
}

std::unique_ptr<Object3d> GimmickChikuwaBlock::Clone() const {
    auto newObj = std::make_unique<GimmickChikuwaBlock>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
