#define NOMINMAX
#include "game/actor/item/DroppedCopyStar.h"
#include "Player.h"
#include "SceneManager.h"
#include "VFXSequencer.h"
#include "game/actor/player/copy/PlayerCopyTypeCatalog.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kFlightDuration = 0.45f;
constexpr float kPickupDelay = 0.70f;
} // namespace

void DroppedCopyStar::InitializeDrop(Object3dCommon* common, const std::string& enemyType, bool unlimitedDuration,
                                     const engine::Vector3& start, const engine::Vector3& destination) {
    // 投下中の移動を再現できるよう、起点と到達点を生成時に固定する。
    enemyType_ = enemyType;
    unlimitedDuration_ = unlimitedDuration;
    start_ = start;
    destination_ = destination;
    elapsed_ = 0.0f;
    BaseItem::Initialize(common, std::string(PlayerCopyTypeCatalog::GetEssenceModel(enemyType)));
    SetName("DroppedCopyStar_" + enemyType);
    SetItemType("DroppedCopyStar");
    SetTranslate(start);
    SetScale({0.92f, 0.92f, 0.92f});
    SetMaterialType(0);
    SetTexture("");
    SetColor({1, 1, 1, 1});
    SetSelectedLighting(2);
    SetEnableLighting(true);
    SetEnableEnvMap(true);
    SetEnvIntensity(0.55f);
    SetMetallic(0.0f);
    SetRoughness(0.46f);
    SetEmissive(0.65f);
    SetCastShadow(true);
    UpdateWorldMatrix();
}

void DroppedCopyStar::Update(float deltaTime) {
    if (isDead || isCollected_)
        return;
    auto* manager = SceneManager::GetInstance();
    if (!manager || !manager->IsPlaying())
        return;
    deltaTime = std::isfinite(deltaTime) ? (std::max)(0.0f, deltaTime) : 0.0f;
    elapsed_ += deltaTime;
    if (elapsed_ >= lifetime_) {
        MarkCollected();
        return;
    }
    const float t = std::clamp(elapsed_ / kFlightDuration, 0.0f, 1.0f);
    engine::Vector3 position = start_ * (1.0f - t) + destination_ * t;
    position.y += std::sin(t * 3.14159265f) * 0.85f;
    if (t >= 1.0f)
        position.y += std::sin((elapsed_ - kFlightDuration) * 2.15f) * 0.10f;
    SetTranslate(position);
    const float remaining = lifetime_ - elapsed_;
    // 点滅中も拾える判定は維持し、消える瞬間にだけ判定も取り除きます。
    const float period = remaining < 1.0f ? 0.14f : 0.30f;
    SetIsVisible(remaining > blinkDuration_ || std::fmod(elapsed_, period) < period * 0.60f);
    BaseItem::Update(deltaTime);
}

void DroppedCopyStar::Collect(Player* player) {
    if (!player || isCollected_ || isDead || elapsed_ < kPickupDelay || elapsed_ >= lifetime_)
        return;
    auto* manager = SceneManager::GetInstance();
    if (!manager || !manager->IsPlaying())
        return;
    if (!player->ApplyStoredCopy(enemyType_, unlimitedDuration_))
        return;
    // 成功した瞬間に消費し、同フレームの重複衝突でも二重取得させません。
    MarkCollected();
    VFXSequencer::PlayOneShot("copy_memory_station_activate_cue", player->GetWorldPosition(), {0.65f, 0.65f, 0.65f});
}

std::unique_ptr<engine::Object3d> DroppedCopyStar::Clone() const {
    // 軌道と経過時間を引き継ぎ、複製直後の位置が巻き戻らないようにする。
    auto copy = std::make_unique<DroppedCopyStar>();
    copy->InitializeDrop(common_, enemyType_, unlimitedDuration_, start_, destination_);
    copy->CopyFrom(this);
    copy->elapsed_ = elapsed_;
    copy->isCollected_ = isCollected_;
    copy->lifetime_ = lifetime_;
    copy->blinkDuration_ = blinkDuration_;
    return copy;
}

void DroppedCopyStar::CaptureReplayCustomState(json& state) const {
    // コピー能力の種類と投下時計を、共通の位置情報に追加して記録する。
    engine::Object3d::CaptureReplayCustomState(state);
    state["copyDropType"] = enemyType_;
    state["copyDropUnlimited"] = unlimitedDuration_;
    state["copyDropElapsed"] = elapsed_;
    state["copyDropCollected"] = isCollected_;
    state["copyDropLifetime"] = lifetime_;
    state["copyDropBlink"] = blinkDuration_;
    state["copyDropStart"] = {start_.x, start_.y, start_.z};
    state["copyDropDestination"] = {destination_.x, destination_.y, destination_.z};
}

void DroppedCopyStar::RestoreReplayCustomState(const json& state) {
    // 旧記録に項目がない場合は現在値を保持して復元する。
    engine::Object3d::RestoreReplayCustomState(state);
    enemyType_ = state.value("copyDropType", enemyType_);
    unlimitedDuration_ = state.value("copyDropUnlimited", unlimitedDuration_);
    elapsed_ = state.value("copyDropElapsed", elapsed_);
    isCollected_ = state.value("copyDropCollected", isCollected_);
    lifetime_ = state.value("copyDropLifetime", lifetime_);
    blinkDuration_ = state.value("copyDropBlink", blinkDuration_);
    auto restoreVector = [&](const char* key, engine::Vector3& value) {
        if (state.contains(key) && state[key].is_array() && state[key].size() == 3) {
            value = {state[key][0].get<float>(), state[key][1].get<float>(), state[key][2].get<float>()};
        }
    };
    restoreVector("copyDropStart", start_);
    restoreVector("copyDropDestination", destination_);
}
