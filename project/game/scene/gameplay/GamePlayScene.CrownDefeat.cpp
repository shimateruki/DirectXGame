#include "game/scene/gameplay/GamePlayScene.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "game/system/progress/GameDataManager.h"
#include "game/system/progress/StageManager.h"
#include "SceneManager.h"
#include "Log.h"
#include <algorithm>
#include <cmath>

bool GamePlayScene::StartCrownDefeat(EnemyFalseKingSlime* boss) {
    // 資源と対象の検証を済ませてから、ゲーム側の操作と撃破判定を止める。
    if (!boss || !player_ || player_->IsDead() || crownDefeat_.active || crownDefeat_.completed || crownFinale_.active || isGoal_)
        return false;
    if (!crownDefeatVfxSequence_.Load("Resources/json/scenario/crown_defeat.json"))
        return false;
    crownDefeatVfxPlayer_.Initialize(SceneManager::GetInstance());
    crownDefeatVfxPlayer_.SetBindingResolver([this,boss](const CinematicObjectBinding& binding) -> engine::Object3D* {
        if (binding.targetName=="$Actor") return boss;
        if (binding.targetName=="$Gate") return player_;
        return nullptr;
    });
    crownDefeatVfxPlayer_.SetSequence(&crownDefeatVfxSequence_);
    auto issues=crownDefeatVfxPlayer_.ValidateBindings();
    if (GetCrownDefeatMarkerTime("victory")<=0)
        issues.push_back("勝利カットの節目がありません。");
    if (!issues.empty()) {
        for (const auto& issue:issues) LOG_ERROR("Crown defeat: {}",issue);
        crownDefeatVfxPlayer_.Stop(true);
        return false;
    }
    auto& s=crownDefeat_;
    s.boss=boss;
    s.hadControl=player_->IsControlActive();
    s.playerYaw=player_->GetMoveYaw();
    const auto forward=player_->GetTranslate()-boss->GetTranslate();
    s.victoryYaw=std::atan2(forward.x,forward.z);
    s.victoryTime=GetCrownDefeatMarkerTime("victory");
    player_->BeginCinematicLock(true);
    player_->SetVelocity({});
    if (player_->GetEnemyMorphType()==Player::EnemyMorphType::Crown) {
        player_->SetMaterialType(0);
        player_->SetColor({1,.77f,.25f,1});
    }
    boss->HoldFinalDefeat();
    s.active=true;
    // 一時部品・姿勢・見た目・粒子・カメラ・黒帯はエンジンの同じ時計で再生する。
    crownDefeatVfxPlayer_.Play(false);
    UpdateCrownDefeat(0);
    return true;
}

void GamePlayScene::UpdateCrownDefeat(float deltaTime) {
    if (!crownDefeat_.active) return;
    if (!player_ || !crownDefeat_.boss || player_->IsDead()) {
        ResetCrownDefeat();
        return;
    }
    if (crownMoviePreview_.active) {
        // プレビューは完了しても画面を保持し、ゲームの撃破・クリア通知を出さない。
        crownDefeatVfxPlayer_.SetTime(crownDefeatVfxPlayer_.GetCurrentTime()+(std::max)(0.0f,deltaTime),deltaTime>0);
    } else {
        crownDefeatVfxPlayer_.Update((std::max)(0.0f,deltaTime));
        if (!crownDefeatVfxPlayer_.IsPlaying() && !crownDefeatVfxPlayer_.IsFinished()) {
            ResetCrownDefeat();
            return;
        }
    }
    // 操作用の向きはPlayer固有の前方軸を使うため、ゲーム側で通知する。
    player_->SetMoveYaw(crownDefeatVfxPlayer_.GetCurrentTime()>=crownDefeat_.victoryTime ? crownDefeat_.victoryYaw : crownDefeat_.playerYaw);
    player_->UpdateWorldMatrix(true);
    if (crownDefeatVfxPlayer_.IsFinished() && !crownMoviePreview_.active) FinishCrownDefeat();
}

float GamePlayScene::GetCrownDefeatMarkerTime(const std::string& name) const {
    return crownDefeatVfxSequence_.GetSignalTime(name);
}

void GamePlayScene::FinishCrownDefeat() {
    auto& s=crownDefeat_;
    if (!s.active || crownMoviePreview_.active) return;
    crownDefeatVfxPlayer_.Stop(false);
    s.active=false;
    s.completed=true;
    // 完了状態を先に立て、旧報酬ムービーを重ねず通常のクリア演出へ渡す。
    s.boss->CompleteFinalDefeat();
    for (const auto& object:GetObjects())
        if (object->GetName()=="Stage3_HighCrown_VictoryCrown") object->SetIsVisible(false);
    goalWasStageCleared_=GameDataManager::GetInstance()->IsStageCleared(StageManager::GetInstance()->GetCurrentStageIndex());
    SetIsGoal(true);
    goalPresentationState_=GoalPresentationState::Celebrating;
    goalPresentationTimer_=(std::max)(0.0f,goalClearPlayerAnimator_.GetTuning().readyTime-2.5f);
    goalPlayerSnapshotValid_=goalCrownSnapshotValid_=goalCameraSnapshotValid_=false;
    goalCinematicPlayer_.Stop(false);
    UpdateGoalPresentationOverlay();
}

void GamePlayScene::ResetCrownDefeat() {
    auto& s=crownDefeat_;
    if (!s.boss) return;
    // 共通再生器が保存した姿勢・材質・表示・カメラを戻してから、ゲームのロックを解除する。
    crownDefeatVfxPlayer_.Stop(true);
    for (const auto& object:GetObjects())
        if (object.get()==s.boss) { s.boss->CancelFinalDefeat(); break; }
    if (player_) {
        player_->SetMoveYaw(s.playerYaw);
        player_->EndCinematicLock(s.hadControl);
    }
    s=CrownDefeatState{};
}

void GamePlayScene::DrawCrownDefeatWorld(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // Drawで取得した共有ライトを渡し、直前の描画が残したGPU状態に依存させない。
    crownDefeatVfxPlayer_.DrawPresentation(pointLightResource, spotLightResource);
}
