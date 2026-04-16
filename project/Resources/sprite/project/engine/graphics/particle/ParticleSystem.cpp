#include "ParticleSystem.h"
#include "ParticleCommon.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "CameraManager.h"
#include "SRVManager.h"
#include "engine/utility/math/Math.h" 
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
    if (params_.textureName.empty()) {
        params_.textureName = texturePath;
    }
    SetTexture(params_.textureName); // ここでロード＆ハンドル取得

    assert(textureHandle_ != 0);
    CreateResources();
    particles_.reserve(kMaxParticles); 
    std::random_device seed_gen;
    randomEngine_.seed(seed_gen());

    // Emitterの初期設定
    spawnTimer_ = 0.0f;
    params_.isEmitting = true;
}

/// <summary>
/// 自動エミッターが内部で呼ぶSpawn
/// </summary>
void ParticleSystem::SpawnFromEmitter() {
    if (particles_.size() >= kMaxParticles) return;

    // ---------------------------------------------------
    // ★形状ごとの計算分岐
    // ---------------------------------------------------
    Vector3 pos = params_.spawnPosition;
    Vector3 vel = params_.initialVelocity; // 基準速度

    if (params_.emitterType == EmitterType::Box) {
        // --- 1. Box (従来通り) ---
        pos.x += params_.spawnArea.x * dis(gen);
        pos.y += params_.spawnArea.y * dis(gen);
        pos.z += params_.spawnArea.z * dis(gen);

        // 速度にはランダム性を足す
        vel.x += params_.velocityRandomness.x * dis(gen);
        vel.y += params_.velocityRandomness.y * dis(gen);
        vel.z += params_.velocityRandomness.z * dis(gen);
    } else if (params_.emitterType == EmitterType::Sphere) {
        // --- 2. Sphere (球体) ---
        // ランダムな方向ベクトルを作成
        Vector3 dir = { dis(gen), dis(gen), dis(gen) };

        // 長さが0にならないようにチェックして正規化
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len != 0.0f) dir = dir / len;
        else dir = { 0, 1, 0 }; // ゼロなら上へ

        // 位置: 中心から半径分ずらす (中身を埋めるなら * dis(gen) ではなく * abs(dis(gen)) など工夫)
        pos += dir * params_.spawnRadius;

        // 速度: 中心から外側に向かって飛ぶ (爆発)
        // initialVelocity.z を「スピード」として扱うと使いやすいです
        float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
        if (speed == 0) speed = 1.0f; // デフォルトスピード

        vel = dir * speed;
    } else if (params_.emitterType == EmitterType::Cone) {
        // --- 3. Cone (円錐) ---
        // Y軸(上)向きを基準に、角度分だけランダムに傾ける計算が必要
        // 簡易実装: XとZをランダムにずらして正規化

        float angleRad = params_.coneAngle * (3.14159f / 180.0f);
        float r = std::tan(angleRad); // 広がり具合

        Vector3 dir;
        dir.x = dis(gen) * r;
        dir.z = dis(gen) * r;
        dir.y = 1.0f; // 上向き

        // 正規化
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len != 0) dir = dir / len;

        // 位置: 原点から出るか、少し円状に広げるか
        pos += dir * (dis(gen) * 0.5f + 0.5f); // 少しばらつかせる

        // 速度: 方向 * スピード
        float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
        if (speed == 0) speed = 5.0f;
        vel = dir * speed;

    }


    Particle p;
    p.position = pos;
    p.velocity = vel;
    p.lifeTime = params_.particleLifetime;
    p.currentTime = 0.0f;
    p.startColor = params_.startColor;
    p.endColor = params_.endColor;
    p.startSize = params_.startSize;
    p.endSize = params_.endSize;
    p.acceleration = params_.acceleration;
    p.hdrIntensity = params_.hdrIntensity;
    // 回転初期化 (前回の実装分)
    p.rotation = 0.0f;
    float rotSpeedDeg = params_.initialRotationSpeed + dis(gen) * params_.rotationSpeedRandomness;
    p.rotationSpeed = rotSpeedDeg * (3.141592f / 180.0f);

    particles_.push_back(p);
}

