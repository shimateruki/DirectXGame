#include "GimmickLiquidLevel.h"
#include "CollisionConfig.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Player.h"
#include "CameraManager.h"
#include "game/actor/gimmick/movement/GimmickMovingFloor.h"
#include "game/actor/gimmick/combat/GimmickMagmaGeyser.h"
#include "game/actor/gimmick/combat/GimmickFireCannon.h"
#include "game/actor/gimmick/trigger/GimmickSwitch.h"
#include <algorithm>
#include <cmath>

void GimmickLiquidLevel::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // 液面は足場の衝突には使わず、表示と高さの制御を担当します。
    SetClassName("Gimmick");
    SetGimmickType("LiquidLevel");
    SetName("Gimmick_LiquidLevel");
    SetCollisionAttribute(0);
    SetCollisionMask(0);
    SetScale({4.0f, 0.08f, 4.0f});
    SetStatic(false);

    if (!param_.has_value())
        param_.emplace();
    param_->moveAmount = 6.0f;
    param_->moveSpeed = 3.0f;
    param_->startActive = false;
    param_->returnOnOff = true;

    ApplyLiquidVisual();
}

void GimmickLiquidLevel::Update(float deltaTime) {
    // 脱出シーケンスだけは通常のスイッチ昇降と別の時間軸で進めます。
    if (!param_.has_value())
        param_.emplace();

    if (param_->actionMode == 1) {
        UpdateEscape(deltaTime);
        return;
    }
    bool isPlaying = false;
    if (SceneManager* sceneManager = SceneManager::GetInstance()) {
        isPlaying = sceneManager->IsPlaying();
    }

    // エディター中の位置を基準点として誤記録しないよう、再生開始時に初期化します。
    if (!isPlaying) {
        initializedForPlay_ = false;
        ApplyLiquidVisual();
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedForPlay_) {
        basePosition_ = GetTransform()->translate;
        targetPosition_ = basePosition_;
        active_ = param_->startActive;
        ApplyTarget(active_);
        initializedForPlay_ = true;
    }

    // 指数補間を使い、フレーム時間が変わっても目標位置への追従を安定させます。
    float speed = (std::max)(0.01f, param_->moveSpeed);
    float t = 1.0f - std::exp(-speed * deltaTime);
    engine::Vector3 pos = GetTransform()->translate;
    pos.x = engine::Math::Lerp(pos.x, targetPosition_.x, t);
    pos.y = engine::Math::Lerp(pos.y, targetPosition_.y, t);
    pos.z = engine::Math::Lerp(pos.z, targetPosition_.z, t);
    SetTranslate(pos);

    ApplyLiquidVisual();
    BaseGimmick::Update(deltaTime);
}

void GimmickLiquidLevel::OnTrigger() {
    // 脱出用液面はエリア進行で動くため、通常のトリガー入力を無視します。
    if (param_ && param_->actionMode == 1)
        return;
    OnSwitchEvent(true);
}

void GimmickLiquidLevel::OnSwitchEvent(bool active) {
    // オフで戻さない設定なら、解除イベント後も上昇位置を維持します。
    if (!param_.has_value())
        param_.emplace();
    if (param_->actionMode == 1)
        return;
    if (!active && !param_->returnOnOff)
        return;
    ApplyTarget(active);
}

void GimmickLiquidLevel::ApplyTarget(bool active) {
    // 配置時の高さを基準に、設定した距離だけ上下させます。
    active_ = active;
    float amount = param_.has_value() ? param_->moveAmount : 6.0f;
    targetPosition_ = active ? basePosition_ + engine::Vector3{0.0f, amount, 0.0f} : basePosition_;
}

void GimmickLiquidLevel::ApplyLiquidVisual() {
    // 色種別に合わせて水とマグマの材質・透過色を切り替えます。
    int colorType = param_.has_value() ? param_->colorType : 0;
    if (colorType == 1) {
        SetMaterialType(9);
        SetColor({1.0f, 0.25f, 0.02f, 0.88f});
    } else {
        SetMaterialType(8);
        SetColor({0.45f, 0.85f, 1.0f, 0.65f});
    }
}

