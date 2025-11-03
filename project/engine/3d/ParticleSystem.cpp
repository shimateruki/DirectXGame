#include "ParticleSystem.h"
#include "ParticleCommon.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "CameraManager.h" // カメラ行列のため
#include "SRVManager.h"
#include "engine/base/Math.h" // Math のため
#include <cassert>
#include <string>

// 乱数とMathインスタンス
static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
static Math math;

// 板ポリゴン頂点
struct ParticleVertex {
    Vector4 position;
    Vector2 texcoord;
};

void ParticleSystem::Initialize(ParticleCommon* common, const std::string& texturePath) {
    assert(common);
    common_ = common;

    textureHandle_ = TextureManager::GetInstance()->Load(texturePath);

    assert(textureHandle_ != 0);
    CreateResources();
    particles_.reserve(kMaxParticles); // vector::reserve
    std::random_device seed_gen;
    randomEngine_.seed(seed_gen());

    // Emitterの初期設定
    spawnTimer_ = 0.0f;
    params_.isEmitting = true; // デフォルトでON
}

/// <summary>
/// 【使い方B】自動エミッターが内部で呼ぶSpawn
/// </summary>
void ParticleSystem::SpawnFromEmitter() {
    if (particles_.size() >= kMaxParticles) {
        return; // 最大数
    }

    // params_ に基づいてランダムな値を決定
    Vector3 pos = params_.spawnPosition;
    pos.x += params_.spawnArea.x * dis(gen);
    pos.y += params_.spawnArea.y * dis(gen);
    pos.z += params_.spawnArea.z * dis(gen);

    Vector3 vel = params_.initialVelocity;
    vel.x += params_.velocityRandomness.x * dis(gen);
    vel.y += params_.velocityRandomness.y * dis(gen);
    vel.z += params_.velocityRandomness.z * dis(gen);

    // ★ Particle 構造体に直接詰める
    Particle p;
    p.position = pos;
    p.velocity = vel;
    p.lifeTime = params_.particleLifetime;
    p.currentTime = 0.0f;
    p.startColor = params_.startColor;
    p.endColor = params_.endColor;
    p.startSize = params_.startSize;
    p.endSize = params_.endSize;

    particles_.push_back(p);
}


/// <summary>
/// 【使い方A】手動で（単発で）発生させる関数
/// </summary>
void ParticleSystem::SpawnParticles(const Vector3& position, int count,
    float initialSpeed, const Vector3* direction, float spreadAngle,
    Vector4 initialColor, Vector4 endColor, 
    float lifeTimeMin, float lifeTimeMax,
    float startSize, float endSize)       
{
    std::uniform_real_distribution<float> lifeDist(lifeTimeMin, lifeTimeMax);

    for (int i = 0; i < count; ++i) {
        if (particles_.size() >= kMaxParticles) {
            break; // 最大数
        }

        Vector3 spawnDir;
        if (direction) { // 方向指定あり
            spawnDir = *direction;
            // ... (既存のばらつき計算) ...
            if (spreadAngle > 0.0f) {
                std::uniform_real_distribution<float> angleDist(-spreadAngle / 2.0f, spreadAngle / 2.0f);
                Matrix4x4 rotX = math.MakeRotateXMatrix(angleDist(randomEngine_));
                Matrix4x4 rotY = math.MakeRotateYMatrix(angleDist(randomEngine_));
                spawnDir = math.TransformNormal(spawnDir, rotX * rotY);
            }
        } else { // 方向指定なし (ランダム)
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            spawnDir = { dist(randomEngine_), dist(randomEngine_), dist(randomEngine_) };
        }

        if (math.Length(spawnDir) < 0.001f) {
            spawnDir = { 0.0f, 1.0f, 0.0f }; // ゼロベクトル対策
        }
        spawnDir = math.Normalize(spawnDir);

        // ★ Particle 構造体に直接詰める
        Particle p;
        p.position = position;
        p.velocity = spawnDir * initialSpeed;
        p.lifeTime = lifeDist(randomEngine_);
        p.currentTime = 0.0f;
        p.startColor = initialColor;
        p.endColor = endColor;
        p.startSize = startSize;
        p.endSize = endSize;

        particles_.push_back(p);
    }
}


/// <summary>
/// パーティクル全体の更新
/// </summary>
void ParticleSystem::Update(float deltaTime) {

    // --- 1. エミッター（自動発生）の処理 ---
    if (params_.isEmitting && params_.particlesPerSecond > 0.0f) {

        float particlesToSpawn = params_.particlesPerSecond * deltaTime;
        spawnTimer_ += particlesToSpawn;

        while (spawnTimer_ >= 1.0f) {
            spawnTimer_ -= 1.0f;
            SpawnFromEmitter();
        }
    }

    // --- 2. パーティクル（個々）の更新 (★ここが重要) ---

    // (↓ 既存の Update() から持ってきたカメラ情報)
    const Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    const Matrix4x4& viewMatrix = camera->GetViewMatrix();
    const Matrix4x4& projectionMatrix = camera->GetProjectionMatrix();
    // (↓ CBV[0] にカメラ行列をセット)
    matrixData_->viewProjection = viewMatrix * projectionMatrix;

    // (↓ 既存の Update() から持ってきたビルボード行列)
    Matrix4x4 backToFrontMatrix = math.Inverse(viewMatrix);
    backToFrontMatrix.m[3][0] = 0.0f;
    backToFrontMatrix.m[3][1] = 0.0f;
    backToFrontMatrix.m[3][2] = 0.0f;

    // GPUに送るカウントをリセット
    particleCount_ = 0;

    for (auto it = particles_.begin(); it != particles_.end(); ) {
        Particle& p = *it;
        p.currentTime += deltaTime;

        if (p.currentTime >= p.lifeTime) {
            it = particles_.erase(it); // 寿命で削除
            continue;
        }

        // ★ 補間計算 (p が持つ start/end データを使う)
        float lifeRatio = p.currentTime / p.lifeTime;
        Vector4 currentColor = math.Lerp(p.startColor, p.endColor, lifeRatio);
        float currentSize = math.Lerp(p.startSize, p.endSize, lifeRatio);

        // ★ 速度を反映
        p.position += p.velocity * deltaTime;

        // ▼▼▼ ★★★ "消えていた" 処理 ★★★ ▼▼▼
        //
        // --- 3. Instancingデータへの書き込み ---
        // (↓ 既存の Update() から持ってきた行列計算)

        // スケール行列
        Matrix4x4 scaleMatrix = math.MakeScaleMatrix({ currentSize, currentSize, currentSize });
        // トランスフォーム行列
        Matrix4x4 translateMatrix = math.MakeTranslateMatrix(p.position);

        // ワールド行列の計算 (ビルボード対応)
        Matrix4x4 worldMatrix = scaleMatrix * backToFrontMatrix * translateMatrix;

        // ★ GPUバッファ (instancingData_) にデータをコピー
        instancingData_[particleCount_].world = worldMatrix;
        instancingData_[particleCount_].color = currentColor;

        // ★ カウントアップ (これが 0 のままだと Draw() が動かない)
        particleCount_++;
        //
        // ▲▲▲ ★★★ "消えていた" 処理 ★★★ ▲▲▲

        ++it;
    }
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

void ParticleSystem::Clear() {
    particles_.clear();
    particleCount_ = 0;
}