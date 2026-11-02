#pragma once
#ifdef USE_IMGUI
#include "SceneManager.h"
#include "Player.h"
#include "GimmickStageGate.h"
#include "game/system/scene/SceneGameplayAccess.h"
#include "game/system/progress/StageManager.h"
#include "CameraManager.h"
#include "PostEffect.h"
#include "DirectXCommon.h"
#include "DirectXTex.h"
#include "WinApp.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

// 専用worktreeでの明示起動だけが、実際のGPU描画とゲート遷移を検証します。
namespace GateEntryRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_GATE_ENTRY_TEST",nullptr,0)>0; }
inline int phase = 0;
inline bool WantsPlay() { return phase != 3; }
inline void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
inline bool Near(float a,float b) { return std::abs(a-b)<0.001f; }
inline GimmickStageGate* FindGate(BaseScene* scene,bool node = false) {
    for (const auto& object : scene->GetObjects()) {
        auto* gate = dynamic_cast<GimmickStageGate*>(object.get());
        if (gate && gate->IsStageSelectNodeMode()==node && (!node || gate->GetStageIndex()==0)) return gate;
    }
    return nullptr;
}
inline void Capture(const std::string& path) {
    auto* dx = DirectXCommon::GetInstance();
    auto* texture = PostEffect::GetInstance()->GetRenderTexture();
    DirectX::ScratchImage image, converted;
    Require(SUCCEEDED(DirectX::CaptureTexture(dx->GetCommandQueue(),texture,false,image,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)),"GPU capture failed");
    Require(SUCCEEDED(DirectX::Convert(*image.GetImage(0,0,0),DXGI_FORMAT_R8G8B8A8_UNORM,
        DirectX::TEX_FILTER_DEFAULT,DirectX::TEX_THRESHOLD_DEFAULT,converted)),"GPU image conversion failed");
    Require(SUCCEEDED(DirectX::SaveToWICFile(*converted.GetImage(0,0,0),DirectX::WIC_FLAGS_NONE,
        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG),std::filesystem::path(path).c_str())),"PNG export failed");
}
inline void Start(GimmickStageGate* gate,Player* player) {
    Require(gate && player,"missing gate or player");
    gate->SetTransitionEnabled(true);
    auto pos=gate->GetWorldPosition();pos.z-=2.0f;
    player->SetTranslate(pos);
    player->SetVelocity({0,0,0});
    player->UpdateWorldMatrix(true);
    Require(gate->OnCollision(player),"gate did not accept player collision");
    Require(gate->IsEntryPresentationActive(),"gate bypassed entry presentation");
    Require(!SceneManager::GetInstance()->IsTransitioning(),"transition started before dissolve");
    Require(!gate->OnCollision(player),"duplicate collision was accepted");
}
inline void CheckDissolve(Player* player) {
    Require(player->IsCinematicLocked() && !player->IsControlActive(),"player control or damage lock missing");
    Require(player->GetCollisionAttribute()==0 && player->GetCollisionMask()==0,"entry collision remained active");
    auto* material=player->GetMaterialData();
    Require(material && material->portalClipEnabled>0.5f && material->portalClipProgress>0.14f,
            "player dissolve did not progress");
    for (auto* child:player->GetChildren()) {
        if (auto* m=child->GetMaterialData()) Require(m->portalClipEnabled>0.5f,"child model did not dissolve");
    }
}
inline void AfterDraw(SceneManager* manager,const std::string& path) {
    static std::ofstream log(path,std::ios::trunc);
    static ULONGLONG started=GetTickCount64();
    static bool done=false,capturedEarly=false,capturedLate=false,sawSelectDissolve=false,sawGameplayDissolve=false;
    static int frames=0;
    static uint32_t attribute=0,mask=0;
    static float emissive=0;
    static BlendMode blend=BlendMode::kNormal;
    static engine::Vector3 scale{};
    auto finish=[&](const std::string& message) { log<<message<<std::endl;done=true;WinApp::CloseNow(); };
    if (done) return;
    try {
        Require(GetTickCount64()-started<600000,"gate runtime timeout");
        if (manager->IsTransitioning()) return;
        auto* scene=manager->GetCurrentScene();
        auto* player=GetScenePlayer(scene);
        const auto& name=manager->GetCurrentSceneName();
        if (phase==0) {
            manager->ChangeScene("TUTORIAL");phase=1;log<<"REQUEST tutorial"<<std::endl;return;
        }
        if (phase==1 && name=="TUTORIAL" && player) {
            auto* gate=FindGate(scene);
            Require(gate && gate->GetParameters(),"tutorial gate parameter missing");
            const auto destination=gate->GetParameters()->targetScene;
            gate->SetParameter(&engine::EntityParameter::targetScene, "__MissingGateProbeScene");
            gate->SetTransitionEnabled(true);
            Require(!gate->OnCollision(player),"invalid destination started a presentation");
            gate->SetParameter(&engine::EntityParameter::targetScene, destination);
            attribute=player->GetCollisionAttribute();mask=player->GetCollisionMask();
            emissive=player->GetEmissive();blend=player->GetBlendMode();scale=player->GetScale();
            Start(gate,player);phase=2;log<<"START tutorial entry"<<std::endl;return;
        }
        if (phase==2) {
            auto* gate=FindGate(scene);
            Require(gate && gate->IsEntryPresentationActive(),"tutorial entry disappeared");
            if (gate->GetEntryPresentationProgress()>0.22f && !capturedEarly) {
                CheckDissolve(player);Capture(path+".tutorial-early.png");capturedEarly=true;
                log<<"DRAW tutorial partial dissolve"<<std::endl;
            }
            if (gate->GetEntryPresentationProgress()>0.43f) {
                CheckDissolve(player);Capture(path+".tutorial-late.png");capturedLate=true;phase=3;
            }
            return;
        }
        if (phase==3) {
            auto* gate=FindGate(scene);
            Require(gate && !gate->IsEntryPresentationActive(),"stop did not cancel presentation");
            Require(!player->IsCinematicLocked(),"stop retained cinematic lock");
            Require(player->GetCollisionAttribute()==attribute && player->GetCollisionMask()==mask,"collision restoration failed");
            Require(player->GetBlendMode()==blend && Near(player->GetEmissive(),emissive),"material restoration failed");
            Require(Near(player->GetScale().x,scale.x),"scale restoration failed");
            Require(player->GetMaterialData()->portalClipEnabled<0.5f,"stop retained dissolve");
            log<<"PASS stop restored material and collision"<<std::endl;
            auto away=gate->GetWorldPosition();away.z-=20.0f;
            player->SetTranslate(away);player->UpdateWorldMatrix(true);
            phase=4;return;
        }
        if (phase==4) { Start(FindGate(scene),player);phase=5;return; }
        if (phase==5 && name=="SELECT") {
            Require(capturedEarly && capturedLate,"tutorial dissolve was not rendered");
            auto* gate=FindGate(scene,true);Require(gate,"select gate 0 missing");
            Require(!gate->OnCollision(player),"display node triggered an immediate transition");
            player->SetTranslate(gate->GetWorldPosition());player->SetVelocity({0,0,0});
            player->UpdateWorldMatrix(true);phase=6;frames=0;
            log<<"PASS tutorial gate reached SELECT; approach select gate"<<std::endl;return;
        }
        if (phase==6 && name=="SELECT") {
            if (player->IsCinematicLocked() && player->GetMaterialData()->portalClipProgress>0.14f) {
                CheckDissolve(player);sawSelectDissolve=true;
                if (++frames==12) Capture(path+".select.png");
            }
            return;
        }
        if (phase==6 && name=="GAMEPLAY") {
            Require(sawSelectDissolve,"select did not use shared dissolve");
            phase=7;frames=0;log<<"PASS select gate reached GAMEPLAY"<<std::endl;return;
        }
        if (phase==7) Require(name=="GAMEPLAY","gameplay returned before manual gate contact");
        if (phase==7 && name=="GAMEPLAY") {
            if (++frames<200 || player->IsGateReturnAnimationActive() || !player->IsControlActive()) return;
            Start(FindGate(scene),player);phase=8;return;
        }
        if (phase==8 && name=="GAMEPLAY") {
            auto* gate=FindGate(scene);
            if (gate && gate->GetEntryPresentationProgress()>0.22f) {CheckDissolve(player);sawGameplayDissolve=true;}
            return;
        }
        if (phase==8 && name=="SELECT") {
            Require(sawGameplayDissolve,"gameplay return gate did not dissolve");
            finish("PASS: tutorial, select and gameplay gates rendered dissolve; cancellation restored state");
        }
    } catch (const std::exception& error) {finish(std::string("FAIL: ")+error.what());}
}
}
#endif
