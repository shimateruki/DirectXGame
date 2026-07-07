#include "GimmickBlinkBlock.h"
#include "CollisionConfig.h"
#include "Player.h"

void GimmickBlinkBlock::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("BlinkBlock");

    // イベント購読
    EventManager::GetInstance()->Subscribe([this](const PlayerJumpEvent& e) { this->OnPlayerJump(e); });

    // 初期パラメータの読み込み
    if (param_.has_value()) {
        colorType_ = param_->colorType;
    }

    // 初期状態の反映（ジャンプ0回目を基準にする）
    isActive_ = (0 % 2 == colorType_);
    UpdateAppearance();
}

void GimmickBlinkBlock::Update(float deltaTime) {
    // エディタでの色変更を即座に反映
    if (param_.has_value() && param_->colorType != colorType_) {
        colorType_ = param_->colorType;
        UpdateAppearance();
    }

    BaseGimmick::Update(deltaTime);
}

void GimmickBlinkBlock::OnPlayerJump(const PlayerJumpEvent& event) {
    // プレイヤーのジャンプ回数(0, 1, 2...)に合わせて状態を切り替える
    // 0回目: 青ON/赤OFF, 1回目: 青OFF/赤ON ... という同期をとる
    if (event.player) {
        uint32_t count = event.player->GetJumpCount();
        bool shouldBeActive = (count % 2 == colorType_);
        
        if (isActive_ != shouldBeActive) {
            isActive_ = shouldBeActive;
            UpdateAppearance();
        }
    }
}

void GimmickBlinkBlock::UpdateAppearance() {
    // isActive_ に基づいて見た目と衝突判定を切り替える
    if (isActive_) {
        SetIsVisible(true);
        SetCollisionAttribute(kGround);
        // 不透明
        if (colorType_ == 0) {
            // 青
            SetColor({ 0.2f, 0.6f, 1.0f, 1.0f });
        } else {
            // 赤
            SetColor({ 1.0f, 0.3f, 0.3f, 1.0f });
        }
    } else {
        // 非アクティブ時は衝突判定を消し、半透明にする
        SetCollisionAttribute(0);
        if (colorType_ == 0) {
            // 青（半透明）
            SetColor({ 0.2f, 0.6f, 1.0f, 0.2f });
        } else {
            // 赤（半透明）
            SetColor({ 1.0f, 0.3f, 0.3f, 0.2f });
        }
    }
}

std::unique_ptr<Object3d> GimmickBlinkBlock::Clone() const {
    auto newObj = std::make_unique<GimmickBlinkBlock>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
