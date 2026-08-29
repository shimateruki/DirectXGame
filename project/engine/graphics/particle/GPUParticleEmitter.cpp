#include "GPUParticleEmitter.h"
#include "GPUParticleManager.h"
#include "Object3d.h" 
#include "Model.h"    

// 発生元の子階層を走査し、実際にMeshを持つObjectを探します。
Object3d* FindModelObject(Object3d* obj) {
    if (!obj) return nullptr;
    if (obj->GetModel()) return obj;
    for (auto child : obj->GetChildren()) {
        Object3d* found = FindModelObject(child);
        if (found) return found;
    }
    return nullptr;
}

void GPUParticleEmitter::Initialize(const std::string& presetName, Object3d* targetObject) {
    presetName_ = presetName; targetObject_ = targetObject; isPlaying_ = false; emitTimer_ = 0.0f;
}


void GPUParticleEmitter::Update(float deltaTime) {
    if (!isPlaying_ || presetName_.empty()) return;
    emitTimer_ += deltaTime;
    if (emitTimer_ >= emitInterval_) { EmitOnce(); emitTimer_ = fmod(emitTimer_, emitInterval_); }
}

void GPUParticleEmitter::Play() { isPlaying_ = true; emitTimer_ = emitInterval_; }
void GPUParticleEmitter::Stop() { isPlaying_ = false; emitTimer_ = 0.0f; }

void GPUParticleEmitter::EmitOnce() {
    Vector3 spawnPos = { 0, 0, 0 };
    ID3D12Resource* vb = nullptr;
    uint32_t vCount = 0; uint32_t vStride = 0; uint32_t boneSrv = 0;
    Matrix4x4 worldMat = { 1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f };

    if (targetObject_) {
        // 発生元本体にModelがない場合は、子階層の表示用Modelを使用します。
        Object3d* modelObj = FindModelObject(targetObject_);
        if (modelObj && modelObj->GetModel()) {
            worldMat = modelObj->GetWorldMatrix();
            Model* model = modelObj->GetModel();
            const auto& modelData = model->GetModelData();
            if (!modelData.meshes.empty()) {
                vb = modelData.meshes[0].vertexResource.Get();
                vCount = static_cast<uint32_t>(modelData.meshes[0].vertices.size());
                vStride = sizeof(Model::VertexData);
                boneSrv = model->GetBoneSrvIndex(); // Skinning済み発生位置の計算に使用するBone SRVです。
            }
        }
        else {
            worldMat = targetObject_->GetWorldMatrix();
        }
        Math math;
        Vector3 worldOffset = math.TransformNormal(offset_, worldMat);
        spawnPos.x = worldMat.m[3][0] + worldOffset.x;
        spawnPos.y = worldMat.m[3][1] + worldOffset.y;
        spawnPos.z = worldMat.m[3][2] + worldOffset.z;
    }
    else {
        spawnPos = offset_;
    }

    GPUParticleManager::GetInstance()->SetEmitterMesh(vb, vCount, vStride, boneSrv);
    GPUParticleManager::GetInstance()->Emit(presetName_, spawnPos, worldMat);
}
