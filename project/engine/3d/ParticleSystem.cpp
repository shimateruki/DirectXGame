#include "ParticleSystem.h"
#include "ParticleCommon.h"
#include "engine/base/DirectXCommon.h"
#include "engine/3d/TextureManager.h"
#include "engine/3d/CameraManager.h"
#include "engine/base/SRVManager.h"
#include <cassert>
#include <string>
#include <format>

// パーティクルの板ポリゴンを形成する頂点の構造体
struct ParticleVertex {
    Vector4 position;
    Vector2 texcoord;
};

void ParticleSystem::Initialize(ParticleCommon* common) {
    assert(common);
    common_ = common;
    // パーティクル用のテクスチャを読み込む 
    textureHandle_ = TextureManager::GetInstance()->Load("resouces/uvChecker.png");
    CreateResources();
    particles_.reserve(kMaxParticles);
    std::random_device seed_gen;
    randomEngine_.seed(seed_gen());
}

void ParticleSystem::Update() {
    particleCount_ = 0;
    const Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    const Matrix4x4& viewMatrix = camera->GetViewMatrix();
    Math m;

    // ビルボード計算用の行列を作成
    Matrix4x4 billboardMatrix = m.Inverse(viewMatrix);
    billboardMatrix.m[3][0] = 0.0f;
    billboardMatrix.m[3][1] = 0.0f;
    billboardMatrix.m[3][2] = 0.0f;

    for (auto it = particles_.begin(); it != particles_.end(); ) {
        // --- 寿命と移動の計算 (コメントアウトを解除) ---
        it->currentTime += 1.0f / 60.0f;
        if (it->currentTime > it->lifeTime) {
            it = particles_.erase(it);
            continue;
        }
        it->position = it->position + it->velocity * (1.0f / 60.0f);

        Matrix4x4 scaleMatrix = m.MakeScaleMatrix({ 1.0f, 1.0f, 1.0f });
        Matrix4x4 translateMatrix = m.MakeTranslateMatrix(it->position);

        // まず拡大とビルボード回転を合成
        Matrix4x4 scaleAndBillboard = m.Multiply(scaleMatrix, billboardMatrix);
        // その結果に平行移動を合成してワールド行列を完成させる
        Matrix4x4 worldMatrix = m.Multiply(scaleAndBillboard, translateMatrix);

        instancingData_[particleCount_].world = worldMatrix;
        instancingData_[particleCount_].color = it->color;

        particleCount_++;
        ++it;
    }

    const Matrix4x4& projectionMatrix = camera->GetProjectionMatrix();
    matrixData_->viewProjection = m.Multiply(viewMatrix, projectionMatrix);
}

void ParticleSystem::Draw() {
    if (particleCount_ == 0) return;

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    common_->SetPipeline(commandList);

    D3D12_VERTEX_BUFFER_VIEW vbvs[] = { vertexBufferView_, instancingBufferView_ };
    commandList->IASetVertexBuffers(0, 2, vbvs);
    commandList->IASetIndexBuffer(&indexBufferView_);

    commandList->SetGraphicsRootConstantBufferView(0, matrixResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 1, textureHandle_);

    commandList->DrawIndexedInstanced(6, particleCount_, 0, 0, 0);
}

void ParticleSystem::SpawnParticles(const Vector3& position, int count) {
    for (int i = 0; i < count; ++i) {
        if (particles_.size() < kMaxParticles) {
            particles_.push_back(CreateParticle(position));
        }
    }
}

ParticleSystem::Particle ParticleSystem::CreateParticle(const Vector3& position) {
    Particle p;
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> lifeDist(1.0f, 3.0f);
    p.position = position;
    Vector3 velocity = { dist(randomEngine_), dist(randomEngine_), dist(randomEngine_) };
    p.velocity = Math().Normalize(velocity) * 2.0f;
    p.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    p.lifeTime = lifeDist(randomEngine_);
    p.currentTime = 0.0f;
    return p;
}

void ParticleSystem::CreateResources() {
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // --- 頂点バッファ (四角形) ---
    vertexResource_ = dxCommon->CreateBufferResource(sizeof(ParticleVertex) * 4);
    vertexBufferView_ = { vertexResource_->GetGPUVirtualAddress(), sizeof(ParticleVertex) * 4, sizeof(ParticleVertex) };
    ParticleVertex* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    vertexData[0] = { { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f } }; // 左下
    vertexData[1] = { { -0.5f,  0.5f, 0.0f, 1.0f }, { 0.0f, 0.0f } }; // 左上
    vertexData[2] = { {  0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f } }; // 右下
    vertexData[3] = { {  0.5f,  0.5f, 0.0f, 1.0f }, { 1.0f, 0.0f } }; // 右上
    vertexResource_->Unmap(0, nullptr);

    // --- インデックスバッファ ---
    indexResource_ = dxCommon->CreateBufferResource(sizeof(uint32_t) * 6);
    indexBufferView_ = { indexResource_->GetGPUVirtualAddress(), sizeof(uint32_t) * 6, DXGI_FORMAT_R32_UINT };
    uint32_t* indexData = nullptr;
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    indexData[0] = 0; indexData[1] = 1; indexData[2] = 2;
    indexData[3] = 2; indexData[4] = 1; indexData[5] = 3;
    indexResource_->Unmap(0, nullptr);

    // --- インスタンシング用リソース ---
    instancingResource_ = dxCommon->CreateBufferResource(sizeof(ParticleForGPU) * kMaxParticles);
    instancingBufferView_ = { instancingResource_->GetGPUVirtualAddress(), sizeof(ParticleForGPU) * kMaxParticles, sizeof(ParticleForGPU) };
    instancingResource_->Map(0, nullptr, reinterpret_cast<void**>(&instancingData_));

    // --- カメラ行列用リソース ---
    matrixResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    matrixResource_->Map(0, nullptr, reinterpret_cast<void**>(&matrixData_));
}