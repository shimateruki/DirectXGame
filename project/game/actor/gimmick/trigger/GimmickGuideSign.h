#pragma once

#include "BaseGimmick.h"
#include <memory>
#include <string>

namespace engine { class Camera; class SpriteCommon; }

// 担当：プレイヤーに反応する案内看板。Sprite Editorと同じレイアウトを読み込み、ゲームUIで再生します。
// 関連：GimmickFactory（生成）、GamePlayScene（UI描画）、SpriteLayoutAnimation（コマ選択）。
class GimmickGuideSign final : public BaseGimmick {
public:
    static constexpr const char* kTypeId = "GuideSign";
    static constexpr const char* kModelPath = "Gimmicks/guide_sign/guide_sign.obj";

    GimmickGuideSign();
    ~GimmickGuideSign() override;
    void Initialize(engine::Object3DCommon* common, const std::string& modelName) override;
    void Update(float deltaTime) override;
    std::unique_ptr<engine::Object3D> Clone() const override;

    // プレイヤーが反応範囲内にいる間、看板の上端を画面に投影して実演を描きます。
    void DrawGuide(engine::Camera* camera);

private:
    struct Layout;
    void LoadLayout(engine::SpriteCommon* common);

    std::unique_ptr<Layout> layout_;
    std::string layoutPath_; // 独立レイアウトJSONのResources相対パス。
    std::string attemptedPath_; // 同じ読み込みエラーの再試行を抑制します。
    float activationRadius_ = 5.0f; // 看板からの反応距離。ワールド単位。
    float displayHeight_ = 2.3f; // 看板原点から実演の下端までの高さ。ワールド単位。
    float displayWidth_ = 0.19f; // 画面幅に対する実演の幅。
    float displayOffsetX_ = 0.0f; // 看板の投影位置からの横ずれ。画面幅に対する割合。
    bool inRange_ = false;
    double elapsed_ = 0.0;
};
