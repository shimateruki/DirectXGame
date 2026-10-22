#include "engine/utility/SystemDebug/editor/cinematic/GhostDirector.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>

void GhostDirector::DrawPresentationEditor() {
#ifdef USE_IMGUI
    auto& p=sequence_.presentation;
    if (!ImGui::CollapsingHeader("演出部品・見た目・カメラ", p.objects.empty()?0:ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextWrapped("演出専用の部品は再生器が生成・破棄します。停止・姿勢復元で既存Objectの変更も戻します。");
    if (player_.IsPlaying()) ImGui::TextDisabled("キーや部品を編集するときは一時停止してください。");
    ImGui::BeginDisabled(player_.IsPlaying());
    auto text=[](const char* label,std::string& value) {
        char buffer[512]{}; strncpy_s(buffer,sizeof(buffer),value.c_str(),_TRUNCATE);
        if (ImGui::InputText(label,buffer,sizeof(buffer))) value=buffer;
    };
    auto binding=[&](const char* label,std::string& value) {
        CinematicObjectBinding b{value,-1};
        if (DrawObjectBindingEditor(label,b,false,false)) value=b.targetName;
        text((std::string(label)+" 名・役割").c_str(),value);
    };
    binding("視点の基準",p.reference); binding("視点が向く対象",p.lookToward);
    if (ImGui::TreeNode("再生中に隠すObject")) {
        for (size_t i=0;i<p.hideObjects.size();++i) {
            ImGui::PushID(static_cast<int>(i)); text("対象名・役割",p.hideObjects[i]); ImGui::PopID();
        }
        if (ImGui::Button("非表示対象を追加")) p.hideObjects.push_back("$Actor");
        ImGui::SameLine(); if (!p.hideObjects.empty() && ImGui::Button("末尾の非表示対象を削除")) p.hideObjects.pop_back();
        ImGui::TreePop();
    }
    if (ImGui::Button("演出部品を追加")) {
        CinematicObjectClip clip; clip.name="part_"+std::to_string(p.objects.size());
        clip.source="$Actor"; clip.model="Primitives/cube"; clip.keys.push_back({});
        p.objects.push_back(std::move(clip)); selectedVisualObject_=static_cast<int>(p.objects.size())-1;
        selectedVisualKey_=0; RefreshPlayer(true);
    }
    if (!p.objects.empty()) {
        selectedVisualObject_=std::clamp(selectedVisualObject_,0,static_cast<int>(p.objects.size())-1);
        if (ImGui::BeginCombo("編集する部品",p.objects[selectedVisualObject_].name.c_str())) {
            for (int i=0;i<static_cast<int>(p.objects.size());++i)
                if (ImGui::Selectable(p.objects[i].name.c_str(),selectedVisualObject_==i)) {
                    selectedVisualObject_=i; selectedVisualKey_=0;
                }
            ImGui::EndCombo();
        }
        auto& clip=p.objects[selectedVisualObject_];
        text("部品名",clip.name); text("モデル（Resources/3DModel以下）",clip.model);
        binding("部品の基準対象",clip.source);
        ImGui::Checkbox("一時Objectを生成",&clip.spawn); ImGui::SameLine(); ImGui::Checkbox("部品を有効化",&clip.enabled);
        ImGui::Checkbox("キーで姿勢を動かす",&clip.animate);
        ImGui::Checkbox("基準の現在位置に追従",&clip.followPosition); ImGui::SameLine(); ImGui::Checkbox("現在の回転に追従",&clip.followRotation);
        ImGui::Checkbox("現在の大きさに追従",&clip.followScale); ImGui::SameLine(); ImGui::Checkbox("基準が隠れたら隠す",&clip.inheritVisibility);
        ImGui::DragFloat3("継承する回転軸（0で除外）",&clip.rotationMask.x,.1f,0,1);
        ImGui::Checkbox("加算描画",&clip.additive); ImGui::SameLine(); ImGui::Checkbox("ライティング",&clip.lit);
        ImGui::Checkbox("基準の大きさを継承",&clip.inheritScale); ImGui::SameLine(); ImGui::Checkbox("基準の回転を継承",&clip.inheritRotation);
        const char* spaces[]={"world","model","radial"};
        int mode=clip.positionSpace=="model"?1:clip.positionSpace=="radial"?2:0;
        if (ImGui::Combo("位置の座標系",&mode,spaces,3)) clip.positionSpace=spaces[mode];
        ImGui::DragFloat3("モデル基準の支点",&clip.pivot.x,.01f);
        ImGui::DragFloat("接地高さ（-100000で無制限）",&clip.floorHeight,.02f);
        ImGui::Checkbox("消失面の高さに基準スケールを適用",&clip.clipScaleHeight);
        ImGui::DragFloat("消失面の原点補正",&clip.clipCenterOffset,.01f);
        ImGui::DragFloat("消失面の縁幅",&clip.clipEdgeWidth,.005f,0,3);
        ImGui::DragFloat("消失面のぼかし",&clip.clipDissolve,.005f,0,3);
        ImGui::ColorEdit4("消失面の色",&clip.clipColor.x);
        if (!clip.keys.empty()) {
            selectedVisualKey_=std::clamp(selectedVisualKey_,0,static_cast<int>(clip.keys.size())-1);
            if (ImGui::Button("現在時刻に近いキーを選択")) {
                auto it=std::lower_bound(clip.keys.begin(),clip.keys.end(),currentScrubTime_,[](const auto& k,float t){return k.time<t;});
                selectedVisualKey_=std::min(static_cast<int>(it-clip.keys.begin()),static_cast<int>(clip.keys.size())-1);
            }
            ImGui::SliderInt("見た目キー",&selectedVisualKey_,0,static_cast<int>(clip.keys.size())-1);
            auto& k=clip.keys[selectedVisualKey_];
            ImGui::DragFloat("キー時刻",&k.time,.005f,0,sequence_.GetAuthoredDuration());
            ImGui::DragFloat3("位置・半径",&k.position.x,.01f); ImGui::DragFloat3("回転（rad）",&k.rotation.x,.01f);
            ImGui::DragFloat3("大きさ",&k.scale.x,.01f); ImGui::ColorEdit4("色・透明度",&k.color.x);
            ImGui::DragFloat("発光",&k.emissive,.02f,0,100); ImGui::DragFloat("消失面の高さ",&k.clipHeight,.01f);
            ImGui::DragFloat("接地後の跳ね上がり",&k.bounce,.01f,0,10);
            ImGui::Checkbox("表示",&k.visible); ImGui::SameLine(); ImGui::Checkbox("消失面を使う",&k.clipEnabled);
            if (ImGui::Button("選択キーを現在時刻へ複製")) { auto copy=k; copy.time=currentScrubTime_; clip.keys.push_back(copy); }
            ImGui::SameLine();
            if (clip.keys.size()>1 && ImGui::Button("選択キーを削除")) clip.keys.erase(clip.keys.begin()+selectedVisualKey_);
        }
    }
    if (ImGui::TreeNode("演出カメラと黒帯")) {
        ImGui::Checkbox("カメラ補正をワールド座標で指定",&p.viewWorldSpace);
        if (ImGui::Button("カメラキーを追加")) {
            auto key=p.view.empty()?CinematicViewKey{}:p.view[std::clamp(selectedViewKey_,0,static_cast<int>(p.view.size())-1)];
            key.time=currentScrubTime_; p.view.push_back(key); selectedViewKey_=static_cast<int>(p.view.size())-1;
        }
        if (!p.view.empty()) {
            selectedViewKey_=std::clamp(selectedViewKey_,0,static_cast<int>(p.view.size())-1);
            if (ImGui::Button("現在時刻に近いカメラキー")) {
                auto it=std::lower_bound(p.view.begin(),p.view.end(),currentScrubTime_,[](const auto& k,float t){return k.time<t;});
                selectedViewKey_=std::min(static_cast<int>(it-p.view.begin()),static_cast<int>(p.view.size())-1);
            }
            ImGui::SliderInt("カメラキー",&selectedViewKey_,0,static_cast<int>(p.view.size())-1);
            auto& k=p.view[selectedViewKey_];
            binding("このカットの注視元（空なら開始位置）",k.reference);
            binding("このカットの注視先（空なら開始位置）",k.lookToward);
            binding("視点位置の基準（空なら注視点）",k.eyeReference);
            ImGui::DragFloat("カメラキー時刻",&k.time,.005f,0,sequence_.GetAuthoredDuration());
            ImGui::SliderFloat("2対象間の注視割合",&k.focusMix,0,1);
            ImGui::DragFloat3("注視位置の補正",&k.focusOffset.x,.02f);
            ImGui::DragFloat3("基準スケールで伸びる注視補正",&k.scaledFocusOffset.x,.02f);
            ImGui::DragFloat3("注視点からの視点距離",&k.eyeOffset.x,.02f);
            ImGui::DragFloat3("視点の揺れ",&k.jitter.x,.005f);
            ImGui::SliderFloat("視野角（rad）",&k.fov,.1f,2.0f); ImGui::SliderFloat("黒帯の高さ",&k.bars,0,.3f);
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("持続粒子")) {
        if (ImGui::Button("粒子クリップを追加")) p.particles.push_back({});
        for (size_t i=0;i<p.particles.size();++i) {
            ImGui::PushID(static_cast<int>(i)); auto& clip=p.particles[i];
            if (ImGui::TreeNode("粒子", "%s",clip.preset.empty()?"未設定":clip.preset.c_str())) {
                text("発生点の部品名",clip.target); text("GPU粒子プリセット",clip.preset);
                ImGui::DragFloat("開始",&clip.startTime,.01f,0,sequence_.duration);
                ImGui::DragFloat("終了",&clip.endTime,.01f,clip.startTime,sequence_.duration);
                ImGui::DragFloat("発生間隔",&clip.interval,.01f,.01f,10);
                ImGui::DragFloat("発生量倍率",&clip.scale,.01f,0,10); ImGui::DragFloat3("発生方向",&clip.direction.x,.01f);
                bool radial=clip.directionMode=="radialIn";
                if (ImGui::Checkbox("基準対象へ向ける",&radial)) clip.directionMode=radial?"radialIn":"fixed";
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    ImGui::TreePop();
    }
    if (ImGui::Button("変更をプレビューへ反映（キーを時刻順に整列）")) { sequence_.Sort(); RefreshPlayer(true); }
    ImGui::TextWrapped("部品名・モデル・基準対象の変更後は反映してください。保存には上の演出シーケンス保存を使います。");
    ImGui::EndDisabled();
#endif
}