/// <summary>
///手動で（単発で）発生させる関数
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

        //Particle 構造体に直接詰める
        Particle p;
        p.position = position;
        p.velocity = spawnDir * initialSpeed;
        p.lifeTime = lifeDist(randomEngine_);
        p.currentTime = 0.0f;
        p.startColor = initialColor;
        p.endColor = endColor;
        p.startSize = startSize;
        p.endSize = endSize;
        p.rotation = 0.0f; 
        p.acceleration = params_.acceleration;
        p.hdrIntensity = params_.hdrIntensity;
    // 回転スピード決定 (度数法 -> ラジアン変換)
    float rotSpeedDeg = params_.initialRotationSpeed + dis(gen) * params_.rotationSpeedRandomness;
    p.rotationSpeed = rotSpeedDeg * (3.141592f / 180.0f);
        particles_.push_back(p);
    }
}
void ParticleSystem::EmitOneShot(const EmitterParams& params, const Vector3& position) {
    // 上限チェック
    if (particles_.size() >= kMaxParticles) return;

    Particle p;
    // 初期状態
    p.currentTime = 0.0f;

    // 寿命計算
    // float lifeRandom = dis(gen) * params.lifeTimeRandomness; 
    p.lifeTime = params.particleLifetime; // 単純化

    // 位置計算
    Vector3 offset;
    offset.x = dis(gen) * params.spawnArea.x;
    offset.y = dis(gen) * params.spawnArea.y;
    offset.z = dis(gen) * params.spawnArea.z;
    p.position = position + offset; // 引数のpositionを基準にする
    
    // 速度計算
    p.velocity.x = params.initialVelocity.x + dis(gen) * params.velocityRandomness.x;
    p.velocity.y = params.initialVelocity.y + dis(gen) * params.velocityRandomness.y;
    p.velocity.z = params.initialVelocity.z + dis(gen) * params.velocityRandomness.z;

    // 色とサイズ
    p.startColor = params.startColor;
    p.endColor = params.endColor;
    p.startSize = params.startSize;
    p.endSize = params.endSize;
    p.rotation = 0.0f; // 最初は0度から (ランダムにしてもOK)
    p.acceleration = params_.acceleration;
    p.hdrIntensity = params_.hdrIntensity;
    // 回転スピード決定 (度数法 -> ラジアン変換して保存)
    // 3.1415... / 180.0f = 0.01745...
    float rotSpeedDeg = params.initialRotationSpeed + dis(gen) * params.rotationSpeedRandomness;
    p.rotationSpeed = rotSpeedDeg * (3.141592f / 180.0f);

    // リストに追加
    particles_.push_back(p);
}




