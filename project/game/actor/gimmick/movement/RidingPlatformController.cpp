#define NOMINMAX
#include "RidingPlatformController.h"
#include "GimmickMovingFloor.h"
#include "BaseEnemy.h"
#include "game/actor/gimmick/combat/GimmickMagmaGeyser.h"
#include "game/actor/gimmick/trigger/GimmickSwitch.h"
#include "BaseScene.h"
#include "Player.h"
#include "SceneManager.h"
#include "GhostRecorder.h"
#include "DebugConsole.h"
#include "MovingFloorSupport.h"
#include <algorithm>
#include <cmath>

namespace {
float DistanceXZ(const Vector3& a, const Vector3& b) {
    // 乗り場との近さは高さを除いた水平面で判定する。
    const float x = a.x - b.x, z = a.z - b.z;
    return std::sqrt(x * x + z * z);
}

} // namespace

BaseEnemy* RidingPlatformController::FindEnemy(BaseScene& scene, const std::string& name) const {
    // リプレイから除外された敵は乗車イベントの対象にしない。
    for (auto& object : scene.GetObjects()) {
        if (object && !object->IsReplayRemoved() && object->GetName() == name)
            return dynamic_cast<BaseEnemy*>(object.get());
    }
    return nullptr;
}

void RidingPlatformController::Reset(GimmickMovingFloor& floor, BaseScene& scene) {
    // 床・敵・当たり判定を演出開始前の配置へ戻し、再乗車できる状態にする。
    player_.Stop(false);
    floor.SetTranslate(start_);
    floor.UpdateLocalMatrix();
    floor.UpdateWorldMatrix();
    running_ = false;
    absentTime_ = 0.0f;
    for (auto& boarder : boarders_) {
        boarder.active = false;
        boarder.released = false;
        boarder.elapsed = 0;
        if (auto* enemy = FindEnemy(scene, boarder.name); enemy && !enemy->isDead && !enemy->IsCarried()) {
            enemy->SetTranslate(boarder.origin);
            enemy->SetCollisionAttribute(boarder.attribute);
            enemy->SetCollisionMask(boarder.mask);
            enemy->SetDormant(true);
        }
    }
}

