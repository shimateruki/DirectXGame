#pragma once
#include "CopyAbilitySupport.h"
#include "CrownPresentationSettings.h"
#include "EffectObject3D.h"
#include "ModelManager.h"
#include <deque>
#include <numbers>

// 近接・射撃・回避の一過性演出。攻撃終了後の残光も所有し、共有プリセットは変更しません。
// Ring/Hitが発生の入口です。EffectObject3dの残光をUpdate/Drawで処理し、攻撃判定は呼び出し側に任せます。
// 関連：王冠の攻撃から発生位置を受け、MeshEffect・GPU粒子の表示をまとめる。
// 主な入口：Update / Hit
class CrownAbilityEffects {
    using Vec = engine::Vector3;
    // 発生済みの効果と経過時間・寿命を保持する。負のageは表示開始までの待機秒数。
    struct Pulse {
        std::unique_ptr<EffectObject3D> visual;
        Vec position, normal;
        float age, duration, size; // 経過秒数・寿命秒数・表示倍率。ageが負なら発生待ち。
        bool fragments;
    };
    std::unique_ptr<Model> ring_;
    std::deque<Pulse> pulses_;
    float hitCooldown_ = 0.0f;
    static Vec Unit(Vec v) {
        const float length = engine::Math::Length(v);
        return length > 0.0001f ? v / length : Vec{0, 1, 0};
    }
    // 初回だけXZ平面の輪メッシュを生成・保持します。commonが未指定なら生成しません。
    void EnsureRing(engine::Object3DCommon* common) {
        if (ring_ || !common) return;
        std::vector<Model::VertexData> vertices;
        std::vector<uint32_t> indices;
        constexpr int kCount = 96;
        constexpr float kFullTurn = 2.0f * std::numbers::pi_v<float>; // 1周分の角度（ラジアン）。
        for (int i = 0; i <= kCount; ++i) {
            const float a = float(i) / kCount * kFullTurn;
            for (int side = 0; side < 2; ++side) {
                Model::VertexData v{};
                const float radius = side ? 1.0f : 0.95f;
                v.position = {std::cos(a) * radius, 0, std::sin(a) * radius, 1};
                v.normal = {0, 1, 0}; v.texcoord = {float(i) / kCount, float(side)};
                vertices.push_back(v);
            }
            if (i < kCount) {
                const uint32_t n = uint32_t(i * 2);
                for (auto v : {n,n+2,n+1,n+1,n+2,n+3}) indices.push_back(v);
            }
        }
        ring_ = std::make_unique<Model>();
        ring_->CreateFromVertices(ModelManager::GetInstance()->GetModelCommon(), vertices, indices);
    }
public:
    // 共有プリセットを値コピーし、位置・方向・倍率を変更して一回発生する。原本は編集しない。
    static void Emit(const char* name, Vec position, Vec direction, float size = 1.0f, float speed = 1.0f) {
        auto* particles = GpuParticleManager::GetInstance();
        if (!particles || !particles->IsInitialized()) return;
        const auto& presets = particles->GetPresets();
        const auto found = presets.find(name); if (found == presets.end()) return;
        auto c = found->second;
        c.emitPos = position;
        c.baseSize *= size; c.midSize *= size; c.endSize *= size;
        c.emitArea = c.emitArea * size; c.shapeRadius *= size; c.fieldRadius *= size;
        c.emitVelocity = Unit(direction) * engine::Math::Length(c.emitVelocity) * speed;
        c.isLooping = false;
        particles->EmitFromConfig(c);
    }
    // delayは発生までの秒数。輪の表示を所有し、寿命に達したらUpdateで回収する。
    void Ring(Vec position, Vec normal, float size, bool fragments = false, float delay = 0.0f) {
        auto* scene = SceneManager::GetInstance()->GetCurrentScene();
        auto* common = scene ? scene->GetObject3DCommon() : nullptr;
        EnsureRing(common); if (!ring_ || !common) return;
        if (pulses_.size() >= 8) return;
        Pulse pulse;
        pulse.visual = std::make_unique<EffectObject3D>();
        pulse.visual->Initialize(common); pulse.visual->SetModel(ring_.get());
        pulse.visual->SetColliderType(ColliderType::None);
        pulse.visual->SetProceduralType(17); pulse.visual->SetBlendMode(BlendMode::Normal);
        pulse.visual->SetQuaternionMaster(true);
        normal = Unit(normal);
        // XZ平面の輪を接触面へ向けます。床では必ず床に沿って広がります。
        const float dot = std::clamp(normal.y, -1.0f, 1.0f);
        if (dot < -0.9999f) pulse.visual->SetQuaternion({1,0,0,0});
        else {
            const float q = std::sqrt((1 + dot) * 2);
            pulse.visual->SetQuaternion({normal.z/q,0,-normal.x/q,q*0.5f});
        }
        pulse.position = position + normal * 0.06f; pulse.normal = normal;
        pulse.age = -delay; pulse.duration = fragments ? 0.38f : 0.25f;
        pulse.size = size; pulse.fragments = fragments;
        pulse.visual->SetIsVisible(false);
        pulses_.push_back(std::move(pulse));
    }
    // 命中位置と方向から粒子・破片・輪を発生する。ダメージの適用は呼出側が行う。
    void Hit(int stage, Vec position, Vec normal, Vec swing, const CrownPresentationSettings& style) {
        // 複数敵への同時命中でも発光の総量を抑えます。ダメージ判定とは独立です。
        if (hitCooldown_ > 0) return;
        hitCooldown_ = 0.045f;
        const float weight = stage == 2 ? 1.35f : stage == 1 ? 1.05f : 0.90f;
        Emit("player_crown_melee_flash", position + Unit(normal)*0.07f, normal, style.particleScale*weight);
        Emit("player_crown_melee_sparks", position, swing, style.particleScale*weight, weight);
        if (stage == 2) Emit("player_crown_melee_shards", position, normal, style.particleScale);
    }
    // dtは秒。遅延を含む効果の時間を進め、寿命を終えた表示物を取り除く。
    void Update(float deltaTime) {
        hitCooldown_ = (std::max)(0.0f, hitCooldown_ - deltaTime);
        for (auto& pulse : pulses_) {
            const float before = pulse.age;
            pulse.age += deltaTime;
            if (pulse.age < 0 || pulse.age >= pulse.duration) continue;
            if (before <= 0 && pulse.fragments)
                Emit("player_crown_melee_shards", pulse.position, pulse.normal, 1.2f);
            const float t = pulse.age / pulse.duration;
            const float radius = pulse.size * (0.18f + 0.82f * (1 - (1-t)*(1-t)));
            pulse.visual->SetTranslate(pulse.position);
            pulse.visual->SetScale({radius,1,radius});
            pulse.visual->SetIsVisible(true);
            pulse.visual->SetColor({1.25f,0.88f,0.34f,(1-t)*(1-t)*0.85f});
            pulse.visual->GetMaterialData()->time = pulse.age;
            pulse.visual->GetMaterialData()->dissolveFade = t;
        }
        std::erase_if(pulses_, [](const Pulse& pulse) { return pulse.age >= pulse.duration; });
    }
    void Draw(ID3D12Resource* point, ID3D12Resource* spot, Camera* camera, int index) {
        for (auto& pulse : pulses_) {
            if (!pulse.visual->GetIsVisible()) continue;
            pulse.visual->UpdateWorldMatrix(true);
            if (camera) pulse.visual->DrawForCamera(camera,point,spot,index);
            else { pulse.visual->RefreshRenderCameraData(); pulse.visual->Draw(point,spot); }
        }
    }
    // 所有する一時効果をすべて解放し、命中演出の再使用待ちも戻す。
    void Clear() { pulses_.clear(); hitCooldown_ = 0; }
};
