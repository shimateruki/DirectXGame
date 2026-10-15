#pragma once

#include "EffectObject3d.h"
#include "engine/utility/math/Math.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// 敵の攻撃予兆を円形・線形などのメッシュエフェクトとして表示します。
// 位置はワールド座標、半径・長さ・幅はワールド距離、progress は 0～1 の予兆進行率です。
class AttackTelegraph {
  public:
    // 予兆として表示する形状の種類です。
    enum class Shape { Circle, ImpactDisc, Line, DecalCircle };

    // 描画用エフェクトを必要時に生成するための共有コンテキストを借ります。
    void Initialize(Object3dCommon* common);
    // 円形範囲攻撃の予兆を表示します。
    void ShowCircle(const engine::Vector3& center, float radius, float progress, const engine::Vector4& color);
    // 専用テクスチャを使い、攻撃半径を固定した地面デカールとして表示します。
    void ShowDecalCircle(const engine::Vector3& center, float radius, float progress, const engine::Vector4& color,
                         const std::string& texturePath);
    // 直線範囲攻撃の予兆を表示します。
    void ShowLine(const engine::Vector3& center, const engine::Vector3& direction, float length, float width, float progress,
                  const engine::Vector4& color);
    // ブレスや扇状弾など、先端へ向かって広がる攻撃範囲を表示します。
    void ShowCone(const engine::Vector3& origin, const engine::Vector3& direction, float length, float startWidth, float endWidth,
                  float progress, const engine::Vector4& color);
    // 連続落雷など、実際に攻撃が発生する複数地点をまとめて表示します。
    void ShowImpactAreas(const engine::Vector3* centers, std::size_t centerCount, float radius, float progress,
                         const engine::Vector4& color);
    // 多数の着弾地点を、薄い危険面と収束リングに分けて読みやすく表示します。
    void ShowReadableImpactAreas(const engine::Vector3* centers, std::size_t centerCount, float radius, float progress,
                                 const engine::Vector4& color);
    // 複数の飛び道具が通るレーンを、本数と間隔に合わせて表示します。
    void ShowLaneFan(const engine::Vector3& origin, const engine::Vector3& direction, float length, float width, int laneCount,
                     float lateralSpacing, float angleStep, float progress, const engine::Vector4& color);
    // 攻撃直前の強調表示を直近の予兆位置で再生します。
    void TriggerCue(const engine::Vector4& color);
    void TriggerCueAt(const engine::Vector3& center, float radius, const engine::Vector4& color);
    // 敵本体の上へ、回避入力の瞬間を伝える短い共通フラッシュを表示します。
    void TriggerWarningCue(engine::Object3d* target, const engine::Vector3& offset, float size, const engine::Vector4& accentColor);
    // 表示中の予兆を非表示にします。
    void Hide();
    // deltaTime は秒。予兆の脈動と短い強調表示の残り時間を更新します。
    void Update(float deltaTime);
    // 地面上の範囲予兆と、対象の頭上に出す警告を別パスで描画します。
    void DrawGround(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    void DrawWarning(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    bool IsVisible() const { return isVisible_; }

  private:
    // 必要数だけ予兆・強調用Effectを遅延生成する。
    void EnsureEffect(Object3dCommon* common);
    void EnsureCueEffect(Object3dCommon* common);
    void EnsureWarningEffect(Object3dCommon* common);
    void EnsurePatternEffects(std::size_t count);
    void EnsureTimingEffects(std::size_t count);
    // 表示する形状と有効個数を切り替え、前の予兆を片付ける。
    void BeginPattern(Shape shape, std::size_t count);
    void BeginTiming(Shape shape, std::size_t count);
    // パターンとタイミング表示をまとめて消す。
    void HidePatternEffects();
    void HideTimingEffects();
    // 頭上警告の追従位置と残り時間を更新する。
    void UpdateWarningEffect(float deltaTime);
    // 使用する形状が変わったときにメッシュ構成を更新する。
    void ConfigureShape(Shape shape);
    void ConfigureCueShape(Shape shape);
    // EffectObject3dの手続き生成形状を予兆用に設定します。
    void ConfigureEffectShape(EffectObject3d* effect, Shape shape);
    // 発動タイミング表示は範囲本体より細く明るい形状に設定します。
    void ConfigureTimingEffectShape(EffectObject3d* effect, Shape shape);
    // 個別Effectへ円、デカール、線の寸法と進行率を反映する。
    void ApplyCircle(EffectObject3d* effect, const engine::Vector3& center, float radius, float progress, const engine::Vector4& color,
                     float scaleMultiplier) const;
    void ApplyDecalCircle(EffectObject3d* effect, const engine::Vector3& center, float radius, float progress,
                          const engine::Vector4& color, float scaleMultiplier) const;
    void ApplyLine(EffectObject3d* effect, const engine::Vector3& center, const engine::Vector3& direction, float length, float width,
                   float progress, const engine::Vector4& color, float scaleMultiplier) const;
    void ApplyLineTimingMarker(EffectObject3d* effect, const engine::Vector3& origin, const engine::Vector3& direction, float length,
                               float width, float progress, const engine::Vector4& color) const;
    // 予兆本体とは別に発動タイミング用のマーカーを表示する。
    void ShowCircleTiming(const engine::Vector3& center, float radius, float progress, const engine::Vector4& color);
    void ShowLineTiming(const engine::Vector3& origin, const engine::Vector3& direction, float length, float width, float progress,
                        const engine::Vector4& color);
    // 予兆、デカール、強調、タイミングごとの透明度と発光色を計算する。
    engine::Vector4 MakeDisplayColor(const engine::Vector4& color, float progress) const;
    engine::Vector4 MakeDecalColor(const engine::Vector4& color, float progress) const;
    engine::Vector4 MakeCueColor(const engine::Vector4& color, float progress) const;
    engine::Vector4 MakeTimingColor(const engine::Vector4& color, float progress) const;

    std::unique_ptr<EffectObject3d> effect_;
    std::unique_ptr<EffectObject3d> cueEffect_;
    std::unique_ptr<EffectObject3d> warningEffect_;
    std::vector<std::unique_ptr<EffectObject3d>> patternEffects_;
    std::vector<std::unique_ptr<EffectObject3d>> timingEffects_;
    Object3dCommon* common_ = nullptr;
    Shape shape_ = Shape::Circle;
    Shape cueShape_ = Shape::Circle;
    Shape lastShape_ = Shape::Circle;
    Shape patternShape_ = Shape::Line;
    Shape timingShape_ = Shape::Circle;
    engine::Vector3 lastCenter_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 lastDirection_ = {0.0f, 0.0f, 1.0f};
    engine::Vector4 lastColor_ = {1.0f, 0.1f, 0.04f, 1.0f};
    engine::Vector4 cueColor_ = {1.0f, 0.05f, 0.02f, 1.0f};
    float lastRadius_ = 1.0f;
    float lastLength_ = 1.0f;
    float lastWidth_ = 1.0f;
    std::string decalTexturePath_;
    bool isVisible_ = false;
    bool hasLastShape_ = false;
    bool isShapeConfigured_ = false;
    bool isCueShapeConfigured_ = false;
    bool isPatternShapeConfigured_ = false;
    bool isTimingShapeConfigured_ = false;
    std::size_t patternActiveCount_ = 0;
    std::size_t timingActiveCount_ = 0;
    float pulseTimer_ = 0.0f;
    float cueTimer_ = 0.0f;
    float cueDuration_ = 0.18f;
    float patternCueTimer_ = 0.0f;
    engine::Vector4 patternCueColor_ = {1.0f, 0.05f, 0.02f, 1.0f};
    engine::Vector3 warningCenter_ = {0.0f, 0.0f, 0.0f};
    engine::Vector3 warningOffset_ = {0.0f, 0.0f, 0.0f};
    engine::Vector4 warningAccentColor_ = {1.0f, 0.45f, 0.08f, 1.0f};
    float warningSize_ = 1.0f;
    // シーンが所有する対象への非所有参照。予告を対象に追従させる間だけ使います。
    engine::Object3d* warningTarget_ = nullptr;
};
