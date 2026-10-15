#include "ParticleSystem.h"
#include "RenderStats.h"
#include "ParticleCommon.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "CameraManager.h"
#include "SRVManager.h"
#include "engine/utility/math/Math.h" 
#include <cassert>
#include <chrono>
#include <string>
#include <numbers>

// 乱数とMathインスタンス
static std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
static engine::Math math;

// 板ポリゴン頂点
struct ParticleVertex {
    engine::Vector4 position;
    engine::Vector2 texcoord;
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
    params_.isEmitting = false;
}

float ParticleSystem::ResolveEmissionScale(const EmitterParams& params, const engine::Vector3& origin) const {
    // LODが有効なときだけカメラ距離から放出量の倍率を評価する。
    if (!params.lod.enabled) {
        return 1.0f;
    }
    const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) {
        return 1.0f;
    }
    const engine::Vector3 eye = camera->GetEye();
    const float x = origin.x - eye.x;
    const float y = origin.y - eye.y;
    const float z = origin.z - eye.z;
    return params.lod.EvaluateEmissionScale(std::sqrt(x * x + y * y + z * z));
}

std::size_t ParticleSystem::ResolveParticleLimit(const EmitterParams& params) const {
    if (!params.lod.enabled || params.lod.maxAliveParticles <= 0) {
        return static_cast<std::size_t>(kMaxParticles);
    }
    return static_cast<std::size_t>(std::clamp(params.lod.maxAliveParticles, 1, kMaxParticles));
}

/// <summary>
/// 自動エミッターが内部で呼ぶSpawn
/// </summary>
void ParticleSystem::SpawnFromEmitter() {
    SpawnFromParams(params_, params_.spawnPosition + editorPreviewOffset_);
}

void ParticleSystem::SpawnFromParams(const EmitterParams& params, const engine::Vector3& origin) {
    if (particles_.size() >= ResolveParticleLimit(params)) return;

    // 出現形状に応じて発生位置と初速を決める。粒子の共通属性は分岐の後で埋める。
    engine::Vector3 pos = origin;
    engine::Vector3 vel = params.initialVelocity; // 基準速度

    if (params.emitterType == EmitterType::Box) {
        // Boxは基準位置の各軸へ独立した乱数を加える。
        pos.x += params.spawnArea.x * dis(randomEngine_);
        pos.y += params.spawnArea.y * dis(randomEngine_);
        pos.z += params.spawnArea.z * dis(randomEngine_);

        // Boxの速度だけは初速の各軸へ乱数成分を追加する。
        vel.x += params.velocityRandomness.x * dis(randomEngine_);
        vel.y += params.velocityRandomness.y * dis(randomEngine_);
        vel.z += params.velocityRandomness.z * dis(randomEngine_);
    } else if (params.emitterType == EmitterType::Sphere) {
        // Sphereは乱数方向を正規化して球面上の位置を決め、外向きへ飛ばす。
        engine::Vector3 dir = { dis(randomEngine_), dis(randomEngine_), dis(randomEngine_) };

        // 乱数がゼロベクトルになったときは上方向を使う。
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len != 0.0f) dir = dir / len;
        else dir = { 0, 1, 0 }; // ゼロなら上へ

        pos += dir * params.spawnRadius;

        // initialVelocityの長さを速度量として使い、方向は球面法線へ置き換える。
        float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
        if (speed == 0) speed = 1.0f; // デフォルトスピード

        vel = dir * speed;
    } else if (params.emitterType == EmitterType::Cone) {
        // Coneは上方向を基準にX/Zを傾けた方向を作る簡易的な放出形状。

        float angleRad = params.coneAngle * (3.14159f / 180.0f);
        float r = std::tan(angleRad); // 指定角度をX/Z方向の広がり幅へ変換する。

        engine::Vector3 dir;
        dir.x = dis(randomEngine_) * r;
        dir.z = dis(randomEngine_) * r;
        dir.y = 1.0f; // 上向き

        // 初速へ使うため方向を正規化する。
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (len != 0) dir = dir / len;

        // 発生点は原点から同じ方向へ0～1の範囲で散らす。
        pos += dir * (dis(randomEngine_) * 0.5f + 0.5f);

        // 初速の大きさだけ維持し、向きは放出方向へ合わせる。
        float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
        if (speed == 0) speed = 5.0f;
        vel = dir * speed;

    }


    // 共通の寿命・色・サイズと、編集用カーブの焼き込み結果を粒子へ保存する。
    Particle p;
    p.position = pos;
    p.velocity = vel;
    p.lifeTime = params.particleLifetime;
    p.currentTime = 0.0f;
    p.startColor = params.startColor;
    p.endColor = params.endColor;
    p.startSize = params.startSize;
    p.endSize = params.endSize;
    p.acceleration = params.acceleration;
    p.hdrIntensity = params.hdrIntensity;
    // 編集用カーブは両方のキーが揃っている場合だけ有効にする。
    p.useAuthoringCurves =
        params.useAuthoringCurves &&
        !params.sizeOverLife.keys.empty() &&
        !params.colorOverLife.keys.empty();
    p.sizeOverLife = BakeVFXCurve(params.sizeOverLife, params.startSize);
    p.colorOverLife = BakeVFXGradient(params.colorOverLife, params.startColor);
    p.rotation = 0.0f;
    float rotSpeedDeg = params.initialRotationSpeed + dis(randomEngine_) * params.rotationSpeedRandomness;
    p.rotationSpeed = rotSpeedDeg * (3.141592f / 180.0f);

    particles_.push_back(p);
}

