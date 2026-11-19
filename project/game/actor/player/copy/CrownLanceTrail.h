#pragma once
#include "CrownLanceSettings.h"
#include "EffectObject3D.h"
#include <memory>

// 王冠槍は等速直進するため、柄の発射位置と現在位置だけで連続した帯の両端を決められます。
// 関連：王冠の射出槍が位置を渡し、残像の帯を更新・描画する。
// 主な入口：Initialize / Update / Draw
class CrownLanceTrail {
public:
    // この槍の帯表示Objectを生成し、モデル・専用材質・初期回転を設定する。
    void Initialize(engine::Object3DCommon* common, const engine::Vector3& start,
                    const engine::Quaternion& rotation) {
        start_ = start;
        if (!common)
            return;
        mesh_ = std::make_unique<EffectObject3D>();
        mesh_->Initialize(common);
        // 材質を取得できない場合は表示物を破棄し、残像だけを省略します。
        if (!mesh_->GetMaterialData()) {
            mesh_.reset();
            return;
        }
        // 帯は表示専用にし、槍が決めた回転を使って判定とは独立させます。
        mesh_->SetModel("Effects/crown_lance_trail");
        mesh_->SetColliderType(ColliderType::None);
        mesh_->SetQuaternionMaster(true);
        mesh_->SetQuaternion(rotation);
        mesh_->SetBlendMode(BlendMode::Normal);
        mesh_->SetProceduralType(15); // Effect3d.PSの王冠槍専用の帯。
        mesh_->ResetTime();
        mesh_->SetIsVisible(false);
    }

    // dtは秒。開始点から現在位置までの帯を、槍の進行方向に合わせて更新する。
    void Update(const engine::Vector3& root, const CrownLanceSettings::Appearance& style,
                bool full, float fade, float deltaTime) {
        if (!mesh_)
            return;
        // 発射直後は実際に進んだ距離で長さを制限し、最大長を超えたら槍の後方だけを残します。
        const float weight = full ? 1.35f : 1.0f;
        const float length = (std::min)(engine::Math::Length(root - start_), style.trailLength * weight)
                           * (1.0f - fade);
        mesh_->SetIsVisible(length > 0.015f);
        if (length <= 0.015f)
            return;
        mesh_->SetTranslate(root);
        const float width = style.trailWidth * weight * (1.0f - 0.65f * fade);
        mesh_->SetScale({width, length, width});
        // 帯の変形は移動から求め、流れるノイズと消え方は専用材質へ渡します。
        auto& material = *mesh_->GetMaterialData();
        material.time += deltaTime;
        material.distortionStrength = style.trailNoise;
        material.dissolveFade = fade;
        material.color = {style.edgeColor[0], style.edgeColor[1], style.edgeColor[2], 1.0f - fade};
        mesh_->UpdateWorldMatrix(true);
    }

    void Draw(ID3D12Resource* point, ID3D12Resource* spot, engine::Camera* camera, int bufferIndex) {
        if (!mesh_ || !mesh_->GetIsVisible())
            return;
        // カメラが渡された描画先には専用の行列を使い、省略時は現在の描画カメラを取り直します。
        if (camera) {
            mesh_->DrawForCamera(camera, point, spot, bufferIndex);
        } else {
            mesh_->RefreshRenderCameraData();
            mesh_->Draw(point, spot);
        }
    }

private:
    std::unique_ptr<EffectObject3D> mesh_;
    engine::Vector3 start_{};
};
