#include "GimmickChikuwaBlock.h"
#include "ObjectPropertyDefinition.h"
#include "CollisionConfig.h"
#include "Math.h"
#include <random>

void GimmickChikuwaBlock::Initialize(engine::Object3DCommon* common, const std::string& modelName) {
    // 配置位置を復帰先として保持し、待機中にプレイヤーを支える地面として登録します。
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("ChikuwaBlock");

    startPos_ = GetTransform()->translate;

    if (!GetParameters().has_value())
        ResetParameters();

    // 当たり判定設定 (地面属性)
    SetCollisionAttribute(CollisionCategories::Get("ground"));

    // 編集項目の宣言です。動作処理と旧paramの保存キーは変更しません。
    using Self = GimmickChikuwaBlock;
    engine::ObjectPropertyDefinition<Self> properties(*this, kTypeId, "ちくわブロック",
        "");
    using Parameter = engine::Object3D::EntityParameter;
    properties.Parameter("震え時間", "shakeDuration", &Parameter::shakeDuration, 0.0f, 10.0f);
    properties.Parameter("落下時間", "fallDuration", &Parameter::fallDuration, 0.0f, 10.0f);
    properties.Parameter("リスポーン間隔", "interval", &Parameter::interval, 0.0f, 10.0f);
    properties.Parameter("落下重力", "gravity", &Parameter::gravity, 0.0f, 200.0f);
}

void GimmickChikuwaBlock::Update(float deltaTime) {
    // 状態更新
    switch (state_) {
    // まだ踏まれていない間だけ、編集中に移動した配置を復帰位置へ反映します。
    case State::Idle:
        startPos_ = GetTransform()->translate;
        break;

    // 足場のまま横方向だけ震わせ、予告時間が尽きたら落下へ移します。
    case State::Shaking:
        timer_ += deltaTime;

        // ランダムな微振動を加える
        {
            float intensity = 0.05f;
            float offX = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * intensity;
            float offZ = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * intensity;
            SetTranslateX(startPos_.x + offX);
            SetTranslateZ(startPos_.z + offZ);
        }

        if (timer_ >= GetShakeDuration()) {
            ChangeState(State::Falling);
        }

        break;

    // 毎フレーム鉛直速度へ重力を積分し、落下時間が尽きたら表示を消す段階へ進みます。
    case State::Falling:
        timer_ += deltaTime;
        velocityY_ -= GetGravity() * deltaTime;
        SetTranslateY(GetTranslate().y + velocityY_ * deltaTime);

        // 落下中は少し回転させると「落ちてる感」が出る
        SetEulerX(GetRotation().x + 2.0f * deltaTime);
        SetEulerZ(GetRotation().z + 1.0f * deltaTime);

        if (timer_ >= GetFallDuration()) {
            ChangeState(State::Hidden);
        }

        break;

    // 非表示のまま復帰時間を待ち、開始地点に戻す処理を ChangeState() に任せます。
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

bool GimmickChikuwaBlock::OnCollision(engine::Object3D* other) {
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
    // 各段階の時計は遷移時にリセットし、衝突や表示をその場で切り替えます。
    state_ = newState;
    timer_ = 0.0f;

    switch (state_) {
    // 復帰時は衝突と表示を戻し、次の落下に前回の回転や速度を持ち越しません。
    case State::Idle:
        SetIsVisible(true);
        SetCollisionAttribute(CollisionCategories::Get("ground")); // 衝突判定を元に戻す
        SetTranslate(startPos_);
        SetEulerAngles({0, 0, 0});
        velocityY_ = 0.0f;
        break;

    case State::Shaking:
        // 予告中は表示と地面属性を維持し、足場から離れる猶予を残します。
        break;

    case State::Falling:
        // 落下開始時に地面属性を消す（上に乗れなくする）
        SetCollisionAttribute(0);
        break;

    // 落下が終わった実体は透明化ではなく非表示にし、復帰まで接触を無効にします。
    case State::Hidden:
        SetIsVisible(false);
        SetCollisionAttribute(0);
        break;
    }
}

std::unique_ptr<engine::Object3D> GimmickChikuwaBlock::Clone() const {
    // 同じモデルで新しい実体を初期化してから、配置と設定値を複製する。
    auto newObj = std::make_unique<GimmickChikuwaBlock>();
    assert(GetObjectCommon() != nullptr);
    newObj->Initialize(GetObjectCommon(), GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
