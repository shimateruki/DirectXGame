#pragma once
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>

// 撃破演出の調整値。duration は秒、rupture～victory は全体長に対する 0～1 の進行率です。
// spread・cameraDistance・shake・energy は演出量の倍率で、読み込み時に範囲と順序を補正します。
struct CrownDefeatTuning {
    float duration = 12.0f;
    float rupture = 0.28f;
    float shrink = 0.43f;
    float settle = 0.64f;
    float victory = 0.76f;
    float spread = 1.0f;
    float cameraDistance = 1.0f;
    float shake = 1.0f;
    float energy = 1.0f;
    static constexpr const char* Path = "Resources/json/scenario/crown_defeat_tuning.json";
    // 最初の参照時だけ JSON を読み込みます。以降の外部変更は Load() を明示的に呼んで反映します。
    static CrownDefeatTuning& Get() {
        static CrownDefeatTuning value;
        static const bool loaded = value.Load();
        (void)loaded;
        return value;
    }
    // 各場面の時刻を前段より後へ制約し、非有限値は既定値へ戻します。
    void Sanitize() {
        auto limit=[](float& v,float lo,float hi,float fallback) { v=std::isfinite(v)?std::clamp(v,lo,hi):fallback; };
        limit(duration,9,18,12); limit(rupture,.22f,.34f,.28f);
        limit(shrink,rupture+.10f,.52f,.43f); limit(settle,shrink+.12f,.72f,.64f);
        limit(victory,settle+.08f,.88f,.76f);
        limit(spread,.5f,1.8f,1); limit(cameraDistance,.8f,1.4f,1);
        limit(shake,0,2,1); limit(energy,.4f,1.8f,1);
    }
    // ファイルが読めない場合は現在の値を維持します。欠けたキーも現在値を使います。
    bool Load() {
        std::ifstream file(Path); if(!file)return false;
        const auto j=nlohmann::json::parse(file,nullptr,false); if(!j.is_object())return false;
        auto read=[&](const char* name,float& value){if(j.contains(name)&&j[name].is_number())value=j[name].get<float>();};
        read("duration",duration);read("rupture",rupture);read("shrink",shrink);read("settle",settle);
        read("victory",victory);read("spread",spread);read("cameraDistance",cameraDistance);read("shake",shake);read("energy",energy);
        Sanitize();return true;
    }
    // 現在値をそのまま保存します。書き込み前の補正が必要なら Sanitize() を先に呼びます。
    bool Save() const {
        const nlohmann::json j={{"duration",duration},{"rupture",rupture},{"shrink",shrink},{"settle",settle},
            {"victory",victory},{"spread",spread},{"cameraDistance",cameraDistance},{"shake",shake},{"energy",energy}};
        std::ofstream file(Path);if(!file)return false;file<<j.dump(2)<<'\n';return file.good();
    }
};