std::unique_ptr<engine::Object3d> GimmickLiquidLevel::Clone() const {
    // 複製先の描画資源を作った後で、液面の設定を引き継ぎます。
    auto newObj = std::make_unique<GimmickLiquidLevel>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

// 脱出モードは、Target IDから順に開始・終了・復帰マーカーを参照します。
// マーカーは中心とScaleを半径として使い、Editorから範囲を調整できます。
void GimmickLiquidLevel::UpdateEscape(float deltaTime) {
    auto* manager = SceneManager::GetInstance();
    if (!manager || !manager->IsPlaying()) {
        if (initializedForPlay_)
            SetTranslate(basePosition_);
        initializedForPlay_ = false;
        escapeStarted_ = escapeFinished_ = false;
        escapeTime_ = 0.0f;
        BaseGimmick::Update(deltaTime);
        return;
    }
    auto* scene = manager->GetCurrentScene();
    if (!scene)
        return;
    if (!initializedForPlay_) {
        basePosition_ = GetTranslate();
        initializedForPlay_ = true;
        SetStatic(false);
    }
    Player* player = nullptr;
    for (auto& obj : scene->GetObjects()) {
        if ((player = dynamic_cast<Player*>(obj.get())))
            break;
    }
    auto* start = scene->FindObjectByEventID(GetTargetID());
    auto* finish = scene->FindObjectByEventID(GetTargetID() + 1);
    auto* checkpoint = scene->FindObjectByEventID(GetTargetID() + 2);
    const auto inside = [&](engine::Object3d* marker) {
        if (!marker || !player)
            return false;
        const engine::Vector3 p = player->GetTranslate() - marker->GetTranslate();
        const engine::Vector3 radius = marker->GetScale();
        return std::abs(p.x) <= radius.x && std::abs(p.y) <= radius.y && std::abs(p.z) <= radius.z;
    };
    if (player && !player->isDead && player->IsControlActive()) {
        // 開いた入口へ引き返しても上昇は継続。死亡後のScene再生成で初期化します。
        if (!escapeStarted_ && inside(start)) {
            escapeStarted_ = true;
            escapeTime_ = 0.0f;
            if (checkpoint)
                player->ActivateCheckpoint(checkpoint->GetTranslate());
            ResetEscapeObjects(true);
            if (auto* camera = CameraManager::GetInstance()->GetActiveCamera())
                camera->StartShake(0.65f, 0.10f, 18.0f, {0.4f, 1.0f, 0.4f});
        }
        if (escapeStarted_ && !escapeFinished_ && inside(finish)) {
            escapeFinished_ = true;
            player->ActivateCheckpoint(finish->GetTranslate());
            // 到達後は追撃を止め、頂上のゴールへ安全に進めるようにします。
            for (auto& obj : scene->GetObjects())
                if (obj->GetTargetID() == GetEventID()) {
                    if (auto* geyser = dynamic_cast<GimmickMagmaGeyser*>(obj.get()))
                        geyser->ResetEscapeCycle(false);
                    else if (auto* cannon = dynamic_cast<GimmickFireCannon*>(obj.get()))
                        cannon->OnSwitchEvent(false);
                }
        }
    }
    if (escapeStarted_ && !escapeFinished_) {
        escapeTime_ += (std::max)(0.0f, deltaTime);
        const float elapsed = (std::max)(0.0f, escapeTime_ - param_->shakeDuration);
        const float rise =
            (std::min)((std::max)(0.0f, param_->moveAmount), elapsed * (std::max)(0.01f, param_->moveSpeed));
        SetTranslate(basePosition_ + engine::Vector3{0.0f, rise, 0.0f});
    }
    // 接触判定はPlayerが現在の液面を読むため、ここで行列まで確定します。
    SetMaterialType(9);
    BaseGimmick::Update(deltaTime);
}

void GimmickLiquidLevel::ResetEscapeObjects(bool active) {
    auto* scene = SceneManager::GetInstance()->GetCurrentScene();
    if (!scene)
        return;
    for (auto& obj : scene->GetObjects()) {
        if (!obj || obj.get() == this || obj->GetTargetID() != GetEventID())
            continue;
        if (auto* floor = dynamic_cast<GimmickMovingFloor*>(obj.get()))
            floor->ResetEscapeCycle();
        else if (auto* geyser = dynamic_cast<GimmickMagmaGeyser*>(obj.get()))
            geyser->ResetEscapeCycle(active);
        else if (auto* cannon = dynamic_cast<GimmickFireCannon*>(obj.get()))
            cannon->OnSwitchEvent(active);
        else if (obj->GetGimmickType() == "EventReceiver")
            obj->OnSwitchEvent(false);
    }
    // 出現床のスイッチは独立したリンクなので、受信側IDから同じ組を探します。
    for (auto& obj : scene->GetObjects())
        if (obj->GetTargetID() == GetEventID() + 4)
            if (auto* button = dynamic_cast<GimmickSwitch*>(obj.get()))
                button->ResetEscapeCycle();
    scene->SetEventActive(GetEventID() + 5, active);
}
