#include "GimmickCoin.h"
#include "CollisionConfig.h"
#include "GameDataManager.h"
#include <GPUParticleManager.h>
#include <DebugConsole.h>

void GimmickCoin::Initialize(Object3dCommon* common, const std::string& modelName) {
    // sphere などをコインモデルとして想定
    BaseGimmick::Initialize(common, modelName);
    
    // トリガーとして機能させるため、属性を設定
    SetCollisionAttribute(CollisionAttribute::kTrigger);
    SetCollisionMask(CollisionAttribute::kPlayer); // プレイヤーのみと衝突する
    
    // 球体コライダーで判定
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(1.0f); // 獲得しやすい適度なサイズ
    
    // コインらしいビジュアル設定（ゴールドイエローに光らせる）
    SetColor({ 1.0f, 0.9f, 0.0f, 1.0f });
    
    // 薄いコインの形を表現するために扁平させる（Z軸のスケールを小さく）
    SetScale({ 0.6f, 0.6f, 0.15f });
    
    isCollected_ = false;
    SetClassName("Gimmick");
    SetGimmickType("Coin");
}

void GimmickCoin::Update(float deltaTime) {
    if (isCollected_) {
        // 獲得されたら判定を切って消滅
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        isDead = true;
        return;
    }
    
    // コインをY軸方向に自動でクルクル回転させる（ゲーム的な演出！）
    Vector3 rotate = GetRotation();
    rotate.y += rotationSpeed_ * deltaTime;
    if (rotate.y > 6.28f) rotate.y -= 6.28f;
    SetRotation(rotate);
    
    BaseGimmick::Update(deltaTime);
}

bool GimmickCoin::OnCollision(Object3d* other) {
    if (isCollected_) return false;

    // 衝突してきた相手がプレイヤーの場合のみ獲得
    if (other->GetCollisionAttribute() & CollisionAttribute::kPlayer) {
        Collect();
        return true;
    }

    return false;
}

void GimmickCoin::Collect() {
    isCollected_ = true;

    // コイン所持数を増やす（100枚で1UP）
    GameDataManager::GetInstance()->AddCoin(1);

    // 獲得ログ出力
    int currentCoins = GameDataManager::GetInstance()->GetCoins();
    int currentLives = GameDataManager::GetInstance()->GetLives();
    DebugConsole::GetInstance()->AddLog(
        "Coin Collected! (" + std::to_string(currentCoins) + "/100) Lives: " + std::to_string(currentLives)
    );

    // キラキラするGPUパーティクルの発生（プレミアム演出！）
    if (GPUParticleManager::GetInstance()) {
        GPUParticleManager::GetInstance()->Emit("star_sparkleGet", GetTranslate());
    }
}

std::unique_ptr<Object3d> GimmickCoin::Clone() const {
    auto newObj = std::make_unique<GimmickCoin>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