/// <summary>
///手動で（単発で）発生させる関数
/// </summary>
void ParticleSystem::SpawnParticles(const engine::Vector3& position, int count,
    float initialSpeed, const engine::Vector3* direction, float spreadAngle,
    engine::Vector4 initialColor, engine::Vector4 endColor,
    float lifeTimeMin, float lifeTimeMax,
    float startSize, float endSize)
{
    // カメラ距離等の放出倍率を先に適用し、上限数を越える前に粒子を止める。
    const float emissionScale = ResolveEmissionScale(params_, position);
    if (emissionScale <= 0.0001f) {
        return;
    }
    const int scaledCount = (std::max)(
        1, static_cast<int>(std::lround(static_cast<float>(count) * emissionScale)));
    const std::size_t particleLimit = ResolveParticleLimit(params_);
    std::uniform_real_distribution<float> lifeDist(lifeTimeMin, lifeTimeMax);

    for (int i = 0; i < scaledCount; ++i) {
        if (particles_.size() >= particleLimit) {
            break; // 最大数
        }

        engine::Vector3 spawnDir;
        if (direction) { // 指定方向に二軸の角度揺れを加える。
            spawnDir = *direction;
            if (spreadAngle > 0.0f) {
                std::uniform_real_distribution<float> angleDist(-spreadAngle / 2.0f, spreadAngle / 2.0f);
                engine::Matrix4x4 rotX = math.MakeRotateXMatrix(angleDist(randomEngine_));
                engine::Matrix4x4 rotY = math.MakeRotateYMatrix(angleDist(randomEngine_));
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

        // 発生時点の属性と、寿命中に評価するカーブを一粒子分作る。
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
        // 回転速度は度からラジアンへ直してから積分に渡す。
        float rotSpeedDeg = params_.initialRotationSpeed + dis(randomEngine_) * params_.rotationSpeedRandomness;
        p.useAuthoringCurves =
            params_.useAuthoringCurves &&
            !params_.sizeOverLife.keys.empty() &&
            !params_.colorOverLife.keys.empty();
        p.sizeOverLife = BakeVFXCurve(params_.sizeOverLife, startSize);
        p.colorOverLife = BakeVFXGradient(params_.colorOverLife, initialColor);
        p.rotationSpeed = rotSpeedDeg * (3.141592f / 180.0f);
        particles_.push_back(p);
    }
}

void ParticleSystem::EmitOneShot(const EmitterParams& params, const engine::Vector3& position) {
    // カメラ距離によるLOD倍率と粒子上限を反映して単発放出数を決める。
    const float emissionScale = ResolveEmissionScale(params, position);
    if (emissionScale <= 0.0001f) {
        return;
    }
    const int scaledCount = static_cast<int>(std::lround(static_cast<float>(params.emitCount) * emissionScale));
    const int count = std::clamp(scaledCount, 1, static_cast<int>(ResolveParticleLimit(params)));
    const std::size_t particleLimit = ResolveParticleLimit(params);
    for (int i = 0; i < count && particles_.size() < particleLimit; ++i) {
        SpawnFromParams(params, position);
    }
}

/// <summary>
/// パーティクル全体の更新
/// </summary>
void ParticleSystem::Update(float deltaTime) {
    const auto cpuStart = std::chrono::high_resolution_clock::now();
    deltaTime *= simulationTimeScale_;

    // 連続放出は端数を次フレームへ持ち越し、指定秒間レートを維持する。
    if (params_.isEmitting && params_.particlesPerSecond > 0.0f) {
        // 1フレームでの発生数を計算
        const engine::Vector3 emitterPosition = params_.spawnPosition + editorPreviewOffset_;
        const float emissionScale = ResolveEmissionScale(params_, emitterPosition);
        float particlesToSpawn = params_.particlesPerSecond * emissionScale * deltaTime;
        spawnTimer_ += particlesToSpawn;

        // 整数個分だけ発生させる
        while (spawnTimer_ >= 1.0f) {
            spawnTimer_ -= 1.0f;
            SpawnFromEmitter();
        }
    }

    // カメラ行列がなければ描画姿勢を作れないため、計測値を残してここで戻る。
    const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    // カメラが無い場合の安全対策 (必要に応じて)
    if (!camera) {
        const auto cpuEnd = std::chrono::high_resolution_clock::now();
        lastUpdateCpuTimeMs_ = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
        return;
    }

    const engine::Matrix4x4& viewMatrix = camera->GetViewMatrix();
    const engine::Matrix4x4& projectionMatrix = camera->GetProjectionMatrix();

    // 定数バッファ用のVP行列更新
    // ※ matrixData_ がMapされている前提
    if (matrixData_) {
        matrixData_->viewProjection = viewMatrix * projectionMatrix;
    }

    // ビルボード行列計算 (カメラの回転を打ち消す行列)
    // ビュー行列の逆行列を作り、平行移動成分を消すことで回転成分だけ抽出
    engine::Matrix4x4 billboardMatrix = math.Inverse(viewMatrix);
    billboardMatrix.m[3][0] = 0.0f;
    billboardMatrix.m[3][1] = 0.0f;
    billboardMatrix.m[3][2] = 0.0f;
    billboardMatrix.m[3][3] = 1.0f;

    // このフレームにGPUへ渡す生存粒子数をゼロから数え直す。
    particleCount_ = 0;

    // 生存粒子だけ、寿命・色・大きさ・物理状態・GPUインスタンス行列を更新する。
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

        // 編集カーブが有効ならそちらを使い、なければ開始色と終了色を補間する。
        engine::Vector4 currentColor = p.useAuthoringCurves
            ? p.colorOverLife.Evaluate(lifeRatio)
            : engine::Vector4{
                std::lerp(p.startColor.x, p.endColor.x, lifeRatio),
                std::lerp(p.startColor.y, p.endColor.y, lifeRatio),
                std::lerp(p.startColor.z, p.endColor.z, lifeRatio),
                std::lerp(p.startColor.w, p.endColor.w, lifeRatio)
            };

        currentColor.x *= p.hdrIntensity;
        currentColor.y *= p.hdrIntensity;
        currentColor.z *= p.hdrIntensity;

        // サイズも新しいカーブを優先し、旧形式の固定10点グラフへ戻せるようにする。
        // 旧形式の10点サイズカーブを寿命の進行率から選びます。
        int graphIndex = (int)(lifeRatio * 9.0f);
        if (graphIndex < 0) graphIndex = 0;
        if (graphIndex > 9) graphIndex = 9;

        // 新形式のカーブが有効ならそれを優先し、旧形式の固定10点へフォールバックします。
        float currentSize = p.useAuthoringCurves
            ? p.sizeOverLife.Evaluate(lifeRatio)
            : params_.sizeCurve[graphIndex];
        p.velocity.x += p.acceleration.x * deltaTime;
        p.velocity.y += p.acceleration.y * deltaTime;
        p.velocity.z += p.acceleration.z * deltaTime;
        // 加速度を速度へ加えた後、その速度で位置を積分する。
        p.position.x += p.velocity.x * deltaTime;
        p.position.y += p.velocity.y * deltaTime;
        p.position.z += p.velocity.z * deltaTime;

        // 回転速度を秒単位で積分し、ビルボード面内の向きに反映します。
        p.rotation += p.rotationSpeed * deltaTime;


        // 粒子面をカメラへ向けたまま、その面内だけで回転させる。
        engine::Matrix4x4 scaleMatrix = math.MakeScaleMatrix({ currentSize * p.baseScale.x, currentSize * p.baseScale.y, currentSize });

        // ビルボード回転より前に Z 回転を置き、常にカメラへ向いた面の中で回します。
        engine::Matrix4x4 rotateMatrix = math.MakeRotateZMatrix(p.rotation);

        engine::Matrix4x4 translateMatrix = math.MakeTranslateMatrix(p.position);

        // 行列の順序はサイズ、面内回転、カメラ向き、配置位置。
        engine::Matrix4x4 worldMatrix = scaleMatrix * rotateMatrix * billboardMatrix * translateMatrix;


        // CPU側に粒子が残っていても、GPUの固定インスタンス容量は越えない。
        if (particleCount_ < kMaxParticles) {
            instancingData_[particleCount_].world = worldMatrix;
            instancingData_[particleCount_].color = currentColor;

            // カウントアップ
            particleCount_++;
        }

        ++it;
    }
    const auto cpuEnd = std::chrono::high_resolution_clock::now();
    lastUpdateCpuTimeMs_ = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
}

void ParticleSystem::Draw() {
    if (particleCount_ == 0) {
        lastDrawCpuTimeMs_ = 0.0f;
        return;
    }

    const auto cpuStart = std::chrono::high_resolution_clock::now();

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    DirectXCommon* dxCommon = common_->GetDxCommon();
    dxCommon->StartGpuProfile("Particle CPU pass");
    common_->SetPipeline(commandList, params_.blendMode);

    // 共通の四角形メッシュと、Updateで詰めた粒子ごとの姿勢・色をインスタンス描画する。
    D3D12_VERTEX_BUFFER_VIEW vbvs[] = { vertexBufferView_, instancingBufferView_ };
    commandList->IASetVertexBuffers(0, 2, vbvs);
    commandList->IASetIndexBuffer(&indexBufferView_);

    commandList->SetGraphicsRootConstantBufferView(0, matrixResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 1, textureHandle_);

    commandList->DrawIndexedInstanced(6, particleCount_, 0, 0, 0);
    dxCommon->EndGpuProfile("Particle CPU pass");
    engine::RenderStats::GetInstance()->RecordIndexedDraw(6, particleCount_, 2);
    engine::RenderStats::GetInstance()->RecordCpuParticles(particleCount_);

    const auto cpuEnd = std::chrono::high_resolution_clock::now();
    lastDrawCpuTimeMs_ = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
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
    spawnTimer_ = 0.0f;
}

void ParticleSystem::ResetSimulation(uint32_t randomSeed) {
    Clear();
    randomEngine_.seed(randomSeed);
}

size_t ParticleSystem::GetEstimatedMemoryBytes() const {
    // パーティクル配列と描画用リソースから診断表示向けの使用量を概算する。
    size_t bytes = particles_.capacity() * sizeof(Particle);
    ID3D12Resource* resources[] = {
        vertexResource_.Get(),
        indexResource_.Get(),
        instancingResource_.Get(),
        matrixResource_.Get()
    };
    for (ID3D12Resource* resource : resources) {
        if (resource) {
            bytes += static_cast<size_t>(resource->GetDesc().Width);
        }
    }
    return bytes;
}

void ParticleSystem::SetTexture(const std::string& texturePath) {
    // パラメータに名前を記憶
    params_.textureName = texturePath;

    // テクスチャ読み込み (TextureManagerが重複ロードを防止してくれる前提)
    textureHandle_ = TextureManager::GetInstance()->Load(texturePath);
}

void ParticleSystem::SpawnPrimitiveHitEffect(const engine::Vector3& position) {
    std::uniform_real_distribution<float> angleDist(0.0f, 3.141592f);
    float baseRotation = angleDist(randomEngine_);

    // ① 星型の閃光 (4本のラインで十字を作る)
    int lineCount = 4;
    for (int i = 0; i < lineCount; ++i) {
        if (particles_.size() >= kMaxParticles) break;

        Particle p;
        p.position = position;
        p.velocity = { 0.0f, 0.0f, 0.0f }; // 固定
        p.lifeTime = 0.15f;
        p.currentTime = 0.0f;

        p.startColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // 真っ白
        p.endColor = { 1.0f, 1.0f, 1.0f, 0.0f };

        p.startSize = 0.5f;
        p.endSize = 6.0f;   // 大きく広げる

        p.rotation = baseRotation + i * (3.141592f / lineCount);
        p.rotationSpeed = 0.0f;
        p.acceleration = { 0.0f, 0.0f, 0.0f };
        p.hdrIntensity = 1.5f;

        p.baseScale = { 0.04f, 1.0f }; // 細長く

        particles_.push_back(p);
    }

        // ★ ②の中心のまるフラッシュは削除しました
}

// ==============================================================
// ★課題エフェクト①: ランダムZ回転で星型ヒットエフェクト
//   楕円パーティクルを8個、-π〜πのランダム回転で配置 → 星型/閃光
// ==============================================================
void ParticleSystem::SpawnStarHitEffect(const engine::Vector3& position) {
    // Z回転をランダムに (-π〜π)
    std::uniform_real_distribution<float> distRotate(
        -std::numbers::pi_v<float>, std::numbers::pi_v<float>);
    // 縦方向スケールをランダムに (1.0〜3.5) ← 大きめ
    std::uniform_real_distribution<float> distScale(1.0f, 3.5f);

    constexpr int kCount = 8;

    for (int i = 0; i < kCount; ++i) {
        if (particles_.size() >= kMaxParticles) break;

        Particle p;
        p.position    = position;
        p.velocity    = { 0.0f, 0.0f, 0.0f };
        p.lifeTime    = 0.25f;
        p.currentTime = 0.0f;

        p.startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        p.endColor   = { 1.0f, 1.0f, 1.0f, 0.0f };

        p.startSize  = 1.5f;   // ← 大きく
        p.endSize    = 14.0f;  // ← かなり大きく広がる

        p.rotation      = distRotate(randomEngine_);
        p.rotationSpeed = 0.0f;
        p.acceleration  = { 0.0f, 0.0f, 0.0f };
        p.hdrIntensity  = 5.0f; // ← 強発光

        p.baseScale = { 0.04f, distScale(randomEngine_) };

        particles_.push_back(p);
    }
}

// ==============================================================
// ★課題エフェクト②: ランダムY-scaleで斬撃エフェクト
//   縦長パーティクルを3個、Yスケールランダム → 斬撃/スラッシュ
// ==============================================================
void ParticleSystem::SpawnSlashEffect(const engine::Vector3& position, float baseRotation) {
    // 縦方向スケールをランダムに (1.5〜4.0) ← 大きめ
    std::uniform_real_distribution<float> distScale(1.5f, 4.0f);
    // 回転のゆらぎ
    std::uniform_real_distribution<float> distRotJitter(-0.4f, 0.4f);

    constexpr int kCount = 3;

    for (int i = 0; i < kCount; ++i) {
        if (particles_.size() >= kMaxParticles) break;

        Particle p;
        p.position    = position;
        p.velocity    = { 0.0f, 0.0f, 0.0f };
        p.lifeTime    = 0.28f;
        p.currentTime = 0.0f;

        p.startColor = { 0.8f, 0.9f, 1.0f, 1.0f };
        p.endColor   = { 0.2f, 0.3f, 1.0f, 0.0f };

        p.startSize  = 1.2f;   // ← 大きく
        p.endSize    = 12.0f;  // ← かなり大きく

        float randomY   = distScale(randomEngine_);
        p.baseScale     = { 0.06f, randomY }; // X:細い線, Y:ランダム長さ

        p.rotation      = baseRotation + (i * (3.14159f / 3.0f)) + distRotJitter(randomEngine_);
        p.rotationSpeed = 0.0f;
        p.acceleration  = { 0.0f, 0.0f, 0.0f };
        p.hdrIntensity  = 6.0f; // ← 強い刃の輝き

        particles_.push_back(p);
    }
}