/// <summary>
/// パーティクル全体の更新
/// </summary>
void ParticleSystem::Update(float deltaTime) {

    // --- 1. エミッター（自動発生）の処理 ---
    if (params_.isEmitting && params_.particlesPerSecond > 0.0f) {
        // 1フレームでの発生数を計算
        float particlesToSpawn = params_.particlesPerSecond * deltaTime;
        spawnTimer_ += particlesToSpawn;

        // 整数個分だけ発生させる
        while (spawnTimer_ >= 1.0f) {
            spawnTimer_ -= 1.0f;
            SpawnFromEmitter();
        }
    }

    // --- 2. 行列などの事前計算 ---

    // カメラ情報取得
    const Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    // カメラが無い場合の安全対策 (必要に応じて)
    if (!camera) return;

    const Matrix4x4& viewMatrix = camera->GetViewMatrix();
    const Matrix4x4& projectionMatrix = camera->GetProjectionMatrix();

    // 定数バッファ用のVP行列更新
    // ※ matrixData_ がMapされている前提
    if (matrixData_) {
        matrixData_->viewProjection = viewMatrix * projectionMatrix;
    }

    // ビルボード行列計算 (カメラの回転を打ち消す行列)
    // ビュー行列の逆行列を作り、平行移動成分を消すことで回転成分だけ抽出
    Matrix4x4 billboardMatrix = math.Inverse(viewMatrix);
    billboardMatrix.m[3][0] = 0.0f;
    billboardMatrix.m[3][1] = 0.0f;
    billboardMatrix.m[3][2] = 0.0f;
    billboardMatrix.m[3][3] = 1.0f;

    // GPUに送るカウントをリセット
    particleCount_ = 0;

    // --- 3. パーティクル個別の更新ループ ---
    for (auto it = particles_.begin(); it != particles_.end(); ) {
        Particle& p = *it;
        p.currentTime += deltaTime;

        // 寿命尽きチェック
        if (p.currentTime >= p.lifeTime) {
            it = particles_.erase(it); // 寿命で削除
            continue;
        }

        // 寿命の進行度 (0.0:生まれたて -> 1.0:死ぬ直前)
        float lifeRatio = p.currentTime / p.lifeTime;
        if (lifeRatio > 1.0f) lifeRatio = 1.0f;

        // --- カラーの更新 ---
        // Lerpで開始色～終了色を補間
        Vector4 currentColor;
        currentColor.x = std::lerp(p.startColor.x, p.endColor.x, lifeRatio);
        currentColor.y = std::lerp(p.startColor.y, p.endColor.y, lifeRatio);
        currentColor.z = std::lerp(p.startColor.z, p.endColor.z, lifeRatio);
        currentColor.w = std::lerp(p.startColor.w, p.endColor.w, lifeRatio);

        currentColor.x *= p.hdrIntensity;
        currentColor.y *= p.hdrIntensity;
        currentColor.z *= p.hdrIntensity;

        // --- サイズ更新 (グラフ適用) ---
        // 配列要素数10と仮定してインデックス計算 (0~9)
        int graphIndex = (int)(lifeRatio * 9.0f);
        if (graphIndex < 0) graphIndex = 0;
        if (graphIndex > 9) graphIndex = 9;

        // エディタで編集したカーブの値をサイズとして採用
        // ※ params_.sizeCurve が float配列[10] である前提
        float currentSize = params_.sizeCurve[graphIndex];
        p.velocity.x += p.acceleration.x * deltaTime;
        p.velocity.y += p.acceleration.y * deltaTime;
        p.velocity.z += p.acceleration.z * deltaTime;
        // --- 物理更新 ---
        // 速度による位置更新
        p.position.x += p.velocity.x * deltaTime;
        p.position.y += p.velocity.y * deltaTime;
        p.position.z += p.velocity.z * deltaTime;

        // --- 回転更新 (★今回の追加) ---
        p.rotation += p.rotationSpeed * deltaTime;


        // --- 行列計算 ---

        // 1. スケール行列
        Matrix4x4 scaleMatrix = math.MakeScaleMatrix({ currentSize, currentSize, currentSize });

        // 2. 回転行列 (★今回の追加: Z軸回転)
        // ビルボード面の上でクルクル回る動きを作ります
        Matrix4x4 rotateMatrix = math.MakeRotateZMatrix(p.rotation);

        // 3. 平行移動行列
        Matrix4x4 translateMatrix = math.MakeTranslateMatrix(p.position);

        // 4. ワールド行列の合成
        // 順序: スケール -> Z回転 -> ビルボード回転(カメラ向き) -> 平行移動(位置)
        // これで「カメラを向きながら、自分の中心で回る」ことができます
        Matrix4x4 worldMatrix = scaleMatrix * rotateMatrix * billboardMatrix * translateMatrix;


        // --- GPUバッファへの書き込み ---

        // 最大数を超えて書き込まないようにチェック
        if (particleCount_ < kMaxParticles) {
            instancingData_[particleCount_].world = worldMatrix;
            instancingData_[particleCount_].color = currentColor;

            // カウントアップ
            particleCount_++;
        }

        ++it;
    }
}
void ParticleSystem::Draw() {
    if (particleCount_ == 0) return;

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    common_->SetPipeline(commandList, params_.blendMode);

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

void ParticleSystem::SetTexture(const std::string& texturePath) {
    // パラメータに名前を記憶
    params_.textureName = texturePath;

    // テクスチャ読み込み (TextureManagerが重複ロードを防止してくれる前提)
    textureHandle_ = TextureManager::GetInstance()->Load(texturePath);
}