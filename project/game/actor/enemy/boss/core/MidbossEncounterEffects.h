#pragma once

#include <string>

// 中ボスの登場・封鎖・解除で使うVFXシーケンス名を、先読みと再生へ渡す。
struct MidbossEncounterEffects {
    // アリーナ入場時、隔離壁を有効にした直後の演出。
    std::string seal;
    // ボス本体の登場動作に合わせて再生する演出。
    std::string appearance;
    // ボス撃破後、アリーナを解放するときの演出。
    std::string release;

    // 最初の呼び出しで専用の encounter.json を読み、同じ設定を実行中に共有する。
    static const MidbossEncounterEffects& Prism();
    static const MidbossEncounterEffects& Magma();
};
