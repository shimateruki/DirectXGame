#include "SelectedActorSession.h"
#ifdef USE_IMGUI
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3D.h"
#include <algorithm>
#include <cmath>
namespace { constexpr float kStep = 1.0f / 60.0f; constexpr uint32_t kActorHidden = 1u << 12; }
void SelectedActorSession::Initialize(SceneManager* manager) {
    manager_ = manager; current_ = this;
    if (auto& call = ServicesTable().initialize) call(manager, *this);
}
void SelectedActorSession::Finalize() {
    Stop();
    if (auto& call = ServicesTable().finalize) call();
    manager_ = nullptr; targetId_ = 0;
    if (current_ == this) current_ = nullptr;
}
engine::Object3D* SelectedActorSession::GetTarget() const {
    // Sceneが変わったら旧Objectのポインタには触れない。IDを現Sceneから毎回解決する。
    if (!targetId_ || !manager_ || manager_->GetSceneGeneration() != sceneGeneration_) return nullptr;
    auto* scene = manager_->GetCurrentScene();
    if (!scene) return nullptr;
    for (const auto& object : scene->GetObjects())
        if (object && object->GetReplayId() == targetId_ && !object->IsReplayRemoved()) return object.get();
    return nullptr;
}
void SelectedActorSession::SetTarget(engine::Object3D* target) {
    if (target && target->IsEditorInternal()) target = nullptr;
    const uint64_t id = target ? target->EnsureReplayId() : 0;
    const uint64_t generation = manager_ ? manager_->GetSceneGeneration() : 0;
    const std::string kind = target && ServicesTable().identity ? ServicesTable().identity(*target) : std::string{};
    if (id == targetId_ && generation == sceneGeneration_ && kind == targetKind_) return;
    // 元の対象がまだ解決できる間に停止し、一時非表示を解除してから識別情報を新しい対象へ差し替える。
    Stop(); targetId_ = id; sceneGeneration_ = generation; targetKind_ = kind;
    if (target && ServicesTable().select) ServicesTable().select(*target);
}
bool SelectedActorSession::Begin() {
    if (changing_ || !manager_ || manager_->IsPlaying() || manager_->IsTransitioning() || !GetTarget()) return false;
    Stop(); changing_ = true;
    // ゲーム側の準備中は停止などの再入を防ぐ。確認用の実体を作れた場合に限り元の対象を一時非表示にする。
    active_ = ServicesTable().begin && ServicesTable().begin();
    changing_ = false; accumulator_ = 0;
    if (auto* target = GetTarget()) target->SetEditorHiddenReason(kActorHidden, active_);
    return active_;
}
void SelectedActorSession::Stop() {
    if (changing_) return;
    // 終了コールバックの前に再生状態を止め、未処理の時間も捨てる。片付けが終わってから元の表示を戻す。
    const bool wasActive = active_; active_ = false; accumulator_ = 0;
    if (wasActive && ServicesTable().end) {
        changing_ = true; ServicesTable().end(); changing_ = false;
    }
    if (auto* target = GetTarget()) target->SetEditorHiddenReason(kActorHidden, false);
}
void SelectedActorSession::Restart() { Begin(); }
// 停止中なら先頭から準備し、連続再生を止めて 1/60 秒だけ進める。既に残っている端数時間は使わない。
void SelectedActorSession::StepFrame() {
    if (!active_ && !Begin()) return;
    if (ServicesTable().setPlaying) ServicesTable().setPlaying(false);
    if (ServicesTable().step) ServicesTable().step(kStep);
    accumulator_ = 0;
}
void SelectedActorSession::Seek(float seconds) {
    if (!std::isfinite(seconds) || !Begin()) return;
    if (ServicesTable().setPlaying) ServicesTable().setPlaying(false);
    // 最大60秒に制限し、誤入力でUIが無限に再構築されることを防ぐ。
    const int frames = static_cast<int>(std::round(std::clamp(seconds, 0.0f, 60.0f) / kStep));
    for (int i=0; i<frames && active_; ++i)
        if (ServicesTable().step) ServicesTable().step(kStep);
}
void SelectedActorSession::Update(float realDeltaTime) {
    if (!manager_ || manager_->IsPlaying() || manager_->IsTransitioning() || (targetId_ && !GetTarget())) {
        Stop(); return;
    }
    if (ServicesTable().maintain) ServicesTable().maintain(realDeltaTime);
    if (!active_ || !ServicesTable().playback) return;
    auto state = ServicesTable().playback();
    if (!state.playing) { accumulator_ = 0; return; }
    // 通常フレームの経過秒を倍率で調整して蓄積する。停止や長い描画待ちで大量の固定更新が走らないよう入力時間を制限する。
    accumulator_ += std::clamp(realDeltaTime, 0.0f, 0.25f) * std::clamp(state.speed, 0.05f, 2.0f);
    // 1/60 秒に達した分を一段ずつ消費する。1 フレームで複数段進んでいても、各段の後で演出終了を確認する。
    while (accumulator_ >= kStep && active_) {
        accumulator_ -= kStep;
        if (ServicesTable().step) ServicesTable().step(kStep);
        state = ServicesTable().playback();
        // 再生終端に着いたらループ時だけ初期状態を作り直す。残りの蓄積時間は次周へ持ち越さない。
        if (state.time >= state.duration && state.duration > 0) {
            if (state.loop) Restart();
            else if (ServicesTable().setPlaying) ServicesTable().setPlaying(false);
            accumulator_ = 0; break;
        }
    }
}
void SelectedActorSession::DrawInspector(engine::Object3D* target, ProjectWindow* assets) {
    SetTarget(target);
    if (GetTarget() && ServicesTable().draw) ServicesTable().draw(assets);
}
void SelectedActorSession::DrawOverlay(float x,float y,float width,float height) {
    if (GetTarget() && ServicesTable().overlay) ServicesTable().overlay(x,y,width,height);
}
#endif
