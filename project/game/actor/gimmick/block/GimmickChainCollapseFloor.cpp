#include "GimmickChainCollapseFloor.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include <cstdlib>

namespace {
// 落下復帰後にプレイヤーと床が重ならないよう、十分な退避時間を確保する。
constexpr float kReturnDelaySeconds = 5.0f;
} // namespace

void GimmickChainCollapseFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // 踏まれる前は通常の床として衝突し、落下開始時に判定を外します。
    SetClassName("Gimmick");
    SetGimmickType("ChainCollapseFloor");
    SetName("Gimmick_ChainCollapseFloor");
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
    SetColor({0.75f, 0.92f, 1.0f, 0.82f});
    SetScale({2.0f, 0.25f, 2.0f});
    SetStatic(false);

    if (!param_.has_value())
        param_.emplace();
    param_->shakeDuration = 0.45f;
    param_->fallDuration = 1.4f;
    param_->interval = 0.18f;
    param_->gravity = 48.0f;
    // 従来の一度きりの挙動を既定とし、必要なステージだけ明示的に復帰を有効化する。
    param_->returnOnOff = false;
}

void GimmickChainCollapseFloor::Update(float deltaTime) {
    // 待機・予告・揺れ・落下・消失を状態ごとに進め、再出現もここで管理します。
    if (!initializedStart_) {
        startPosition_ = GetTransform()->translate;
        startRotation_ = GetTransform()->rotate;
        initializedStart_ = true;
    }

    switch (state_) {
    case State::Idle:
        startPosition_ = GetTransform()->translate;
        startRotation_ = GetTransform()->rotate;
        break;

    case State::Pending:
        pendingTimer_ -= deltaTime;
        if (pendingTimer_ <= 0.0f) {
            pendingTimer_ = 0.0f;
            StartCollapse();
        }
        break;

    case State::Shaking: {
        timer_ += deltaTime;

        float intensity = 0.045f;
        float offX = ((float)std::rand() / RAND_MAX * 2.0f - 1.0f) * intensity;
        float offZ = ((float)std::rand() / RAND_MAX * 2.0f - 1.0f) * intensity;
        SetTranslateX(startPosition_.x + offX);
        SetTranslateZ(startPosition_.z + offZ);

        if (timer_ >= GetShakeDuration()) {
            ChangeState(State::Falling);
        }
        break;
    }

    case State::Falling:
        timer_ += deltaTime;
        velocityY_ -= GetGravity() * deltaTime;
        SetTranslateY(GetTranslate().y + velocityY_ * deltaTime);
        SetEulerX(GetRotation().x + 1.4f * deltaTime);
        SetEulerZ(GetRotation().z + 0.9f * deltaTime);
        SetQuaternionMaster(false);

        if (timer_ >= GetFallDuration()) {
            ChangeState(State::Hidden);
        }
        break;

    case State::Hidden:
        if (ShouldReturnAfterCollapse()) {
            returnTimer_ += deltaTime;
            if (returnTimer_ >= kReturnDelaySeconds) {
                ResetForRetry();
            }
        }
        break;
    }

    BaseGimmick::Update(deltaTime);
}

bool GimmickChainCollapseFloor::OnCollision(Object3d* other) {
    // 横から触れただけでは崩れず、プレイヤーが上面に乗った場合に始動します。
    if (state_ == State::Idle && other && other->GetClassName() == "Player") {
        CollisionInfo info = CheckCollision(other);
        if (info.isColliding && info.normal.y < -0.5f) {
            StartCollapse();
        }
    }
    return true;
}

void GimmickChainCollapseFloor::OnTrigger() {
    // 前の床からの連鎖イベントは、設定した遅延を経て崩壊させます。
    QueueCollapse();
}

void GimmickChainCollapseFloor::StartCollapse() {
    // 予告中や待機中以外の重複始動を防ぎます。
    if (state_ != State::Idle && state_ != State::Pending)
        return;
    ChangeState(State::Shaking);
}

void GimmickChainCollapseFloor::QueueCollapse() {
    // 前の床だけ先に復帰して踏まれた場合も、後続床へのイベントを失わない。
    if (state_ == State::Hidden && ShouldReturnAfterCollapse()) {
        ResetForRetry();
    }
    if (state_ != State::Idle)
        return;
    pendingTimer_ = GetChainDelay();
    ChangeState(State::Pending);
}

void GimmickChainCollapseFloor::ResetForRetry() {
    // 復帰時は位置・判定・連鎖済みフラグを待機状態へ戻します。
    ChangeState(State::Idle);
}

void GimmickChainCollapseFloor::ChangeState(State state) {
    // 状態遷移時の表示と衝突を同時に切り替え、見えない床に乗れないようにします。
    state_ = state;
    timer_ = 0.0f;

    switch (state_) {
    case State::Idle:
        SetIsVisible(true);
        SetCollisionAttribute(CollisionCategories::Get("ground"));
        SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
        SetTranslate(startPosition_);
        SetEulerAngles(startRotation_);
        SetQuaternionMaster(false);
        velocityY_ = 0.0f;
        pendingTimer_ = 0.0f;
        returnTimer_ = 0.0f;
        triggeredNext_ = false;
        SetColor({0.75f, 0.92f, 1.0f, 0.82f});
        break;

    case State::Pending:
        SetColor({0.95f, 0.85f, 0.35f, 0.9f});
        break;

    case State::Shaking:
        SetColor({1.0f, 0.95f, 0.45f, 0.9f});
        triggeredNext_ = false;
        break;

    case State::Falling:
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        velocityY_ = 0.0f;
        TriggerNextFloor();
        break;

    case State::Hidden:
        SetIsVisible(false);
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        returnTimer_ = 0.0f;
        break;
    }
}

bool GimmickChainCollapseFloor::ShouldReturnAfterCollapse() const {
    // 復帰は配置設定で明示された床にだけ許可します。
    return param_.has_value() && param_->returnOnOff;
}

void GimmickChainCollapseFloor::TriggerNextFloor() {
    // 落下状態へ入った時だけ次の床へイベントを一度伝えます。
    if (triggeredNext_)
        return;

    triggeredNext_ = true;
    int targetID = GetTargetID();
    if (targetID == -1)
        return;

    if (BaseScene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
        scene->TriggerEvent(targetID);
    }
}

float GimmickChainCollapseFloor::GetShakeDuration() const {
    // 旧データに設定がない場合は既定の予告時間を使います。
    return param_.has_value() ? param_->shakeDuration : 0.45f;
}

float GimmickChainCollapseFloor::GetFallDuration() const {
    // 落下演出の長さは配置設定から取得します。
    return param_.has_value() ? param_->fallDuration : 1.4f;
}

float GimmickChainCollapseFloor::GetChainDelay() const {
    // 連鎖する床同士の崩壊開始を少しずつ遅らせます。
    return param_.has_value() ? param_->interval : 0.18f;
}

float GimmickChainCollapseFloor::GetGravity() const {
    // 落下速度の増加率は配置設定に従います。
    return param_.has_value() ? param_->gravity : 48.0f;
}

std::unique_ptr<Object3d> GimmickChainCollapseFloor::Clone() const {
    // 複製先を初期化し、連鎖の設定と元の配置を引き継ぎます。
    auto newObj = std::make_unique<GimmickChainCollapseFloor>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
