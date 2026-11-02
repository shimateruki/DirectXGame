#include "GimmickTrampoline.h"
#include "ObjectPropertyDefinition.h"
#include "Player.h"
#include "CollisionConfig.h"
#include "game/actor/player/state/PlayerState.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kBounceCooldown = 0.28f;
constexpr float kSquashStiffness = 110.0f;
constexpr float kSquashDamping = 16.0f;
}

void GimmickTrampoline::Initialize(engine::Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("Trampoline");
    if (!GetParameters())
        ResetParameters();
    SetParameter(&engine::EntityParameter::jumpPower, 30.0f);

    // 傘全体を床と側面に使い、モデルの曲面から着地高さを求める。
    // 柄と根元はシーン側の子コライダーに分け、反発対象にはしない。
    ColliderConfig config = GetColliderConfig();
    config.type = ColliderType::kOBB;
    config.center = {0.0f, 1.20f, 0.0f};
    config.size = {1.40f, 0.40f, 1.40f};
    config.useModelSurface = true;
    SetColliderConfig(config);
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::Get("player"));

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickTrampoline;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "トランポリン",
        "");
    using Parameter = engine::Object3d::EntityParameter;
    properties.Parameter("ジャンプ力", "jumpPower", &Parameter::jumpPower, 0.0f, 100.0f,
        nullptr, {}, {.speed = 1.0f});
}

void GimmickTrampoline::Update(float deltaTime) {
    // 保存済みの配置スケールを基準に、着地直後につぶれ、ばねで戻る。
    if (!authoredTransformCaptured_) {
        authoredScale_ = GetScale();
        authoredRotation_ = GetRotation();
        authoredTransformCaptured_ = true;

        // 見た目のつぶれで着地面まで縮まないよう、衝突形状は配置時の大きさに固定する。
        if (GetCollider())
            GetCollider()->SetScaleOverride(authoredScale_);
    }

    const float dt = (std::clamp)(deltaTime, 0.0f, 0.05f);
    bounceCooldown_ = (std::max)(0.0f, bounceCooldown_ - dt);
    wobbleTime_ = (std::max)(0.0f, wobbleTime_ - dt);
    squashVelocity_ += (-kSquashStiffness * squash_ - kSquashDamping * squashVelocity_) * dt;
    squash_ += squashVelocity_ * dt;

    if (std::abs(squash_) < 0.001f && std::abs(squashVelocity_) < 0.01f) {
        squash_ = 0.0f;
        squashVelocity_ = 0.0f;
    }

    const float spread = 1.0f - squash_ * 0.28f;
    SetScale({authoredScale_.x * spread, authoredScale_.y * (1.0f + squash_), authoredScale_.z * spread});
    const float wobble = std::sin((0.42f - wobbleTime_) * 38.0f) * wobbleTime_ * 0.13f;
    SetEulerAngles({authoredRotation_.x, authoredRotation_.y, authoredRotation_.z + wobble});
    BaseGimmick::Update(deltaTime);
}

bool GimmickTrampoline::OnCollision(engine::Object3d* other) {
    Player* player = dynamic_cast<Player*>(other);
    if (!player)
        return false;
    const CollisionInfo info = CheckCollision(other);

    // Collider の法線は「相手から自分へ」向く。キノコ側の下向きをプレイヤー側へ反転する。
    if (!info.isColliding)
        return false;
    return TryBounce(player, info.normal * -1.0f);
}

bool GimmickTrampoline::TryBounce(Player* player, const engine::Vector3& playerGroundNormal) {
    if (!player || bounceCooldown_ > 0.0f || playerGroundNormal.y <= 0.5f ||
        player->GetVelocity().y > 0.0f) return false;

    // 傘の下から側面へ押し込まれた接触は、法線が上寄りでも着地と見なさない。
    const Collider* capCollider = GetCollider();
    if (!capCollider)
        return false;
    const AABB playerBounds = player->GetAABB();
    const engine::Vector3 footCenter = {
        (playerBounds.min.x + playerBounds.max.x) * 0.5f,
        playerBounds.min.y,
        (playerBounds.min.z + playerBounds.max.z) * 0.5f
    };
    float capHeight = 0.0f;
    engine::Vector3 capNormal{};
    // 互換モーターの１フレーム分の沈み込みは許し、傘の下から来た接触は除く。
    const float landingTolerance = (std::max)(0.15f, std::abs(authoredScale_.y) * 0.20f);
    if (!capCollider->SampleModelSurface(footCenter, capHeight, capNormal) ||
        footCenter.y < capHeight - landingTolerance || capNormal.y <= 0.5f)
        return false;

    // 一回の着地に一回だけ上向き速度を与える。見た目の縮みと同時に開始する。
    engine::Vector3 velocity = player->GetVelocity();
    velocity.y = GetParameters() ? GetParameters()->jumpPower : 30.0f;
    player->SetVelocity(velocity);
    player->SetGrounded(false);
    player->ChangeState(std::make_unique<PlayerStateJump>());
    bounceCooldown_ = kBounceCooldown;
    squash_ = -0.28f;
    squashVelocity_ = -1.4f;
    wobbleTime_ = 0.42f;
    return true;
}

std::unique_ptr<engine::Object3d> GimmickTrampoline::Clone() const {
    // 複製後も専用の衝突・反発動作を保つ。
    auto clone = std::make_unique<GimmickTrampoline>();
    clone->Initialize(GetObjectCommon(), GetModelName());
    clone->CopyFrom(this);
    return clone;
}