void RidingPlatformController::Update(GimmickMovingFloor& floor, float deltaTime) {
    // Timeline の信号と床の軌跡を再生し、乗員と敵を同じフレームの移動量で運ぶ。
    auto* manager = SceneManager::GetInstance();
    auto* scene = manager ? manager->GetCurrentScene() : nullptr;
    if (!scene)
        return;
    const std::string path = floor.GetPathMoverComponent()->GetRideSequence();
    // 同じ床の旧RecorderとTimelineを同時に動かさない。
    if (floor.recorder_)
        floor.recorder_->Stop(false);
    // Editorで同じファイルへ保存した変更も、次のゲーム開始時に読み直す。
    if (loadedPath_ != path || (manager->IsPlaying() && !wasPlaying_ && ready_)) {
        if (ready_)
            Reset(floor, *scene);
        loadedPath_ = path;
        ready_ = sequence_.Load(path);
        start_ = floor.GetTranslate();
        boarders_.clear();
        ferry_ = std::any_of(sequence_.signals.begin(), sequence_.signals.end(),
                             [](const auto& signal) { return signal.enabled && signal.signal == "ride.ferry"; });
        if (!ready_) {
            DebugConsole::GetInstance()->AddLog("乗車床: タイムラインを読み込めません: " + path);
            return;
        }
        for (const auto& track : sequence_.transformTracks) {
            if (track.enabled && !track.muted && !track.relative && !track.anchorRelative &&
                track.binding.targetName == floor.GetName() && !track.keys.empty()) {
                start_ = track.keys.front().position;
                break;
            }
        }
        player_.Initialize(manager);
        player_.SetSequence(&sequence_);
        for (const auto& signal : sequence_.signals) {
            if (!signal.enabled || signal.signal != "ride.board")
                continue;
            auto* enemy = FindEnemy(*scene, signal.binding.targetName);
            if (!enemy || enemy->isDead)
                continue;
            Boarder boarder;
            boarder.name = enemy->GetName();
            boarder.origin = enemy->GetTranslate();
            boarder.attribute = enemy->GetCollisionAttribute();
            boarder.mask = enemy->GetCollisionMask();
            const auto payload = json::parse(signal.payload, nullptr, false);
            if (payload.is_object()) {
                if (payload.contains("duration") && payload["duration"].is_number())
                    boarder.duration = std::clamp(payload["duration"].get<float>(), .4f, 4.0f);
                if (payload.contains("height") && payload["height"].is_number())
                    boarder.height = std::clamp(payload["height"].get<float>(), 1.0f, 15.0f);
                if (payload.contains("offset") && payload["offset"].is_array() && payload["offset"].size() == 3 &&
                    payload["offset"][0].is_number() && payload["offset"][1].is_number() &&
                    payload["offset"][2].is_number())
                    boarder.offset = {payload["offset"][0], payload["offset"][1], payload["offset"][2]};
            }
            boarders_.push_back(boarder);
        }
        player_.SetSignalCallback([this, &floor](Object3d* target, const CinematicSignalMarker& signal, bool preview) {
            if (preview || !target)
                return;
            if (signal.signal == "ride.fireball") {
                if (auto* vent = dynamic_cast<GimmickMagmaGeyser*>(target)) {
                    vent->RequestFireballEruption();
                }
                return;
            }
            if (signal.signal != "ride.board")
                return;
            for (auto& boarder : boarders_) {
                if (boarder.name == target->GetName() && !boarder.released)
                    boarder.active = true;
            }
        });
    }
    if (!ready_)
        return;
    const bool playing = manager->IsPlaying();
    if (!playing) {
        if (wasPlaying_)
            Reset(floor, *scene);
        // 編集中はGhostDirectorのプレビューを上書きしない。
        for (auto& b : boarders_)
            if (auto* enemy = FindEnemy(*scene, b.name))
                enemy->SetDormant(false);
        wasPlaying_ = false;
        return;
    }
    if (!wasPlaying_)
        Reset(floor, *scene);
    wasPlaying_ = true;
    auto* rider = dynamic_cast<Player*>(scene->GetPlayer());
    if (!rider || deltaTime <= 0)
        return;
    const Vector3 previous = floor.GetTranslate();
    const bool riderOnDeck = IsStandingOnMovingFloor(floor, *rider);
    // 落下は通常のミス。復活して乗り場へ戻った時だけ床を再準備する。
    if (running_ && !riderOnDeck &&
        (!ferry_ || (rider->IsGrounded() && absentTime_ > 1.2f && rider->GetWorldPosition().y >= start_.y - 0.5f)) &&
        DistanceXZ(previous, start_) > (ferry_ ? 24.0f : 65.0f) &&
        DistanceXZ(rider->GetWorldPosition(), start_) < (ferry_ ? 36.0f : 22.0f) && !rider->isDead) {
        Reset(floor, *scene);
        return;
    }
    if (!running_ && riderOnDeck) {
        player_.Play(false);
        running_ = true;
    }
    // 全キャラの前フレームの接地を見てからTimelineを進める。
    std::vector<Character*> passengers;
    for (auto& object : scene->GetObjects()) {
        auto* actor = object && !object->IsReplayRemoved() ? dynamic_cast<Character*>(object.get()) : nullptr;
        if (actor && IsStandingOnMovingFloor(floor, *actor))
            passengers.push_back(actor);
    }
    if (running_) {
        float step = deltaTime;
        if (ferry_) {
            // 飛び上がり中は止めず、島へ降りた後はその場で再乗車を待つ。
            absentTime_ = riderOnDeck ? 0.0f : absentTime_ + deltaTime;
            if (rider->isDead || absentTime_ > 1.2f)
                step = 0.0f;
        }
        const float now = player_.GetCurrentTime();
        for (const auto& signal : sequence_.signals) {
            if (!signal.enabled || (signal.signal != "ride.wait_clear" && signal.signal != "ride.wait_switch") ||
                signal.time + .001f < now)
                continue;
            // 更新後に巻き戻すと乗員・効果が先へ進むため、更新量を停止時刻で切る。
            const bool blocked =
                std::any_of(scene->GetObjects().begin(), scene->GetObjects().end(), [&](const auto& object) {
                    if (!object || object->GetName() != signal.binding.targetName || object->IsReplayRemoved())
                        return false;
                    if (signal.signal == "ride.wait_switch") {
                        const auto* button = dynamic_cast<GimmickSwitch*>(object.get());
                        return button && !button->IsOutputActive();
                    }
                    return !object->isDead && object->GetIsVisible();
                });
            if (blocked)
                step = std::min(step, std::max(0.0f, signal.time - now));
        }
        if (step > 0.0f)
            player_.Update(step);
    }
    const auto displacement = floor.GetTranslate() - previous;
    for (auto* actor : passengers) {
        actor->SetTranslate(actor->GetTranslate() + displacement);
        actor->UpdateLocalMatrix();
        actor->UpdateWorldMatrix();
        if (auto* player = dynamic_cast<Player*>(actor))
            player->SetPlatformMotion(displacement * (1.0f / deltaTime));
    }
    for (auto& boarder : boarders_) {
        auto* enemy = FindEnemy(*scene, boarder.name);
        if (!enemy || enemy->isDead || boarder.released)
            continue;
        // プレイヤーが先に捕まえた敵を、乗り込み演出で奪い返さない。
        if (enemy->IsCarried()) {
            boarder.released = true;
            enemy->SetDormant(false);
            continue;
        }
        enemy->SetDormant(true);
        if (!boarder.active)
            continue;
        enemy->SetCollisionAttribute(0);
        enemy->SetCollisionMask(0);
        boarder.elapsed += deltaTime;
        const float t = std::clamp(boarder.elapsed / boarder.duration, 0.0f, 1.0f);
        const auto c = floor.GetColliderConfig();
        Vector3 destination = floor.GetTranslate() + boarder.offset;
        destination.y += (c.center.y + c.size.y) * floor.GetScale().y;
        auto position = Math::Lerp(boarder.origin, destination, t);
        position.y += 4.0f * boarder.height * t * (1.0f - t);
        enemy->SetTranslate(position);
        if (t >= 1) {
            boarder.released = true;
            enemy->SetDormant(false);
            enemy->SetVelocity({});
            enemy->SetGrounded(true);
            enemy->SetCollisionAttribute(boarder.attribute);
            enemy->SetCollisionMask(boarder.mask);
        }
        enemy->UpdateLocalMatrix();
        enemy->UpdateWorldMatrix();
    }
}
