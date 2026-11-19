#include "engine/editor/cinematic/GhostDirector.h"
#include "imgui.h"
#include <algorithm>
#include <cstring>

void GhostDirector::DrawPresentationEditor() {
#ifdef USE_IMGUI
    auto& p=sequence_.presentation;
    if (!ImGui::CollapsingHeader("演出部品・見た目・カメラ", p.objects.empty()?0:ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextWrapped("演出専用の部品は再生器が生成・破棄します。停止・姿勢復元で既存Objectの変更も戻します。");
    if (player_.IsPlaying()) ImGui::TextDisabled("キーや部品を編集するときは一時停止してください。");
    // 再生器が参照するキーや部品を変更する間は、再生を止めた状態に限定する。
    ImGui::BeginDisabled(player_.IsPlaying());
    auto text=[](const char* label,std::string& value) {
        char buffer[512]{}; strncpy_s(buffer,sizeof(buffer),value.c_str(),_TRUNCATE);
        if (ImGui::InputText(label,buffer,sizeof(buffer))) value=buffer;
    };
    // 部品の参照は名前・役割を保存する形式なので、選択欄と直接入力を同じ文字列へ反映する。
    auto binding=[&](const char* label,std::string& value) {
        CinematicObjectBinding b{value,-1};
        if (DrawObjectBindingEditor(label,b,false,false)) value=b.targetName;
        text((std::string(label)+" 名・役割").c_str(),value);
    };
    // 付着先に追従したあとの位置・角度の揺れを調整する。初速と減衰を別々に指定する。
    if (ImGui::TreeNode("追従部品のばね（ゲーム側の装着先を使用）")) {
        ImGui::TextWrapped("装着先のモデル寸法はゲーム側から渡します。再生を停止して調整し、シーケンスを保存してください。");
        for (auto& [name, settings] : p.springs) {
            ImGui::PushID(name.c_str());
            if (ImGui::TreeNode(name.c_str())) {
                ImGui::DragFloat3("初期位置の差分", &settings.initialPositionOffset.x, 0.01f);
                ImGui::DragFloat3("初期回転の差分（rad）", &settings.initialRotationOffset.x, 0.01f);
                ImGui::SliderFloat("初期の前後速度", &settings.initialForwardVelocity, -5.0f, 5.0f);
                ImGui::SliderFloat("位置ばねの硬さ", &settings.positionStiffness, 0.0f, 200.0f);
                ImGui::SliderFloat("位置ばねの減衰", &settings.positionDamping, 0.0f, 30.0f);
                ImGui::SliderFloat("回転ばねの硬さ", &settings.angularStiffness, 0.0f, 200.0f);
                ImGui::SliderFloat("回転ばねの減衰", &settings.angularDamping, 0.0f, 30.0f);
                ImGui::SliderFloat("水平偏差の上限", &settings.maxPlanarOffset, 0.0f, 30.0f);
                ImGui::SliderFloat("沈みの上限", &settings.maxSink, 0.0f, 30.0f);
                ImGui::SliderFloat("浮きの上限", &settings.maxLift, 0.0f, 30.0f);
                ImGui::SliderFloat("偏差による傾き", &settings.tiltFromOffset, 0.0f, 30.0f);
                ImGui::SliderFloat("速度による傾き", &settings.tiltFromVelocity, 0.0f, 30.0f);
                ImGui::SliderFloat("基準対象の傾き追従", &settings.sourceTiltFollow, 0.0f, 1.0f);
                ImGui::SliderFloat("初期の上下速度", &settings.initialVerticalVelocity, -5.0f, 30.0f);
                ImGui::SliderFloat("初期のロール速度", &settings.initialRollVelocity, -5.0f, 30.0f);
                ImGui::SliderFloat("圧縮倍率", &settings.compressionScale, 0.0f, 1.0f);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    binding("視点の基準",p.reference); binding("視点が向く対象",p.lookToward);
    if (ImGui::TreeNode("再生中に隠すObject")) {
        for (size_t i=0;i<p.hideObjects.size();++i) {
            ImGui::PushID(static_cast<int>(i)); text("対象名・役割",p.hideObjects[i]); ImGui::PopID();
        }
        if (ImGui::Button("非表示対象を追加")) p.hideObjects.push_back("$Actor");
        ImGui::SameLine(); if (!p.hideObjects.empty() && ImGui::Button("末尾の非表示対象を削除")) p.hideObjects.pop_back();
        ImGui::TreePop();
    }
    // 空の形状では調整を始められないため、既定モデル・基準・初期キーを揃えた部品を追加する。
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
        ImGui::Checkbox("自動再生で姿勢・見た目を適用",&clip.animate);
        if (!clip.animate) ImGui::TextDisabled("ゲームからの姿勢適用と位置参照には、OFFのまま使用できます。");
        ImGui::Checkbox("基準の現在位置に追従",&clip.followPosition); ImGui::SameLine(); ImGui::Checkbox("現在の回転に追従",&clip.followRotation);
        ImGui::Checkbox("現在の大きさに追従",&clip.followScale); ImGui::SameLine(); ImGui::Checkbox("基準が隠れたら隠す",&clip.inheritVisibility);
        ImGui::DragFloat3("継承する回転軸（0で除外）",&clip.rotationMask.x,.1f,0,1);
        ImGui::Checkbox("加算描画",&clip.additive); ImGui::SameLine(); ImGui::Checkbox("ライティング",&clip.lit);
        ImGui::Checkbox("基準の大きさを継承",&clip.inheritScale); ImGui::SameLine(); ImGui::Checkbox("基準の回転を継承",&clip.inheritRotation);
        // 同じXYZでも選んだ座標系で位置の解釈が変わるため、座標値とは別に方式を保存する。
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
        // 既存キーの先頭と末尾を区間へ移し、両形式が同時に残らないようキーを空にする。
        if (clip.motions.empty() && ImGui::Button("キーを置き換えて移動区間を作成")) {
            engine::CinematicMotionSegment motion;
            motion.name="移動";
            if (!clip.keys.empty()) {
                motion.from=clip.keys.front(); motion.to=clip.keys.back();
                motion.startTime=clip.keys.front().time;
                motion.endTime=(std::max)(motion.startTime+.001f,clip.keys.back().time);
            }
            clip.motions.push_back(motion); clip.keys.clear(); selectedVisualKey_=0;
        }
        if (!clip.motions.empty()) {
            ImGui::SeparatorText("移動区間（細かなキーの生成は不要）");
            selectedVisualKey_=std::clamp(selectedVisualKey_,0,static_cast<int>(clip.motions.size())-1);
            if (ImGui::BeginCombo("区間",clip.motions[selectedVisualKey_].name.c_str())) {
                for (int i=0;i<static_cast<int>(clip.motions.size());++i) {
                    ImGui::PushID(i);
                    if (ImGui::Selectable(clip.motions[i].name.c_str(),i==selectedVisualKey_)) selectedVisualKey_=i;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            auto& motion=clip.motions[selectedVisualKey_];
            text("区間名",motion.name);
            // 固定秒数か Signal からの時間差を選ぶ。開始・終了は同じ入力欄で扱う。
            auto timing=[&](const char* label,std::string& signal,float& seconds,float& offset) {
                ImGui::PushID(label);
                if (ImGui::BeginCombo(label,signal.empty()?"秒数で指定":signal.c_str())) {
                    if (ImGui::Selectable("秒数で指定",signal.empty())) signal.clear();
                    for (const auto& marker:sequence_.signals)
                        if (marker.enabled && ImGui::Selectable(marker.name.c_str(),signal==marker.name)) signal=marker.name;
                    ImGui::EndCombo();
                }
                if (signal.empty()) ImGui::DragFloat("秒",&seconds,.01f,0,600);
                else {
                    ImGui::DragFloat("Signalからの時間差（秒）",&offset,.01f,-600,600);
                    ImGui::Text("開始/終了: %.3f 秒",sequence_.GetSignalTime(signal,-1)+offset);
                }
                ImGui::PopID();
            };
            timing("開始",motion.startSignal,motion.startTime,motion.startSignalOffset);
            timing("終了",motion.endSignal,motion.endTime,motion.endSignalOffset);
            // 区間全体の効果に先立って、補間に使う両端の位置と見た目を個別に編集する。
            auto point=[&](const char* label,engine::CinematicVisualKey& key) {
                if (ImGui::TreeNode(label)) {
                    binding("基準点",key.positionReference);
                    ImGui::DragFloat3("位置の補正",&key.position.x,.01f);
                    ImGui::DragFloat3("回転（rad）",&key.rotation.x,.01f);
                    ImGui::DragFloat3("大きさ（倍率）",&key.scale.x,.01f,0,100);
                    ImGui::DragFloat("発光",&key.emissive,.01f,0,100);
                    ImGui::ColorEdit4("色",&key.color.x); ImGui::Checkbox("表示",&key.visible);
                    ImGui::TreePop();
                }
            };
            point("始点",motion.from); point("終点",motion.to);
            ImGui::DragFloat("弧の高さ",&motion.arcHeight,.01f);
            ImGui::Checkbox("始終点を滑らかにする",&motion.smoothProgress);
            if (ImGui::TreeNode("回転・拡縮")) {
                ImGui::DragFloat3("中央で加える傾き（rad）",&motion.rotationArc.x,.01f);
                ImGui::DragFloat("傾きの周期数",&motion.rotationCycles,.05f,0,100);
                ImGui::Checkbox("拡縮を滑らかにする",&motion.smoothScale);
                ImGui::DragFloat("拡縮の減衰率（0で通常補間）",&motion.scaleDecay,.1f,0,100);
                ImGui::DragFloat("拡縮の揺れ周期数",&motion.scaleCycles,.05f,0,100);
                ImGui::DragFloat3("周期的な拡縮の振幅",&motion.scaleWaveAmplitude.x,.001f);
                ImGui::DragFloat("拡縮の角速度（rad/秒）",&motion.scaleWaveFrequency,.1f,0,1000);
                ImGui::Checkbox("区間内で揺れ幅を徐々に増やす",&motion.scaleWaveFadeIn);
                ImGui::SliderFloat("拡縮を始める進行割合",&motion.scaleStart,0,.99f);
                ImGui::DragFloat("最小倍率",&motion.minimumScale,.001f,0,100);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("着地の揺れ・発光")) {
                ImGui::DragFloat("揺れ幅",&motion.bounceHeight,.01f,0,100);
                ImGui::DragFloat("揺れが収まる秒数",&motion.bounceDuration,.01f,.001f,600);
                ImGui::DragFloat("揺れの周期数",&motion.bounceCycles,.1f,0,100);
                ImGui::DragFloat("発光の振幅",&motion.emissiveAmplitude,.01f);
                ImGui::DragFloat("発光の角速度（rad/秒）",&motion.emissiveFrequency,.1f);
                ImGui::TreePop();
            }
            // 直前の終点を新しい始点に引き継ぎ、姿勢と開始時刻を連続させる。
            if (ImGui::Button("末尾に区間を追加")) {
                engine::CinematicMotionSegment next;
                next.name="移動"; next.from=next.to=clip.motions.back().to;
                const auto& last=clip.motions.back();
                next.startSignal=last.endSignal; next.startSignalOffset=last.endSignalOffset;
                next.startTime=last.endSignal.empty()?last.endTime:sequence_.GetSignalTime(last.endSignal,last.endTime)+last.endSignalOffset;
                next.endTime=next.startTime+1; clip.motions.push_back(next);
                selectedVisualKey_=static_cast<int>(clip.motions.size())-1;
            }
            ImGui::SameLine();
            if (clip.motions.size()>1 && ImGui::Button("選択区間を削除")) clip.motions.erase(clip.motions.begin()+selectedVisualKey_);
            ImGui::TextWrapped("時刻の重複・Signal不足は再生前の検証で確認してください。調整後は再評価して保存します。");
        }
        // 時刻指定方式では、シークした時刻以上にある最初のキーを選べるようにする。
        if (!clip.keys.empty()) {
            selectedVisualKey_=std::clamp(selectedVisualKey_,0,static_cast<int>(clip.keys.size())-1);
            if (ImGui::Button("現在時刻に近いキーを選択")) {
                auto it=std::lower_bound(clip.keys.begin(),clip.keys.end(),currentScrubTime_,[](const auto& k,float t){return k.time<t;});
                selectedVisualKey_=std::min(static_cast<int>(it-clip.keys.begin()),static_cast<int>(clip.keys.size())-1);
            }
            ImGui::SliderInt("見た目キー",&selectedVisualKey_,0,static_cast<int>(clip.keys.size())-1);
            auto& k=clip.keys[selectedVisualKey_];
            ImGui::DragFloat("キー時刻",&k.time,.005f,0,sequence_.GetAuthoredDuration());
            binding("位置の基準点（空なら従来の座標系）",k.positionReference);
            // 基準点が指定された場合だけ、別の参照点へ近づける混合係数を編集する。
            if (!k.positionReference.empty()) {
                binding("位置の移動先（空なら基準点のみ）",k.positionTarget);
                ImGui::SliderFloat("基準点から移動先への割合",&k.positionMix,0.0f,1.0f);
                ImGui::TextDisabled("位置は基準点に加えるワールド座標の補正です。");
            }
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
    // カットごとの基準点と補正値は部品とは別に編集し、キー時刻は演出全体の秒数で指定する。
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
            // 固定の注視補正とモデルの倍率に合わせた補正を別の値で調整する。
            ImGui::DragFloat3("注視位置の補正",&k.focusOffset.x,.02f);
            ImGui::DragFloat3("基準スケールで伸びる注視補正",&k.scaledFocusOffset.x,.02f);
            ImGui::DragFloat3("注視点からの視点距離",&k.eyeOffset.x,.02f);
            ImGui::DragFloat3("視点の揺れ",&k.jitter.x,.005f);
            ImGui::SliderFloat("視野角（rad）",&k.fov,.1f,2.0f); ImGui::SliderFloat("黒帯の高さ",&k.bars,0,.3f);
        }
        ImGui::TreePop();
    }
    // 開始から終了の間、部品を起点に指定間隔で発生する粒子を編集する。
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
    // 時刻を書き換えたキーを並べ直し、編集中のシーク位置へプレビューを再構築する。
    if (ImGui::Button("変更をプレビューへ反映（キーを時刻順に整列）")) { sequence_.Sort(); RefreshPlayer(true); }
    ImGui::TextWrapped("部品名・モデル・基準対象の変更後は反映してください。保存には上の演出シーケンス保存を使います。");
    ImGui::EndDisabled();
#endif
}
