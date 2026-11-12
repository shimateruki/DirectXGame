#pragma once
#include "EffectObject3D.h"
#include "ModelManager.h"
#include <array>
#include <deque>
#include <cstring>

// 武器の実際の根元・穂先の履歴をつなぐ帯。モデルと頂点バッファをこの能力だけで所有します。
// 関連：CrownMeleeComboが穂先の標本を渡し、描画用モデルの帯へ変換する。
// 主な入口：Initialize / Add / Tick / Draw
class CrownMeleeTrail {
    using Vec = engine::Vector3;
    // 帯の内外の点と経過秒数、途切れを識別する区間番号。別区間同士は接続しない。
    struct Sample { Vec inner, outer; float age; unsigned segment; };
    static constexpr size_t kCapacity = 256;
    std::unique_ptr<Model> model_;
    std::unique_ptr<EffectObject3D> visual_;
    std::deque<Sample> samples_;
    std::vector<Model::VertexData> vertices_;
    float lifetime_ = 0.22f; // 一標本を残す秒数。経過した標本はTickで捨てる。
    unsigned segment_ = 0; // Breakごとに進む区間番号。技の段をまたいで帯を接続しない。
public:
    // 能力専用の帯モデルと表示物を一度だけ生成する。common未指定なら生成しない。
    void Initialize(engine::Object3DCommon* common) {
        if (visual_ || !common) return;
        vertices_.resize((kCapacity - 1) * 4);
        std::vector<uint32_t> indices;
        for (uint32_t i = 0; i < kCapacity - 1; ++i) {
            const uint32_t n = i * 4;
            for (auto v : {n, n + 1, n + 2, n + 2, n + 1, n + 3}) indices.push_back(v);
        }
        model_ = std::make_unique<Model>();
        model_->CreateFromVertices(ModelManager::GetInstance()->GetModelCommon(), vertices_, indices);
        visual_ = std::make_unique<EffectObject3D>();
        visual_->Initialize(common);
        visual_->SetModel(model_.get());
        visual_->SetColliderType(ColliderType::None);
        visual_->SetProceduralType(16);
        visual_->SetBlendMode(BlendMode::Normal);
        visual_->SetColor({1.0f, 0.79f, 0.36f, 0.88f});
        visual_->SetDistortionStrength(0.24f);
    }
    // 標本の履歴を消す。生成済みモデルは次の攻撃でも再利用する。
    void Clear() { samples_.clear(); ++segment_; }
    // 区間番号を進め、次に追加する標本と直前の帯を切り離す。
    void Break() { ++segment_; }
    // dtとlifeは秒。標本を古くし、指定寿命を越えた履歴を捨てる。
    void Tick(float deltaTime, float lifetime) {
        lifetime_ = lifetime;
        for (auto& sample : samples_) sample.age += deltaTime;
        while (!samples_.empty() && samples_.front().age >= lifetime_) samples_.pop_front();
        if (visual_ && visual_->GetMaterialData()) visual_->GetMaterialData()->time += deltaTime;
    }
    // ワールド空間の穂先・軸・幅から一標本を追加する。ageはフレーム内で既に経過した秒数。
    void Add(Vec tip, Vec axis, float width, float age) {
        if (age >= lifetime_) return;
        if (!samples_.empty() && samples_.back().segment == segment_ && engine::Math::Length(tip - samples_.back().outer) < 0.015f) return;
        samples_.push_back({tip - axis * width, tip, age, segment_});
        if (samples_.size() > kCapacity) samples_.pop_front();
    }
    void Draw(ID3D12Resource* point, ID3D12Resource* spot, Camera* camera, int index) {
        if (!visual_ || samples_.size() < 2) return;
        Model::VertexData collapsed{};
        collapsed.position.w = 1.0f;
        std::fill(vertices_.begin(), vertices_.end(), collapsed);
        for (size_t i = 0; i + 1 < samples_.size(); ++i) {
            const auto& a = samples_[i];
            const auto& b = samples_[i + 1];
            if (a.segment != b.segment) continue;
            const Vec positions[]{a.inner, a.outer, b.inner, b.outer};
            for (size_t j = 0; j < 4; ++j) {
                auto& v = vertices_[i * 4 + j];
                const auto& pos = positions[j];
                v.position = {pos.x, pos.y, pos.z, 1.0f};
                v.normal = {0, 1, 0};
                v.texcoord = {(j < 2 ? a.age : b.age) / lifetime_, float(j % 2)};
            }
        }
        const auto& meshes = model_->GetModelData().meshes;
        if (meshes.empty() || !meshes[0].vertexResource) return;
        void* mapped = nullptr;
        const D3D12_RANGE read{0, 0};
        if (FAILED(meshes[0].vertexResource->Map(0, &read, &mapped))) return;
        std::memcpy(mapped, vertices_.data(), vertices_.size() * sizeof(Model::VertexData));
        meshes[0].vertexResource->Unmap(0, nullptr);
        visual_->UpdateWorldMatrix(true);
        if (camera) visual_->DrawForCamera(camera, point, spot, index);
        else { visual_->RefreshRenderCameraData(); visual_->Draw(point, spot); }
    }
};
