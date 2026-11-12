#pragma once
#include "PostEffect.h"
#include "IEditable.h"
#include <string>

// 担当：画面効果のパラメータを編集し、設定ファイルへ保存する。
// 関連：PostEffect（適用先）、DebugEditor（表示）
// 主な入口：Initialize / DrawImGui / SaveParams / LoadParams
class PostEffectEditor : public IEditable {
public:
    // エフェクト本体のポインタを受け取って初期化する
        // 編集対象のPostEffectを受け取り、UIから調整できる状態にします。
void Initialize(PostEffect* postEffect);

    // Inspectorに表示するUIの描画処理
        // ブルーム、色調補正などのポストエフェクト設定UIを描画します。
void DrawImGui() override;

    // Inspector上部のタイトルバーに表示される名前
    std::string GetName() override { return "Post Effect Settings"; }

    // パラメータの保存と読み込み
    void SaveParams(const std::string& filename = "Resources/json/post_effect.json");

    // 指定JSONの画面効果設定を読み込む。
    void LoadParams(const std::string& filename = "Resources/json/post_effect.json");

private:
    PostEffect* targetEffect_ = nullptr;
};