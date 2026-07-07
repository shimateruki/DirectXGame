#pragma once
#include "PostEffect.h"
#include "IEditable.h"
#include <string>

// PostEffectEditorは、画面全体にかかるポストエフェクトのパラメータを編集、保存します。
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
    void LoadParams(const std::string& filename = "Resources/json/post_effect.json");

private:
    PostEffect* targetEffect_ = nullptr;
};