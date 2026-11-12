#pragma once
#ifdef USE_IMGUI
#include "game/scene/gameplay/GamePlayScene.h"
#include "game/system/progress/StageManager.h"
#include "game/system/progress/GameDataManager.h"
#include "game/system/scene/SceneGameplayAccess.h"
#include "SceneManager.h"
#include "CameraManager.h"
#include "PostEffect.h"
#include "WinApp.h"
#include "Player.h"
#include "game/crown/CrownFinaleTuning.h"
#include <fstream>
#include <cmath>

// 明示した検証起動だけで、実Stage3の複製を生成しGPU描画・巻戻し・片付けを通す。
namespace CinematicRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_CINEMATIC_TEST",nullptr,0)>0; }
inline void AfterDraw(SceneManager* manager,const std::string& path) {
    static std::ofstream log(path,std::ios::trunc);
    static ULONGLONG started=GetTickCount64();
    static int phase=0,frames=0;
    static size_t objectCount=0;
    static Player* originalPlayer=nullptr;
    static Camera* originalCamera=nullptr;
    static engine::Vector3 initialEye{},initialPlayer{};
    static float bars=0;
    static int crowns=0;
    static bool done=false;
    static bool defeat=false;
    if (done || !manager) return;
    auto finish=[&](const char* message) { log<<message<<std::endl;done=true;WinApp::CloseNow(); };
    if (GetTickCount64()-started>240000) { finish("FAIL: cinematic timeout");return; }
    if (manager->IsTransitioning()) return;
    if (phase==0) {
        StageManager::GetInstance()->SetCurrentStageById("stage3");manager->ChangeScene("GAMEPLAY");phase=1;return;
    }
    auto* game=dynamic_cast<GamePlayScene*>(manager->GetCurrentScene());
    if (!game) return;
    if (phase==1) {
        if (++frames<5) return;
        objectCount=game->GetObjects().size(); originalPlayer=GetScenePlayer(game);
        originalCamera=CameraManager::GetInstance()->GetActiveCameraOverride();
        bars=PostEffect::GetInstance()->GetParams()->cinemaBarHeight;
        crowns=GameDataManager::GetInstance()->GetClearedStageCount();
        // 準備後の再生・プレビュー開始で、部品の対象解決が繰り返されないことを確かめる。
        engine::CinematicSequence fixture;
        fixture.duration=0.25f;
        engine::CinematicObjectClip part;
        part.name="__CinematicProbePart"; part.source="$Actor"; part.keys.push_back({});
        part.followPosition=part.followRotation=part.followScale=part.inheritVisibility=true;
        part.rotationMask={0,1,0};
        fixture.presentation.objects.push_back(part);
        engine::CinematicPlayer probe;
        int resolutions=0;
        auto resolveActor=[&](const auto& binding) -> engine::Object3D* {
            if (binding.targetName!="$Actor") return nullptr;
            ++resolutions;
            return originalPlayer;
        };
        probe.Initialize(manager);
        probe.SetBindingResolver(resolveActor);
        probe.SetSequence(&fixture);
        const int prepared=resolutions;
        probe.Play(false);
        if (!probe.IsPlaying() || resolutions!=prepared) { finish("FAIL: Play rebuilt prepared objects");return; }
        // 退避値のまま止まらず、現在の位置・拡縮・表示を借り、指定軸だけを継承する。
        const auto savedTransform=static_cast<const engine::Object3D*>(originalPlayer)->GetTransform();
        const bool savedVisible=originalPlayer->GetIsVisible();
        originalPlayer->SetTranslate(savedTransform.translate+engine::Vector3{2,3,4});
        originalPlayer->SetRotation({.7f,.4f,.3f});
        originalPlayer->SetScale(savedTransform.scale*2);
        originalPlayer->SetIsVisible(false);
        probe.SetTime(.1f,false);
        auto* followed=probe.ResolveBinding({part.name,-1});
        const bool followedCorrectly=followed &&
            engine::Math::Length(followed->GetTranslate()-originalPlayer->GetTranslate())<.0001f &&
            engine::Math::Length(followed->GetScale()-originalPlayer->GetScale())<.0001f &&
            engine::Math::Length(followed->GetRotation()-engine::Vector3{0,.4f,0})<.0001f && !followed->GetIsVisible();
        originalPlayer->SetTranslate(savedTransform.translate);
        originalPlayer->SetRotation(savedTransform.rotate);
        originalPlayer->SetScale(savedTransform.scale);
        originalPlayer->SetIsVisible(savedVisible);
        originalPlayer->UpdateWorldMatrix(true);
        if (!followedCorrectly) { finish("FAIL: live attachment used stale pose or incorrect rotation axes");return; }
        probe.Pause(); probe.Resume(false); probe.Update(0.3f);
        if (!probe.IsFinished()) { finish("FAIL: shared playback did not finish");return; }
        probe.Stop(true);
        probe.SetSequence(&fixture);
        const int previewPrepared=resolutions;
        probe.BeginPreview();
        if (!probe.IsPreviewing() || resolutions!=previewPrepared) { finish("FAIL: BeginPreview rebuilt prepared objects");return; }
        probe.EndPreview(true);
        probe.SetSequence(&fixture);
        const int beforeResolverChange=resolutions;
        probe.SetBindingResolver(resolveActor);
        probe.Play(false);
        if (!probe.IsPlaying() || resolutions==beforeResolverChange) { finish("FAIL: changed resolver kept stale bindings");return; }
        probe.Stop(true);
        fixture.presentation.reference=fixture.presentation.lookToward="$Actor";
        fixture.presentation.view.push_back({}); fixture.cameraShots.push_back({});
        probe.SetSequence(&fixture);
        const auto cameraIssues=probe.ValidateBindings();
        if (std::none_of(cameraIssues.begin(),cameraIssues.end(),[](const auto& issue) {
            return issue.find("視点キーとCamera Shot")!=std::string::npos;
        })) { finish("FAIL: mixed camera controllers were accepted");return; }
        probe.Stop(true);
        log<<"PREPARATION reused by Play and BeginPreview; pause/resume/completion passed"<<std::endl;
        if (!game->BeginCrownMoviePreview(defeat)) { finish("FAIL: could not begin crown preview");return; }
        const float duration=defeat?game->GetCrownDefeatDuration():game->GetCrownFinaleTiming().movieDuration;
        if (duration<9) { finish("FAIL: wrong cinematic duration");return; }
        initialEye=CameraManager::GetInstance()->GetActiveCamera()->GetEye();
        initialPlayer=GetScenePlayer(game)->GetTranslate();
        log<<(defeat?"DEFEAT":"INHERITANCE")<<" BEGIN duration="<<duration<<" objects="<<game->GetObjects().size()<<std::endl;
        phase=2;frames=0;return;
    }
    if (phase==2) {
        game->StepCrownMoviePreview(1.0f/60.0f);
        if (++frames<(defeat?300:900)) return;
        auto delta=CameraManager::GetInstance()->GetActiveCamera()->GetEye()-initialEye;
        if (delta.x*delta.x+delta.y*delta.y+delta.z*delta.z<0.01f) { finish("FAIL: cinematic camera did not move");return; }
        game->RewindCrownMoviePreview();
        delta=CameraManager::GetInstance()->GetActiveCamera()->GetEye()-initialEye;
        const auto playerDelta=GetScenePlayer(game)->GetTranslate()-initialPlayer;
        if (delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>0.001f ||
            playerDelta.x*playerDelta.x+playerDelta.y*playerDelta.y+playerDelta.z*playerDelta.z>0.001f) {
            finish("FAIL: rewind did not restore initial pose/camera");return;
        }
        log<<"REWIND after "<<frames<<" drawn frames"<<std::endl;phase=3;frames=0;return;
    }
    if (phase==3) {
        // 一巡目の巻戻しは60fps、通し再生は30fpsの刻みでも節目を落とさないことを確かめる。
        game->StepCrownMoviePreview(1.0f/30.0f);
        const float duration=defeat?game->GetCrownDefeatDuration():game->GetCrownFinaleTiming().movieDuration;
        if (++frames<static_cast<int>(duration*30)+3) return;
        if (!defeat && GetScenePlayer(game)->GetEnemyMorphType()!=Player::EnemyMorphType::Crown) {
            finish("FAIL: inheritance did not grant Crown at the formation marker");return;
        }
        if (GameDataManager::GetInstance()->GetClearedStageCount()!=crowns) { finish("FAIL: preview changed game progress");return; }
        game->EndCrownMoviePreview();
        if (game->GetObjects().size()!=objectCount || GetScenePlayer(game)!=originalPlayer ||
            CameraManager::GetInstance()->GetActiveCameraOverride()!=originalCamera ||
            std::abs(PostEffect::GetInstance()->GetParams()->cinemaBarHeight-bars)>0.0001f) {
            finish("FAIL: preview cleanup did not restore scene/player/camera/bars");return;
        }
        log<<"END restored objects="<<objectCount<<std::endl;phase=4;return;
    }
    if (!defeat) { defeat=true;phase=1;frames=0;return; }
    finish("PASS: inheritance and defeat rendered, rewound, held final poses and restored editor state without progress changes");
}
}
#endif
