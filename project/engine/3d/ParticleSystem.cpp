#include "ParticleSystem.h"
#include "ParticleCommon.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "CameraManager.h"
#include "SRVManager.h"
#include"Sprite.h"
#include <cassert>
#include <string>
#include <format>

// パーティクルの板ポリゴンを形成する頂点の構造体
struct ParticleVertex {
    Vector4 position;
    Vector2 texcoord;
};

void ParticleSystem::Initialize(ParticleCommon* common, const std::string& texturePath) {
    assert(common);
    common_ = common;

    textureHandle_ = TextureManager::GetInstance()->Load(texturePath);

    assert(textureHandle_ != 0); // 読み込みチェック
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
    char buffer[128]; // 文字列バッファ
    // sprintf_s を使ってフォーマット文字列を作成
    sprintf_s(buffer, sizeof(buffer), "ParticleSystem::Update - Active particleCount_: %u\n", particleCount_);
    // 出力ウィンドウに表示
    OutputDebugStringA(buffer);
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

void ParticleSystem::SpawnParticles(const Vector3& position, int count,
    float initialSpeed, const Vector3* direction, float spreadAngle,
    Vector4 initialColor, float lifeTimeMin, float lifeTimeMax)
{

    std::uniform_real_distribution<float> lifeDist(lifeTimeMin, lifeTimeMax);
    Math m;

    for (int i = 0; i < count; ++i) {
        if (particles_.size() < kMaxParticles) {
            Vector3 spawnDir;
            if (direction) { // 方向指定あり
                spawnDir = *direction;
                // ばらつきを追加
                if (spreadAngle > 0.0f) {
                    std::uniform_real_distribution<float> angleDist(-spreadAngle / 2.0f, spreadAngle / 2.0f);
                    float randomAngleX = angleDist(randomEngine_);
                    float randomAngleY = angleDist(randomEngine_);
                    Matrix4x4 rotX = m.MakeRotateXMatrix(randomAngleX);
                    Matrix4x4 rotY = m.MakeRotateYMatrix(randomAngleY);
                    spawnDir = m.TransformNormal(spawnDir, rotX);
                    spawnDir = m.TransformNormal(spawnDir, rotY);
                    spawnDir = m.Normalize(spawnDir); // 再正規化
                }
            } else { // 方向指定なし (ランダム)
                std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
                spawnDir = { dist(randomEngine_), dist(randomEngine_), dist(randomEngine_) };

                // ▼▼▼ ゼロベクトル対策 (Math クラスの LengthSq を使う形に修正) ▼▼▼
                // m.LengthSq(spawnDir) でベクトルの長さの2乗を取得
                while (m.Length(spawnDir) < 0.001f) {
                    // ほぼゼロベクトルなら作り直す
                    spawnDir = { dist(randomEngine_), dist(randomEngine_), dist(randomEngine_) };
                }
                spawnDir = m.Normalize(spawnDir); // 速度を計算する前に正規化
                // ▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲▲
            }
            float life = lifeDist(randomEngine_);
            particles_.push_back(CreateParticle(position, initialSpeed, spawnDir, initialColor, life));
        } else {
            // OutputDebugStringA("Warning: kMaxParticles reached!\n");
            break;
        }
    }
}


// CreateParticle の実装を修正
ParticleSystem::Particle ParticleSystem::CreateParticle(const Vector3& position, float speed, const Vector3& dir,
    const Vector4& color, float life)
{
    Particle p;
    p.position = position;
    p.velocity = dir * speed; // ★ 引数で受け取った方向と速度を使う
    p.color = color;          // ★ 引数で受け取った色を使う
    p.lifeTime = life;        // ★ 引数で受け取った寿命を使う
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

// ParticleSystem.cpp の最後などに追加
void ParticleSystem::Clear() {
    particles_.clear(); // list の中身を全部消す
    particleCount_ = 0; // カウントもリセット
}