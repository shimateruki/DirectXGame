#pragma once
#ifdef USE_IMGUI
#include "game/scene/tutorial/TutorialScene.h"
#include "game/system/tutorial/TutorialDirector.h"
#include "game/system/navigation/DestinationGuide.h"
#include "GateEntryRuntimeProbe.h"
#include "Fade.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include <fstream>
#include <limits>
#include <stdexcept>

// 専用起動で実シーンを使い、到着・再挑戦・停止・入場演出とGPU描画を確認する。
namespace TutorialExitGuideRuntimeProbe {
inline bool Enabled() { static const bool value = GetEnvironmentVariableA("CG2_TUTORIAL_EXIT_GUIDE_TEST", nullptr, 0) > 0; return value; }
inline bool play = false;
inline bool WantsPlay() { return play; }
struct Access {
    static TutorialDirector* Director(TutorialScene& scene) { return scene.tutorialDirector_.get(); }
    static void Update(TutorialScene& scene, float dt) { scene.UpdateExitGuide(dt); }
    static DestinationGuide& Guide(TutorialScene& scene) { return scene.exitGuide_; }
};
inline void Require(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
inline bool Near(float a, float b) { return std::abs(a-b) < .002f; }
inline engine::Object3d* Find(TutorialScene& scene, const char* name) {
    for (const auto& object : scene.GetObjects()) if (object && object->GetName() == name) return object.get();
    return nullptr;
}
inline void CaptureView(const std::string& path) {
    auto* dx = DirectXCommon::GetInstance();
    // Editorの最終GameViewは1番。0番はBloomを使わないシーンでは前の画面が残る。
    auto* texture = PostEffect::GetInstance()->GetRenderTexture(1);
    DirectX::ScratchImage image, converted;
    Require(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(), texture, false, image,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)), "GameView capture failed");
    Require(SUCCEEDED(DirectX::Convert(*image.GetImage(0,0,0), DXGI_FORMAT_R8G8B8A8_UNORM,
        DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted)), "GameView image conversion failed");
    Require(SUCCEEDED(DirectX::SaveToWICFile(*converted.GetImage(0,0,0), DirectX::WIC_FLAGS_NONE,
        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), std::filesystem::path(path).c_str())), "GameView PNG save failed");
}
inline void PrepareCaptureCamera(SceneManager* manager) {
    if (!play || !manager || manager->IsTransitioning()) return;
    auto* scene=dynamic_cast<TutorialScene*>(manager->GetCurrentScene());
    if (!scene || !scene->GetPlayer()) return;
    auto* camera=CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) return;
    const auto p=scene->GetPlayer()->GetWorldPosition();
    const auto eye=p+engine::Vector3{-7,8,-11};
    const auto target=p+engine::Vector3{4,1,0};
    const auto difference=target-eye;
    const float length=std::sqrt(difference.x*difference.x+difference.y*difference.y+difference.z*difference.z);
    camera->ApplyReplayView(eye,target,{std::asin(-difference.y/length),std::atan2(difference.x,difference.z),0},.65f,.1f,1000);
    for (const auto& object : scene->GetObjects()) if (object) object->RefreshRenderCameraData();
}
inline void GenericCases(engine::Object3d& arrow, const std::string& path, std::ostream& log) {
    DestinationGuide guide;
    guide.Initialize({});
    for (const auto& target : {engine::Vector3{20,0,0}, {0,0,20}, {-20,0,0}, {0,0,-20}}) {
        guide.Reset(); guide.Update(arrow, {}, target, 1.0f/60);
        const auto rotation = arrow.GetRotation();
        const float distance = std::hypot(target.x,target.z);
        Require(std::sin(rotation.y)*target.x/distance + std::cos(rotation.y)*target.z/distance > .999f,
                "arrow tip does not point toward cardinal target");
        Require(arrow.GetIsVisible() && arrow.GetTranslate().y > .7f, "guide missing or not floating");
    }
    guide.Reset(); guide.Update(arrow, {}, {0,0,20}, 1.0f/60);
    const auto frozen = arrow.GetTranslate();
    guide.Update(arrow, {10,0,0}, {0,0,20}, 0);
    Require(Near(arrow.GetTranslate().x,frozen.x) && Near(arrow.GetTranslate().y,frozen.y), "pause advanced guide");
    guide.Update(arrow, {1,0,0}, {0,0,20}, 1.0f/60);
    Require(arrow.GetTranslate().x > 0 && arrow.GetTranslate().x < 1, "position did not interpolate");
    guide.Reset(); guide.Update(arrow, {}, {2,0,0}, 1.0f/60);
    Require(guide.HasArrived() && !arrow.GetIsVisible(), "arrival did not hide guide");
    guide.Update(arrow, {-20,0,0}, {2,0,0}, 1.0f/60);
    Require(!arrow.GetIsVisible(), "arrived guide reappeared after leaving goal");
    guide.Hide(arrow); guide.Update(arrow, {}, {20,0,0}, 1.0f/60);
    Require(!arrow.GetIsVisible(), "temporary hide cleared arrival latch");
    guide.Reset(); guide.Update(arrow, {}, {20,0,0}, 1.0f/60);
    Require(arrow.GetIsVisible(), "new guidance did not reset arrival");
    guide.Reset(); guide.Update(arrow, {}, {1,10,0}, 1.0f/60);
    Require(!guide.HasArrived(), "different floor counted as arrived");
    guide.Update(arrow, {std::numeric_limits<float>::quiet_NaN(),0,0}, {20,0,0}, 1.0f/60);
    Require(!arrow.GetIsVisible(), "invalid coordinates left guide visible");
    auto settings = guide.GetSettings(); settings.height = 1.25f; settings.scale = .75f;
    guide.SetSettings(settings);
    Require(guide.SaveSettings(path+".settings.json"), "guide settings save failed");
    DestinationGuide loaded; loaded.Initialize({});
    Require(loaded.LoadSettings(path+".settings.json") && Near(loaded.GetSettings().height,1.25f) && Near(loaded.GetSettings().scale,.75f),
            "guide settings round trip failed");
    { std::ofstream invalid(path+".invalid.json"); invalid << "{\"height\":\"bad\"}"; }
    Require(!loaded.LoadSettings(path+".invalid.json") && Near(loaded.GetSettings().height,1.25f), "invalid settings replaced current values");
    log << "OK cardinal directions, interpolation, pause, arrival latch, reset, height and settings" << std::endl;
}
inline void AfterDraw(SceneManager* manager, const std::string& path) {
    static std::ofstream log(path,std::ios::trunc);
    static int phase=0, frames=0;
    static bool done=false;
    static ULONGLONG start=GetTickCount64();
    auto finish=[&](const std::string& message) { log << message << std::endl; done=true; play=false; WinApp::CloseNow(); };
    if (done) return;
    try {
        Require(GetTickCount64()-start<240000,"tutorial guide loading timeout");
        if (manager->IsTransitioning()) return;
        if (phase==0) { manager->ChangeScene("TUTORIAL"); phase=1; return; }
        auto* scene=dynamic_cast<TutorialScene*>(manager->GetCurrentScene());
        if (!scene || !scene->GetPlayer()) return;
        auto* player=scene->GetPlayer();
        auto* arrow=Find(*scene,"Tutorial_ExitGuideArrow");
        auto* gate=dynamic_cast<GimmickStageGate*>(Find(*scene,"Gimmick_StageGate"));
        auto* director=Access::Director(*scene);
        Require(arrow && gate && director,"guide, exit gate or director missing");
        constexpr float dt=1.0f/60;
        auto prepare=[&] {
            Require(director->JumpToStep("reach_exit_gate"),"final tutorial step missing");
            gate->SetGateState(false,true,false); gate->SetTransitionEnabled(true);
            auto pos=gate->GetWorldPosition(); pos.x-=11; pos.y=-.54f;
            player->SetTranslate(pos); player->SetVelocity({}); player->SetIsControlActive(false); player->SetIsVisible(true);
            player->SetParameter(&engine::EntityParameter::gravity, 0); player->SetCollisionAttribute(0); player->SetCollisionMask(0); player->UpdateWorldMatrix(true);
            Access::Update(*scene,dt);
        };
        if (phase==1) {
            if (++frames<3) return;
            Require(!arrow->GetIsVisible(),"guide shown before final tutorial step");
            Require(arrow->GetCollisionAttribute()==0 && arrow->GetCollisionMask()==0 && arrow->IsEditorInternal(),
                    "guide participates in collisions or scene serialization");
            GenericCases(*arrow,path,log); Access::Guide(*scene).Reset();
            Require(director->JumpToStep("move"),"move step missing");
            Access::Update(*scene,dt); Require(!arrow->GetIsVisible(),"early tutorial step shows exit guide");
            Require(director->JumpToStep("gate_unlock_cinematic"),"unlock cinematic missing");
            Access::Update(*scene,dt); Require(!arrow->GetIsVisible(),"cinematic shows exit guide");
            prepare(); Require(arrow->GetIsVisible(),"final tutorial step did not show guide");
            auto* camera=CameraManager::GetInstance()->GetActiveCamera();
            const auto p=player->GetWorldPosition();
            CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
            camera->SetInputEnabled(false); camera->SetFollowTarget(nullptr);
            camera->SetEye(p+engine::Vector3{-7,9,-12}); camera->SetTarget(p+engine::Vector3{4,1,0}); camera->Update(dt);
            log << "OK actual final-step activation; gate at " << gate->GetWorldPosition().x << ',' << gate->GetWorldPosition().y << ',' << gate->GetWorldPosition().z << std::endl;
            frames=0; phase=2; play=true; return;
        }
        if (phase==2) {
            if (++frames<12 || !Fade::GetInstance()->IsFinished()) return;
            Require(arrow->GetIsVisible(),"guide disappeared during final-step play");
            auto toGate=gate->GetWorldPosition()-arrow->GetWorldPosition();
            const float length=std::hypot(toGate.x,toGate.z);
            Require(std::sin(arrow->GetRotation().y)*toGate.x/length+std::cos(arrow->GetRotation().y)*toGate.z/length>.99f,
                    "live guide does not face the gate");
            CaptureView(path+".visible.png");
            const auto originalGatePosition=gate->GetTranslate();
            const auto oldDestination=gate->GetWorldPosition();
            const auto desiredGate=oldDestination+engine::Vector3{20,0,0};
            const auto localGate=gate->GetParent()
                ? engine::Math::Transform(desiredGate,engine::Math::Inverse(gate->GetParent()->GetWorldMatrix())) : desiredGate;
            gate->SetTranslate(localGate); gate->UpdateWorldMatrix(true);
            Require(Near(gate->GetWorldPosition().x,desiredGate.x),"test fixture failed to move parented gate in world space");
            player->SetTranslate({oldDestination.x,-.54f,oldDestination.z}); player->UpdateWorldMatrix(true);
            director->Update(.1f);
            Require(director->GetDirectionGuideTarget()==gate,"arrival used stale JSON position after gate moved");
            Access::Update(*scene,dt); Require(arrow->GetIsVisible(),"edited gate lost its guide target");
            gate->SetTranslate(originalGatePosition); gate->UpdateWorldMatrix(true);
            Require(director->JumpToStep("move"),"move reset after edited gate failed"); Access::Update(*scene,dt); prepare();
            log << "OK arrival follows the actual edited gate instead of old JSON coordinates" << std::endl;
            const auto previous=arrow->GetWorldPosition();
            player->SetTranslate(player->GetTranslate()+engine::Vector3{0,0,2}); player->UpdateWorldMatrix(true);
            Access::Update(*scene,dt);
            Require(std::hypot(arrow->GetWorldPosition().x-previous.x,arrow->GetWorldPosition().z-previous.z)>0,
                    "live guide failed to follow moved player");
            Access::Update(*scene,0); Require(!arrow->GetIsVisible(),"editor pause did not hide guide");
            Access::Update(*scene,dt); Require(arrow->GetIsVisible(),"guide did not resume");
            const float hp=player->GetParameters()->hp; player->SetParameter(&engine::EntityParameter::hp, 0);
            Access::Update(*scene,dt); Require(!arrow->GetIsVisible(),"death did not hide guide"); player->SetParameter(&engine::EntityParameter::hp, hp);
            prepare(); auto arrival=gate->GetWorldPosition(); arrival.x-=1; arrival.y=-.54f;
            player->SetTranslate(arrival); player->UpdateWorldMatrix(true); Access::Update(*scene,dt);
            Require(!arrow->GetIsVisible() && Access::Guide(*scene).HasArrived(),"actual gate arrival did not latch hidden");
            player->SetTranslate(arrival+engine::Vector3{-12,0,0}); player->UpdateWorldMatrix(true); Access::Update(*scene,dt);
            Require(!arrow->GetIsVisible(),"actual guide reappeared after arrival");
            Require(director->JumpToStep("move"),"restart failed"); Access::Update(*scene,dt); prepare();
            Require(arrow->GetIsVisible(),"new tutorial attempt retained arrived state");
            log << "OK actual follow, orientation, pause, death, automatic arrival and restart" << std::endl;
            // 最後に実際のゲート入場を開始し、早期return経路でも矢印が残らないことを確認する。
            manager->SetIsPlaying(true); player->SetCollisionAttribute(CollisionCategories::Get("player"));
            Require(gate->OnCollision(player),"gate entry did not start");
            scene->Update(dt); Require(!arrow->GetIsVisible(),"gate entry presentation left guide visible");
            finish("PASS: tutorial exit guide visibility, follow, gate direction, arrival latch, pause, death, restart, settings and gate entry");
        }
    } catch(const std::exception& error) { finish(std::string("FAIL: ")+error.what()); }
}
}
#endif