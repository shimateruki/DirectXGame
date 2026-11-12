#include "GpuParticleEmitter.h"
#include "GpuParticleManager.h"
#include "Object3D.h" 
#include "Model.h"    
#include <cmath>

namespace {
// エミッターが空の親に付いていても、子階層にある描画メッシュを使う。
engine::Object3D* FindModelObject(engine::Object3D* obj) {
    if (!obj)
        return nullptr;
    if (obj->GetModel())
        return obj;
    for (auto child : obj->GetChildren()) {
        engine::Object3D* found = FindModelObject(child);
        if (found)
            return found;
    }

    return nullptr;
}
} // namespace

void GpuParticleEmitter::Initialize(const std::string& presetName, engine::Object3D* targetObject) {
    presetName_ = presetName;
    targetObject_ = targetObject;
    isPlaying_ = false;
    emitTimer_ = 0.0f;
}


void GpuParticleEmitter::Update(float deltaTime) {
    if (!isPlaying_ || presetName_.empty())
        return;
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        return;

    // 0以下や非数の間隔では剰余が定義できないため、各更新で一度だけ発生させる。
    if (!std::isfinite(emitInterval_) || emitInterval_ <= 0.0f) {
        EmitOnce();
        emitTimer_ = 0.0f;
        return;
    }

    emitTimer_ += deltaTime;

    if (emitTimer_ >= emitInterval_) {
        EmitOnce();
        emitTimer_ = std::fmod(emitTimer_, emitInterval_);
    }
}

void GpuParticleEmitter::Play() { isPlaying_ = true; emitTimer_ = emitInterval_; }
void GpuParticleEmitter::Stop() { isPlaying_ = false; emitTimer_ = 0.0f; }

// 発生元Objectのモデル情報と姿勢を収集し、設定済みSystemへ一回分の粒子を発生させます。
void GpuParticleEmitter::EmitOnce() {
    engine::Vector3 spawnPos = engine::Vector3::ZERO;
    ID3D12Resource* vb = nullptr;
    uint32_t vCount = 0;
    uint32_t vStride = 0;
    uint32_t boneSrv = 0;
    engine::Matrix4x4 worldMat = { 1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f };

    if (targetObject_) {
        // ルートにメッシュがない場合は、子階層のモデルを発生元として使う。
        engine::Object3D* modelObj = FindModelObject(targetObject_);

        if (modelObj && modelObj->GetModel()) {
            worldMat = modelObj->GetWorldMatrix();
            Model* model = modelObj->GetModel();
            const auto& modelData = model->GetModelData();

            if (!modelData.meshes.empty()) {
                vb = modelData.meshes[0].vertexResource.Get();
                vCount = static_cast<uint32_t>(modelData.meshes[0].vertices.size());
                vStride = sizeof(Model::VertexData);
                boneSrv = model->GetBoneSrvIndex();
            }
        }
        else {
            worldMat = targetObject_->GetWorldMatrix();
        }

        engine::Math math;
        engine::Vector3 worldOffset = math.TransformNormal(offset_, worldMat);
        spawnPos.x = worldMat.m[3][0] + worldOffset.x;
        spawnPos.y = worldMat.m[3][1] + worldOffset.y;
        spawnPos.z = worldMat.m[3][2] + worldOffset.z;
    }
    else {
        spawnPos = offset_;
    }

    GpuParticleManager::GetInstance()->SetEmitterMesh(vb, vCount, vStride, boneSrv);
    GpuParticleManager::GetInstance()->Emit(presetName_, spawnPos, worldMat);
}
