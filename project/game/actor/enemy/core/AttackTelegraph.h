#pragma once

#include "EffectObject3d.h"
#include "engine/utility/math/Math.h"

#include <memory>

// 敵の攻撃前に出す共通予告表示。敵側は位置・範囲・進行度だけを渡す。
// AttackTelegraphは、敵の攻撃予兆を円形や線形のメッシュエフェクトとして表示します。
class AttackTelegraph {
public:
        // 予兆として表示する形状の種類です。
enum class Shape {
        Circle,
        Line
    };

        // 予兆表示用エフェクトを生成できるように共通描画機能を保持します。
void Initialize(Object3dCommon* common);
        // 円形範囲攻撃の予兆を表示します。
void ShowCircle(const Vector3& center, float radius, float progress, const Vector4& color);
        // 直線範囲攻撃の予兆を表示します。
void ShowLine(const Vector3& center, const Vector3& direction, float length, float width, float progress, const Vector4& color);
        // 攻撃直前の強調表示を直近の予兆位置で再生します。
void TriggerCue(const Vector4& color);
    void TriggerCueAt(const Vector3& center, float radius, const Vector4& color);
        // 表示中の予兆を非表示にします。
void Hide();
    void Update(float deltaTime);
    void Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource);
    bool IsVisible() const { return isVisible_; }

private:
    void EnsureEffect(Object3dCommon* common);
    void EnsureCueEffect(Object3dCommon* common);
    void ConfigureShape(Shape shape);
    void ConfigureCueShape(Shape shape);
        // EffectObject3dの手続き生成形状を予兆用に設定します。
void ConfigureEffectShape(EffectObject3d* effect, Shape shape);
    void ApplyCircle(EffectObject3d* effect, const Vector3& center, float radius, float progress, const Vector4& color, float scaleMultiplier) const;
    void ApplyLine(EffectObject3d* effect, const Vector3& center, const Vector3& direction, float length, float width, float progress, const Vector4& color, float scaleMultiplier) const;
    Vector4 MakeDisplayColor(const Vector4& color, float progress) const;
    Vector4 MakeCueColor(const Vector4& color, float progress) const;

    std::unique_ptr<EffectObject3d> effect_;
    std::unique_ptr<EffectObject3d> cueEffect_;
    Object3dCommon* common_ = nullptr;
    Shape shape_ = Shape::Circle;
    Shape cueShape_ = Shape::Circle;
    Shape lastShape_ = Shape::Circle;
    Vector3 lastCenter_ = { 0.0f, 0.0f, 0.0f };
    Vector3 lastDirection_ = { 0.0f, 0.0f, 1.0f };
    Vector4 lastColor_ = { 1.0f, 0.1f, 0.04f, 1.0f };
    Vector4 cueColor_ = { 1.0f, 0.05f, 0.02f, 1.0f };
    float lastRadius_ = 1.0f;
    float lastLength_ = 1.0f;
    float lastWidth_ = 1.0f;
    bool isVisible_ = false;
    bool hasLastShape_ = false;
    bool isShapeConfigured_ = false;
    bool isCueShapeConfigured_ = false;
    float pulseTimer_ = 0.0f;
    float cueTimer_ = 0.0f;
    float cueDuration_ = 0.18f;
};
