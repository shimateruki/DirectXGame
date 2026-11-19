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
    if (!crownDefeatVfxSequence_.Load("Resources/json/scenario/stage3/crown_defeat.json"))
        return false;
    // シーケンスの役割名を、この場で受け取ったボスと現プレイヤーへ結び付けます。
    crownDefeatVfxPlayer_.Initialize(SceneManager::GetInstance());
    crownDefeatVfxPlayer_.SetBindingResolver([this,boss](const CinematicObjectBinding& binding) -> engine::Object3D* {
        if (binding.targetName=="$Actor") return boss;
        if (binding.targetName=="$Gate") return player_;
        return nullptr;
    });
    crownDefeatVfxPlayer_.SetSequence(&crownDefeatVfxSequence_);
    // 役者と勝利カットを全て確認してからプレイヤーをロックし、欠落時は演出を始めません。
    auto issues=crownDefeatVfxPlayer_.ValidateBindings();
    if (GetCrownDefeatMarkerTime("victory")<=0)
        issues.push_back("勝利カットの節目がありません。");
    if (!issues.empty()) {
        for (const auto& issue:issues) LOG_ERROR("Crown defeat: {}",issue);
        crownDefeatVfxPlayer_.Stop(true);
        return false;
    }
    // 中断時に戻す値を退避し、勝利カットで使う向きをボスからプレイヤーへの方向で決めます。
    auto& s=crownDefeat_;
    s.boss=boss;
    s.hadControl=player_->IsControlActive();
    s.playerYaw=player_->GetMoveYaw();
    const auto forward=player_->GetTranslate()-boss->GetTranslate();
    // ボスからプレイヤーへ向かう水平ベクトルをラジアンへ変え、勝利カットの操作向きにする。
    s.victoryYaw=std::atan2(forward.x,forward.z);
    s.victoryTime=GetCrownDefeatMarkerTime("victory");
    player_->BeginCinematicLock(true);
    player_->SetVelocity({});
    if (player_->GetEnemyMorphType()==Player::EnemyMorphType::Crown) {
        player_->SetMaterialType(0);
        player_->SetColor({1,.77f,.25f,1});
    }
    // 再生中に通常の敵更新で消えないよう、ボスの撃破確定を演出の終了まで保留する。
    boss->HoldFinalDefeat();
    s.active=true;
    // 一時部品・姿勢・見た目・粒子・カメラ・黒帯はエンジンの同じ時計で再生する。
    crownDefeatVfxPlayer_.Play(false);
    UpdateCrownDefeat(0);
    return true;
}

void GamePlayScene::UpdateCrownDefeat(float deltaTime) {
    if (!crownDefeat_.active) return;
    // 必要な役者がいない状態で演出を進めず、一時的な演出状態を解放する。
    if (!player_ || !crownDefeat_.boss || player_->IsDead()) {
        ResetCrownDefeat();
        return;
    }
    if (crownMoviePreview_.active) {
        // プレビューは完了しても画面を保持し、ゲームの撃破・クリア通知を出さない。
        // プレビューでは次の再生秒数を直接指定し、正の時間を進めた場合だけ通過したイベントを出す。
        crownDefeatVfxPlayer_.SetTime(crownDefeatVfxPlayer_.GetCurrentTime()+(std::max)(0.0f,deltaTime),deltaTime>0);
    } else {
        crownDefeatVfxPlayer_.Update((std::max)(0.0f,deltaTime));
        // 本編で終了に達する前に再生が止まった場合は、撃破完了の扱いにせず解除する。
        if (!crownDefeatVfxPlayer_.IsPlaying() && !crownDefeatVfxPlayer_.IsFinished()) {
            ResetCrownDefeat();
            return;
        }
    }
    // 操作用の向きはPlayer固有の前方軸を使うため、ゲーム側で通知する。
    player_->SetMoveYaw(crownDefeatVfxPlayer_.GetCurrentTime()>=crownDefeat_.victoryTime ? crownDefeat_.victoryYaw : crownDefeat_.playerYaw);
    player_->UpdateWorldMatrix(true);
    // 本編の再生完了だけを通常のクリア処理へ渡す。
    if (crownDefeatVfxPlayer_.IsFinished() && !crownMoviePreview_.active) FinishCrownDefeat();
}

float GamePlayScene::GetCrownDefeatMarkerTime(const std::string& name) const {
    return crownDefeatVfxSequence_.GetSignalTime(name);
}

void GamePlayScene::FinishCrownDefeat() {
    auto& s=crownDefeat_;
    if (!s.active || crownMoviePreview_.active) return;
    // 撃破演出の最終ポーズを維持したまま再生を止め、完了を一回だけ確定します。
    crownDefeatVfxPlayer_.Stop(false);
    s.active=false;
    s.completed=true;
    // 完了状態を先に立て、旧報酬ムービーを重ねず通常のクリア演出へ渡す。
    s.boss->CompleteFinalDefeat();
    for (const auto& object:GetObjects())
        if (object->GetName()=="Stage3_HighCrown_VictoryCrown") object->SetIsVisible(false);
    // 攻略済み状態を控えてから、通常ゴールの祝賀段階とUIへ引き継ぎます。
    goalWasStageCleared_=GameDataManager::GetInstance()->IsStageCleared(StageManager::GetInstance()->GetCurrentStageIndex());
    SetIsGoal(true);
    goalPresentationState_=GoalPresentationState::Celebrating;
    // 既存のゴール進行に接続し、帰還を受け付ける時刻より最大2.5秒前から表示を進める。
    goalPresentationTimer_.Seek((std::max)(0.0f,goalClearPlayerAnimator_.GetTuning().readyTime-2.5f));
    goalPresentationTimer_.Resume();
    goalPlayerSnapshotValid_=goalCrownSnapshotValid_=goalCameraSnapshotValid_=false;
    goalCinematicPlayer_.Stop(false);
    UpdateGoalPresentationOverlay();
}

void GamePlayScene::ResetCrownDefeat() {
    auto& s=crownDefeat_;
    if (!s.boss) return;
    // 共通再生器が保存した姿勢・材質・表示・カメラを戻してから、ゲームのロックを解除する。
    crownDefeatVfxPlayer_.Stop(true);
    // ボスが現在もシーンにいることを確認してから、保留中の撃破を解除します。
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
