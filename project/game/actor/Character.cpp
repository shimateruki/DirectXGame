#define NOMINMAX
#include "Character.h"
#include "CollisionConfig.h"
#include "Math.h"
#include <algorithm> // std::min, std::max
#include <cmath>     // std::abs
#include "GhostRecorder.h"

//  Math のインスタンスを作成
static Math math;




void Character::Update(float deltaTime) {

    if (!this->param_.has_value()) {
        return;
    }

    float gravity = this->param_->gravity;
    float maxFallSpeed = this->param_->maxFallSpeed;


    isGrounded_ = false;
    velocity_.y -= gravity * deltaTime;

    if (velocity_.y < -maxFallSpeed) {
        velocity_.y = -maxFallSpeed;
    }
    if (this->param_->hp <=0)
    {
        isDead = true;
    }

    transform_.translate += velocity_ * deltaTime;
}

bool Character::OnCollision(Object3d* other) {
    // ★ 1. 衝突情報を取得
    CollisionInfo info = CheckCollision(other);
    if (!info.isColliding) {
        return false;
    }

    // 新しい関数に処理を委譲
    ApplyPhysicsCollision(info, other->GetCollisionAttribute());

    return info.isColliding;
}


void Character::ApplyPhysicsCollision(const CollisionInfo& info, uint32_t attribute) {
    // 1. 地面・壁属性以外（敵の攻撃判定など）は物理押し出ししない
    // ※ kAllSolid は「kGround | kWall」などのビットマスクと想定
    if (!(attribute & kAllSolid)) {
        return;
    }

    // ★重要: 法線ベクトルの向きの確認
    // この関数では info.normal を「プレイヤーを押し出す方向（壁からプレイヤーへの矢印）」として扱います。
    // もし当たり判定の計算で「プレイヤーから壁への矢印」が入っている場合は、
    // ここで -1.0f を掛けて反転させる必要があります。
    // (現状のコードが += で動いているなら、そのままでOKなはずです)
    Vector3 pushNormal = info.normal;


    // 2. 座標を押し戻す (めり込み解消)
    // 坂道の場合、斜め上に押し出されることで登れるようになります。
    this->transform_.translate += (pushNormal * info.penetration);


    // 3. 速度の補正 (壁ずり・床滑り防止)
    // 壁や床に向かって進んでいる場合のみ、その方向の速度成分を消します。
    float dot = math.Dot(velocity_, pushNormal);

    // dot < 0.0f ＝ 壁に向かって進んでいる（めり込もうとしている）
    if (dot < 0.0f) {
        // 壁・坂の表面に沿って滑るベクトル（Slide Vector）を作る
        // Velocity = Velocity - (Normal * dot)
        velocity_ = velocity_ - (pushNormal * dot);
    }


    // 4. 接地判定 (坂道対応)
    // 法線のY成分が上を向いていれば「地面」または「坂」とみなします。
    // 1.0f = 平地(0度), 0.7f = 約45度, 0.0f = 垂直な壁
    const float kSlopeThreshold = 0.7f;

    if (pushNormal.y > kSlopeThreshold) {
        isGrounded_ = true;

        // 【最重要】坂道での重力リセット
        // これがないと、坂を登っても重力で下に引っ張られ、ずり落ちてしまいます。
        if (velocity_.y < 0.0f) {
            velocity_.y = 0.0f;
        }
    }
}


std::unique_ptr<Object3d> Character::Clone() const {
    // Character として生成
    auto newObj = std::make_unique<Character>();

    assert(common_ != nullptr);
    newObj->Initialize(common_);

    // モデル設定
    if (!GetModelName().empty()) {
        newObj->SetModel(this->GetModelName());
    }
    // Transform 情報
    // (Transform構造体はコピー可能なのでそのまま代入OK)
    newObj->transform_ = this->transform_;

    // 名前 
    newObj->SetName(this->name_);
    newObj->SetColliderConfig(this->GetColliderConfig());
    newObj->SetCollisionAttribute(this->GetCollisionAttribute());
    newObj->SetCollisionMask(this->GetCollisionMask());

    // 1. イベントIDとステータス(param_)をコピー
    newObj->SetEventType(this->eventType_);
    if (this->param_.has_value()) {
        newObj->param_ = this->param_.value();
    }

    // 2. Character 独自のメンバをコピー
    newObj->velocity_ = this->velocity_;
    newObj->isGrounded_ = this->isGrounded_;

    // 3. アニメーション設定のコピーと再生
    newObj->animName_ = this->animName_;
    newObj->isAnimLoop_ = this->isAnimLoop_;
    newObj->isAnimRelative_ = this->isAnimRelative_;

    newObj->InitializeRecorder(nullptr); // レコーダー初期化
    if (!newObj->animName_.empty()) {
        bool isCinematic = (newObj->GetClassName() == "CinematicCamera");
        newObj->recorder_->Play(
            newObj->animName_,
            newObj->isAnimLoop_,
            newObj->isAnimRelative_,
            isCinematic
        );
    }

    // "Player" や "Model" などのクラス名を引き継ぐ
    newObj->SetClassName(this->GetClassName());

    // "Slime" などの敵タイプ名を引き継ぐ
    newObj->SetEnemyType(this->GetEnemyType());

    // ---------------------------------------------------

    return newObj;
}
void Character::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // 親の描画処理をそのまま実行する
    if (!isDead)
    {
        Object3d::Draw(pointLightResource, spotLightResource);
    }
   
}