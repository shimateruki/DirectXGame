#pragma once
#include "Model.h"
#include "Easing.h"
#include <numbers>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <numeric>
#include <unordered_map>

// 元モデルを変更せず、同じ表面の一部を鞭の軌道へ引き伸ばします。
// 元のUV・材質を保ち、小さな独立部品（目）と接地面は変形から除外します。
// 初動の形状引数。進行度は0～1、各強度は体の半径に対する比率です。
struct PlayerWhipLaunchShape {
    float progress = 1.0f;
    float mound = 0.0f;
    float draw = 0.0f;
    float flow = 0.0f;
    engine::Vector3 direction{0,0,1};
};

// 関連：PlayerStateWhipが曲線を渡し、専用のモデル複製だけを変形する。
// 主な入口：Initialize / Update
// 鞭変形用に複製したモデルを保持し、共有元のModelの頂点は書き換えない。
class PlayerWhipDeformer final {
    using Vec = engine::Vector3;
    // 一つのメッシュの元頂点・変形後頂点と、本体だけを変形するための対応表。
    struct Surface {
        std::vector<Model::VertexData> rest, output;
        std::vector<bool> body;
    };
public:
    Model* GetSource() const { return source_; }
    Model* GetModel() const { return model_.get(); }

    // 元モデルのCPUデータを複製・細分化して専用モデルを作る。元のModelは変更しない。
    bool Initialize(Model* source, ModelCommon* common) {
        if (!source || !common || source->GetModelData().hasSkinning) return false;
        source_ = source;
        center_ = source->GetCenter();
        half_ = source->GetSize() * 0.5f;
        if (half_.x < 0.001f || half_.y < 0.001f || half_.z < 0.001f) return false;
        Model::ModelData data = source->GetModelData();
        for (auto& mesh : data.meshes) {
            Surface surface;
            // 接続成分を調べ、4頂点の目などを面積の大きな体表と区別します。
            std::vector<size_t> parent(mesh.vertices.size());
            std::iota(parent.begin(), parent.end(), size_t{0});
            auto root = [&parent](size_t i) {
                while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
                return i;
            };
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const auto a = root(mesh.indices[i]);
                parent[root(mesh.indices[i + 1])] = a;
                parent[root(mesh.indices[i + 2])] = a;
            }
            std::vector<size_t> counts(parent.size(), 0);
            for (size_t i = 0; i < parent.size(); ++i) ++counts[root(i)];
            for (size_t i = 0; i < parent.size(); ++i) surface.body.push_back(counts[root(i)] > 16);
            // 体表の輪郭を保ったまま3段階細分化。UV境界は元の対応を維持します。
            for (int level = 0; level < 3; ++level) {
                std::unordered_map<uint64_t, uint32_t> edges;
                auto midpoint = [&](uint32_t a, uint32_t b) {
                    const uint64_t key = (uint64_t((std::min)(a,b)) << 32) | (std::max)(a,b);
                    if (auto it = edges.find(key); it != edges.end()) return it->second;
                    const auto va = mesh.vertices[a], vb = mesh.vertices[b];
                    Model::VertexData v{};
                    v.position = {(va.position.x+vb.position.x)*0.5f,(va.position.y+vb.position.y)*0.5f,
                                  (va.position.z+vb.position.z)*0.5f,1.0f};
                    v.normal = Unit(va.normal + vb.normal, va.normal);
                    v.tangent = Unit(va.tangent + vb.tangent, {1,0,0});
                    v.texcoord = {(va.texcoord.x+vb.texcoord.x)*0.5f,(va.texcoord.y+vb.texcoord.y)*0.5f};
                    const auto index = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(v); surface.body.push_back(surface.body[a] && surface.body[b]);
                    edges.emplace(key,index);return index;
                };
                std::vector<uint32_t> indices; indices.reserve(mesh.indices.size()*4);
                for (size_t i=0;i+2<mesh.indices.size();i+=3) {
                    const auto a=mesh.indices[i],b=mesh.indices[i+1],c=mesh.indices[i+2];
                    const float highest=(std::max)({mesh.vertices[a].position.y,mesh.vertices[b].position.y,mesh.vertices[c].position.y});
                    // 変形しない下半身は細分化せず、元の輪郭と計算量を維持します。
                    if ((!surface.body[a] && !surface.body[b] && !surface.body[c]) || highest <= center_.y+half_.y*0.42f) {
                        indices.insert(indices.end(),{a,b,c});continue;
                    }
                    const auto ab=midpoint(a,b),bc=midpoint(b,c),ca=midpoint(c,a);
                    indices.insert(indices.end(),{a,ab,ca,ab,b,bc,ca,bc,c,ab,bc,ca});
                }
                mesh.indices = std::move(indices);
            }
            for (auto& v : mesh.vertices) { v.boneWeights={0,0,0,0};v.boneIndices={0,0,0,0}; }
            surface.rest = mesh.vertices;surface.output = surface.rest;
            surfaces_.push_back(std::move(surface));
            mesh.vertexResource.Reset();mesh.indexResource.Reset();mesh.computeSkinnedVertexResource.Reset();
        }
        model_ = std::make_unique<Model>();
        model_->InitializeFromCpuData(common,std::move(data),"PlayerWhipSurface");
        for (const auto& mesh : model_->GetModelData().meshes) if (!mesh.vertexResource) return false;
        return !surfaces_.empty();
    }

    template<size_t N>
    void Update(const std::array<Vec,N>& curve, const Vec& origin, const engine::Matrix4x4& world,
                float rootAngle, float lift, float thickness, float tipRadius = 0.22f,
                float contactWeight = 0.0f, const Vec& contactNormal = {},
                float returnProgress = 0.0f, float returnBulge = 0.0f,
                float returnReceive = 0.0f, const PlayerWhipLaunchShape& launch = {}) {
        if (!model_) return;
        const auto inverse = engine::Math::Inverse(world);
        std::array<Vec,N> local;
        for(size_t i=0;i<N;++i) local[i]=engine::Math::Transform(origin+curve[i],inverse);
        const float worldRadius=engine::Math::Length(engine::Math::TransformNormal({half_.x,0,0},world));
        const float reach=engine::Math::Length(curve.back()-curve.front());
        // 体内へ収まった後も直前の付け根方向を保ち、ゼロ長で輪郭を反転させません。
        const float launchProgress = std::clamp(launch.progress,0.0f,1.0f);
        const bool launching = launchProgress > 0.0f && launchProgress < 1.0f &&
            (launch.mound > 0.0f || launch.draw > 0.0f || launch.flow > 0.0f);
        if (reach > worldRadius*0.5f)
            lastHeading_ = Unit(Vec{local[N/3].x-center_.x,0,local[N/3].z-center_.z},{0,0,1});
        else if (launching) {
            // 体表がまだ伸びていない間も、今回の振り始めの方向へ盛り上げます。
            const auto localDirection = engine::Math::TransformNormal(launch.direction,inverse);
            lastHeading_ = Unit(Vec{localDirection.x,0,localDirection.z},lastHeading_);
        }
        const auto heading = lastHeading_;
        const auto axis=Unit(Vec{heading.x,lift,heading.z},{0,1,0});
        const float cutoff=std::cos(rootAngle*std::numbers::pi_v<float>/180.0f);
        const float activation=Easing::SmoothStep01((reach-worldRadius*0.5f)/(std::max)(worldRadius*1.1f,0.001f));
        const float width=std::clamp(thickness/(std::max)(worldRadius,0.001f),0.15f,1.0f);
        auto surfaceWeight = [&](const Vec& p) {
            const auto offset=p-center_;
            const Vec q{offset.x/half_.x,offset.y/half_.y,offset.z/half_.z};
            const auto radial=Unit(q,{0,1,0});
            const float height=Easing::SmoothStep01((q.y-0.42f)/0.30f);
            const float dot=radial.x*axis.x+radial.y*axis.y+radial.z*axis.z;
            return Easing::SmoothStep01((dot-cutoff)/(1.0f-cutoff))*height;
        };
        // 理想球面ではなく実メッシュの頂点で最大値を求め、調整値にかかわらず先端を届かせます。
        float peakWeight=0.000001f;
        for (const auto& surface:surfaces_) for(size_t j=0;j<surface.rest.size();++j) {
            if (!surface.body[j] || surface.rest[j].position.y <= center_.y+half_.y*0.42f) continue;
            const auto p=surface.rest[j].position;
            peakWeight=(std::max)(peakWeight,surfaceWeight({p.x,p.y,p.z}));
        }
        const float capSpan = std::clamp(tipRadius * 3.0f / (std::max)(reach, 0.001f), 0.04f, 0.28f);
        const float returnCenter = 1.0f - Easing::SmoothStep01(returnProgress);
        const float flowWeight = std::sin(std::numbers::pi_v<float> * std::clamp(returnProgress,0.0f,1.0f));
        const float receiveWeight = Easing::SmoothStep01((returnProgress-0.35f)/0.65f) * returnReceive;
        const float launchCenter = Easing::SmoothStep01(launchProgress);
        const float moundWeight = launching ? launch.mound * Easing::SmoothStep01(launchProgress/0.20f) *
            (1.0f-Easing::SmoothStep01((launchProgress-0.20f)/0.55f)) : 0.0f;
        const float drawWeight = launching ? launch.draw * Easing::SmoothStep01(launchProgress/0.25f) *
            (1.0f-Easing::SmoothStep01((launchProgress-0.65f)/0.35f)) : 0.0f;
        const float launchFlowWeight = launching ? launch.flow * std::sin(std::numbers::pi_v<float>*launchProgress) : 0.0f;
        auto deform = [&](const Vec& p) {
            const float t=std::clamp(surfaceWeight(p)/peakWeight,0.0f,1.0f);
            if(t<0.000001f) return p;
            // 根元寄りの上半身だけに受け止めの丸みを作り、顔と接地面は保護します。
            Vec received{};
            if (receiveWeight > 0.0f) {
                // 収まった後は中心も膨らませ、輪状の段差を残しません。
                const float rootMask = Easing::SmoothStep01(t) * (1.0f-activation*t);
                received = Unit(p-center_,{0,1,0}) * (half_.x * receiveWeight * rootMask);
            }
            Vec launched{};
            if (moundWeight > 0.0f || drawWeight > 0.0f) {
                // 中心まで連続する盛り上がり。周囲だけを軽く絞り、顔と足元は動かしません。
                const float gatherMask = 4.0f*t*(1.0f-t) * (1.0f-Easing::SmoothStep01(t/0.40f));
                launched = axis * (half_.y*moundWeight*Easing::SmoothStep01(t)*(1.0f-activation));
                if (drawWeight > 0.0f) launched = launched - Unit(p-center_,{0,1,0})*(half_.x*drawWeight*gatherMask);
            }
            if (activation <= 0) return p + received + launched;
            const float index=t*static_cast<float>(N-1);
            const auto i=(std::min)(static_cast<size_t>(index),N-2);
            const auto path=local[i]+(local[i+1]-local[i])*(index-static_cast<float>(i));
            // 付け根では元の表面、先端では判定曲線の位置へ連続して接続します。
            const float taper=1.0f-t;
            const Vec target=path+(p-local[0])*taper;
            // 中腹を細くする量も0→1→0とし、付け根と先端は動かしません。
            const auto tangent=Unit(local[(std::min)(i+2,N-1)]-local[i],heading);
            const auto crossOffset=p-center_;
            const float along=crossOffset.x*tangent.x+crossOffset.y*tangent.y+crossOffset.z*tangent.z;
            const auto transverse=crossOffset-tangent*along;
            auto narrowed=target-transverse*((1.0f-width)*4.0f*t*(1.0f-t)*0.20f);
            if ((returnBulge > 0.0f && flowWeight > 0.0f) || launchFlowWeight > 0.0f) {
                const float returnWave = returnBulge*flowWeight*(1.0f-Easing::SmoothStep01(std::abs(t-returnCenter)/0.35f));
                const float launchWave = launchFlowWeight*(1.0f-Easing::SmoothStep01(std::abs(t-launchCenter)/0.35f));
                const float wave = returnWave + launchWave;
                const Vec flowRadial{crossOffset.x/half_.x,crossOffset.y/half_.y,crossOffset.z/half_.z};
                const auto around = flowRadial-axis*engine::Math::Dot(flowRadial,axis);
                const auto sideways = Unit(around-tangent*engine::Math::Dot(around,tangent),{});
                narrowed = narrowed + sideways * (half_.x*wave*4.0f*t*(1.0f-t));
            }
            // 先端の一部だけを丸めます。根元と最前端は固定し、別の球体は追加しません。
            if (t <= 1.0f-capSpan) return p+(narrowed-p)*activation+received+launched;
            const float cap = std::clamp((t - (1.0f-capSpan)) / capSpan, 0.0f, 1.0f);
            const float envelope = std::sin(std::numbers::pi_v<float> * cap);
            const auto radial = p - center_;
            const Vec unitRadial{radial.x / half_.x, radial.y / half_.y, radial.z / half_.z};
            const auto around = unitRadial - axis * engine::Math::Dot(unitRadial, axis);
            const auto radialWorld = engine::Math::TransformNormal(around, world);
            const auto tangentWorld = Unit(engine::Math::TransformNormal(tangent, world), {0,0,1});
            const auto sideWorld = Unit(radialWorld - tangentWorld * engine::Math::Dot(radialWorld, tangentWorld), {});
            auto pointWorld = engine::Math::Transform(narrowed, world) + sideWorld * (tipRadius * std::sqrt((std::max)(0.0f, envelope)) * Easing::SmoothStep01(cap / 0.3f));
            // 接触面の法線方向を圧縮し、面に沿う方向へわずかに広げます。先端位置は維持します。
            const auto normal = Unit(contactNormal, tangentWorld);
            const auto tipWorld = origin + curve.back();
            const auto offset = pointWorld - tipWorld;
            const float flatten = std::clamp(contactWeight, 0.0f, 0.8f) * Easing::SmoothStep01(cap);
            const auto axial = normal * engine::Math::Dot(offset, normal);
            pointWorld = tipWorld + axial * (1.0f - flatten) + (offset - axial) * (1.0f + flatten * 0.25f);
            const auto shaped = engine::Math::Transform(pointWorld, inverse);
            return p+(shaped-p)*activation+received+launched;
        };
        const float epsilon=(std::max)(half_.x*0.001f,0.00005f);
        for (size_t m=0;m<surfaces_.size();++m) {
            auto& surface=surfaces_[m];
            for(size_t j=0;j<surface.rest.size();++j) {
                const auto& rest=surface.rest[j];auto& out=surface.output[j];out=rest;
                if(!surface.body[j] || rest.position.y <= center_.y+half_.y*0.42f) continue;
                const Vec p{rest.position.x,rest.position.y,rest.position.z};
                const auto next=deform(p);
                out.position={next.x,next.y,next.z,1};
                if(engine::Math::Length(next-p)<0.000001f) continue;
                // 位置だけでなく接平面も変形し、付け根の陰影が別部品に見えないようにします。
                const auto n=Unit(rest.normal,{0,1,0});
                const auto a=Unit(engine::Math::Cross(std::abs(n.y)<0.9f?Vec{0,1,0}:Vec{1,0,0},n),{1,0,0});
                const auto b=engine::Math::Cross(n,a);
                const auto da=deform(p+a*epsilon)-deform(p-a*epsilon);
                const auto db=deform(p+b*epsilon)-deform(p-b*epsilon);
                out.normal=Unit(engine::Math::Cross(da,db),n);
                out.tangent=Unit(da,a);
            }
            const auto& resource=model_->GetModelData().meshes[m].vertexResource;
            void* mapped=nullptr;const D3D12_RANGE readRange{0,0};
            if(SUCCEEDED(resource->Map(0,&readRange,&mapped))) {
                std::memcpy(mapped,surface.output.data(),surface.output.size()*sizeof(Model::VertexData));
                resource->Unmap(0,nullptr);
            }
        }
    }
private:
    static Vec Unit(const Vec& v,const Vec& fallback){const float length=engine::Math::Length(v);return length>0.000001f?v/length:fallback;}
    Model* source_=nullptr;
    std::unique_ptr<Model> model_;
    std::vector<Surface> surfaces_;
    Vec center_{},half_{};
    Vec lastHeading_{0,0,1};
};
