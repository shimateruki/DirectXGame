#pragma once
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#ifdef USE_IMGUI
#include "imgui.h"
#endif
// 登場演出専用。既存の戦闘設定とシーン配置から独立して保存します。
struct BossIntroSettings {
    float gateScale=1.12f, auraScale=1.0f, titleWidth=960.0f, titleY=550.0f;
    float closeDuration=1.6f, auraOpacity=1.0f, impactScale=1.0f;
    float idleAuraStrength=.40f, attackAuraStrength=.75f;
    float gateColor[3]={0.30f,0.16f,0.88f};
    static constexpr const char* kConfigPath="Resources/json/enemy_attack/false_king_intro.json";
    // EditorやJSONから入った調整値を、この設定で扱える有限値・範囲へ収める。
    void Sanitize() {
        auto limit=[](float v,float lo,float hi,float fallback){return std::clamp(std::isfinite(v)?v:fallback,lo,hi);};
        gateScale=limit(gateScale,.75f,1.6f,1.12f); auraScale=limit(auraScale,0.0f,2.0f,1.0f);
        closeDuration=limit(closeDuration,.8f,2.0f,1.6f);
        auraOpacity=limit(auraOpacity,0.0f,1.8f,1.0f);impactScale=limit(impactScale,.5f,1.8f,1.0f);
        idleAuraStrength=limit(idleAuraStrength,0.0f,1.0f,.4f);
        attackAuraStrength=limit(attackAuraStrength,0.0f,1.5f,.75f);
        for(auto& c:gateColor)c=limit(c,0.0f,1.0f,.3f);
        titleWidth=limit(titleWidth,600.0f,1120.0f,960.0f); titleY=limit(titleY,450.0f,610.0f,550.0f);
    }
    // 設定JSONを読み込む。成功可否を返し、ゲーム中のActor生成や再生は開始しない。
    void Load() {
        try {std::ifstream in(kConfigPath); if(!in)return;nlohmann::json j;in>>j;
            auto next=*this;
            next.gateScale=j.value("gateScale",gateScale);next.auraScale=j.value("auraScale",auraScale);
            next.titleWidth=j.value("titleWidth",titleWidth);next.titleY=j.value("titleY",titleY);
            next.closeDuration=j.value("closeDuration",closeDuration);
            next.auraOpacity=j.value("auraOpacity",auraOpacity);next.impactScale=j.value("impactScale",impactScale);
            if(j.contains("gateColor") && j["gateColor"].is_array() && j["gateColor"].size()==3)
                for(int i=0;i<3;++i)next.gateColor[i]=j["gateColor"][i].get<float>();
            next.idleAuraStrength=j.value("idleAuraStrength",idleAuraStrength);
            next.attackAuraStrength=j.value("attackAuraStrength",attackAuraStrength);
            next.Sanitize();*this=next;
        }catch(...){}
    }
    // 現在の設定を専用JSONへ保存し、書き込みが成功したかを返す。
    bool Save() const {
        auto v=*this;v.Sanitize();std::error_code e;
        std::filesystem::create_directories(std::filesystem::path(kConfigPath).parent_path(),e);if(e)return false;
        std::ofstream out(kConfigPath);if(!out)return false;
        out<<nlohmann::json{{"gateScale",v.gateScale},{"auraScale",v.auraScale},{"titleWidth",v.titleWidth},{"titleY",v.titleY},{"closeDuration",v.closeDuration},{"auraOpacity",v.auraOpacity},{"impactScale",v.impactScale},{"idleAuraStrength",v.idleAuraStrength},{"attackAuraStrength",v.attackAuraStrength},{"gateColor",{v.gateColor[0],v.gateColor[1],v.gateColor[2]}}}.dump(2);
        return out.good();
    }
    static BossIntroSettings& Get(){static BossIntroSettings s=[] {BossIntroSettings v;v.Load();return v;}();return s;}
#ifdef USE_IMGUI
    void DrawEditor() {
        if(!ImGui::TreeNode("偽王・ゲート登場の演出"))return;
        ImGui::DragFloat("出現ゲート倍率",&gateScale,.01f,.75f,1.6f);
        ImGui::DragFloat("登場オーラ倍率",&auraScale,.01f,0.0f,2.0f);
        ImGui::ColorEdit3("登場ゲートの色",gateColor);
        ImGui::DragFloat("ゲート消失の秒数",&closeDuration,.02f,.8f,2.0f);
        ImGui::DragFloat("オーラの濃さ",&auraOpacity,.01f,0.0f,1.8f);
        ImGui::DragFloat("戦闘・通常時のオーラ",&idleAuraStrength,.01f,0.0f,1.0f);
        ImGui::DragFloat("戦闘・攻撃時のオーラ",&attackAuraStrength,.01f,0.0f,1.5f);
        ImGui::DragFloat("着地衝撃波の倍率",&impactScale,.01f,.5f,1.8f);
        ImGui::DragFloat("名前の横幅",&titleWidth,2.0f,600.0f,1120.0f);
        ImGui::DragFloat("名前の高さ位置",&titleY,1.0f,450.0f,610.0f);
        Sanitize();static int saved=0;
        if(ImGui::Button("登場演出設定を保存"))saved=Save()?1:-1;
        if(saved)ImGui::TextUnformatted(saved>0?"保存しました":"保存に失敗しました");
        ImGui::TextWrapped("動きと名前の表示時刻はstage3_boss_portal_introをTimelineで調整できます。");
        ImGui::TreePop();
    }
#endif
};
