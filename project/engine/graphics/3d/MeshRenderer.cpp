#include "MeshRenderer.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "LightManager.h"
#include <cassert>
#include <SrvManager.h>

MeshRenderer::MeshRenderer(Transform* transform) {
    assert(transform);
    transform_ = transform;
}

void MeshRenderer::Initialize(Object3dCommon* common) {
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // 1. WVPバッファ
    wvpResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    wvpData_->WVP = Math::MakeIdentity4x4();
    wvpData_->world = Math::MakeIdentity4x4();



    // 3. Cameraバッファ
    cameraResource_ = dxCommon->CreateBufferResource(sizeof(CameraForGPU));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));
    cameraData_->worldPosition = { 0.0f, 0.0f, 0.0f };

    // 4. Materialバッファ
    materialResource_ = dxCommon->CreateBufferResource(sizeof(MaterialData));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = 1;
    materialData_->uvTransform = Math::MakeIdentity4x4();
    materialData_->selectedLighting = 2;
    materialData_->shininess = 20.0f;
    materialData_->materialType = 0;
    materialData_->roughness = 0.5f; // 程よくザラザラ（光沢が広がる）
    materialData_->metallic = 0.0f;  // 非金属（景色を反射しない）
    materialData_->enableNormalMap = 0;
    materialData_->enableEnvMap = 0;     // デフォルトoff
    materialData_->envIntensity = 1.0f;  // デフォルト1.0倍
    materialData_->emissive = 1.0f;
    shadowWvpResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    shadowWvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&shadowWvpData_));
    shadowWvpData_->WVP = Math::MakeIdentity4x4();
    shadowWvpData_->world = Math::MakeIdentity4x4();

    localFogResource_ = dxCommon->CreateBufferResource(sizeof(LocalFogData));
    localFogResource_->Map(0, nullptr, reinterpret_cast<void**>(&localFogData_));
    localFogData_->fogColor = { 0.2f, 0.8f, 0.5f, 1.0f }; // 毒沼カラー
    localFogData_->fogDensity = 0.5f;

    waterParamResource_ = dxCommon->CreateBufferResource(sizeof(WaterParamForGPU));
    waterParamResource_->Map(0, nullptr, reinterpret_cast<void**>(&waterParamData_));
    // デフォルト値のセット
    waterParamData_->time = 0.0f;
    waterParamData_->waveSpeed = 2.0f;
    waterParamData_->waveHeight = 0.5f;
    waterParamData_->waveFrequency = 1.5f;
    waterParamData_->flowSpeedX = 0.1f; // 緩やかに流れる
    waterParamData_->flowSpeedY = 0.1f;
    waterParamData_->effectType = 0.0f;
    waterParamData_->effectScale = 1.0f;
    waterParamData_->effectSoftness = 0.55f;
    waterParamData_->effectIntensity = 1.0f;
    waterParamData_->cameraWorldPosition = { 0.0f, 0.0f, -1.0f };
    waterParamData_->billboardScale = 0.55f;
    
}

void MeshRenderer::Update() {
	// 経過時間を更新してGPUに転送
    time_ += 1.0f / 60.0f;

    if (waterParamData_) {
        waterParamData_->time = time_; 

        // ★流速に基づいてオフセットを蓄積）
        waterParamData_->uvOffsetX += waterParamData_->flowSpeedX * (1.0f / 60.0f);
        waterParamData_->uvOffsetY += waterParamData_->flowSpeedY * (1.0f / 60.0f);
    }
    if (localFogData_) {
        localFogData_->time = time_;
        auto& sun = LightManager::GetInstance()->GetDirectionalLight();
        localFogData_->lightDirection = sun.direction;

        // 光の色に「輝度(intensity)」を掛け合わせて、より強い光にする
        localFogData_->lightColor = {
            sun.color.x * sun.intensity,
            sun.color.y * sun.intensity,
            sun.color.z * sun.intensity
        };

    }
    // Transformの計算結果 (matWorld) をGPUに転送する
    if (wvpData_ && transform_) {
        Math math;
        Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

        if (camera) {
            const Matrix4x4& view = camera->GetViewMatrix();
            const Matrix4x4& proj = camera->GetProjectionMatrix();
            Matrix4x4 viewProj = math.Multiply(view, proj);

            // Transform側ですでに計算されたワールド行列を使う
            const Matrix4x4& worldMatrix = transform_->matWorld;

            wvpData_->WVP = math.Multiply(worldMatrix, viewProj);
            wvpData_->world = worldMatrix;
            wvpData_->WorldInverseTranspose = math.Transpose(math.Inverse(worldMatrix));
            cameraData_->worldPosition = camera->GetEye();
            if (waterParamData_) {
                waterParamData_->cameraWorldPosition = camera->GetEye();
            }
            localFogData_->cameraPos = camera->GetEye();
            
            // 軽量化: ViewProjの逆行列はカメラ共通なのでキャッシュする
            static Matrix4x4 lastVP;
            static Matrix4x4 cachedInvVP;
            if (std::memcmp(&lastVP, &viewProj, sizeof(Matrix4x4)) != 0) {
                lastVP = viewProj;
                cachedInvVP = math.Inverse(viewProj);
            }
            localFogData_->inverseViewProj = cachedInvVP;
        } else {
            wvpData_->WVP = Math::MakeIdentity4x4();
            wvpData_->world = Math::MakeIdentity4x4();
        }
        if (isUIPreview_) {
            if (shadowWvpData_) {
                // 影マップの影響を受けないように、行列を初期化しておく
                shadowWvpData_->WVP = Math::MakeIdentity4x4();
                shadowWvpData_->world = Math::MakeIdentity4x4();
            }
            return; 
        }
        // ライト更新
        if (directionalLightData_) {
            directionalLightData_->direction = math.Normalize(directionalLightData_->direction);
        }

        if (shadowWvpData_ && transform_) {
            Math math;

            Vector3 lightDir = LightManager::GetInstance()->GetDirectionalLight().direction;
            // 0除算防止のための安全対策を追加
            if (math.Length(lightDir) > 0.0001f) {
                lightDir = math.Normalize(lightDir);
            } else {
                lightDir = { 0.0f, -1.0f, 0.0f }; // デフォルトの下向き
            }

            // 1. カメラの位置を取得して、影の箱の「中心（ターゲット）」にする
            Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
            Vector3 target = { 0.0f, 0.0f, 0.0f };
            if (camera) {
                target = camera->GetEye(); // カメラ（プレイヤー）の位置を基準にする
            }

            // 2. 太陽の位置を、カメラから光の逆方向へ離す
            Vector3 lightPos = {
                target.x - lightDir.x * 200.0f,
                target.y - lightDir.y * 200.0f,
                target.z - lightDir.z * 200.0f
            };

            Vector3 up = { 0.0f, 1.0f, 0.0f };

            if (std::abs(lightDir.x) < 0.001f && std::abs(lightDir.z) < 0.001f) {
                up = { 0.0f, 0.0f, 1.0f };
            }

            // 太陽目線のビュー行列
            Matrix4x4 lightView = math.MakeLookAtMatrix(lightPos, target, up);

            Matrix4x4 lightProj = math.MakeOrthographicMatrix(80.0f, 80.0f, 1.0f, 400.0f);

            Matrix4x4 lightVP = math.Multiply(lightView, lightProj);
            LightManager::GetInstance()->GetDirectionalLight().lightViewProj = lightVP;
            // 影用のWVP = モデルのワールド行列 * 太陽のビュープロジェクション
            shadowWvpData_->WVP = math.Multiply(transform_->matWorld, lightVP);
            shadowWvpData_->world = transform_->matWorld;
        }
    }
}

void MeshRenderer::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (!model_ || !common_) return;
    common_->SetGraphicsCommand();
    common_->SetPipelineState(blendMode_);
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // [11] 影用のWVP行列 (b1) をセット
    if (shadowWvpResource_) {
        commandList->SetGraphicsRootConstantBufferView(11, shadowWvpResource_->GetGPUVirtualAddress());
    }

    // [12] シャドウマップのテクスチャ (t5) をセット
    uint32_t shadowMapSrvHandle = common_->GetDxCommon()->GetShadowMapSrvHandle();
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 12, shadowMapSrvHandle);
    // ModelのDrawを呼ぶ
    model_->Draw(
        wvpResource_.Get(),
        LightManager::GetInstance()->GetDirectionalLightResource(),
        cameraResource_.Get(),
        pointLightResource,
        spotLightResource,
        materialResource_.Get(), normalMapHandle_, ormMapHandle_, textureHandle_
    );
}
void MeshRenderer::DrawWater(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    if (!model_ || !common_ || !waterParamResource_) return;

    common_->SetWaterGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);

    //  4番目にカラーテクスチャをセット
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);

    model_->DrawMeshOnly();
}
void MeshRenderer::DrawMagma(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    if (!model_ || !common_ || !waterParamResource_) return;

    common_->SetMagmaGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);

    model_->DrawMeshOnly();
}

void MeshRenderer::DrawIce(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    if (!model_ || !common_ || !waterParamResource_) return;

    common_->SetIceGraphicsCommand(); 
    // (以下、DrawMagmaと全く同じ)
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    model_->DrawMeshOnly();
}
void MeshRenderer::DrawFire(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    if (!model_ || !common_ || !waterParamResource_) return;

    if (!fireProxyModel_) {
        InitializeFireProxyModel();
    }

    common_->SetFireGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    Model* drawModel = fireProxyModel_ ? fireProxyModel_.get() : model_;
    drawModel->DrawMeshOnly();
}

void MeshRenderer::DrawSpecialMaterial(uint32_t depthSrvHandle, uint32_t colorSrvHandle, void (Object3dCommon::*setGraphicsCommand)(), bool useProxyModel) {
    if (!model_ || !common_ || !waterParamResource_ || !setGraphicsCommand) return;

    if (useProxyModel && !fireProxyModel_) {
        InitializeFireProxyModel();
    }

    (common_->*setGraphicsCommand)();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, waterParamResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(2, materialResource_->GetGPUVirtualAddress());
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, depthSrvHandle);
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, colorSrvHandle);
    Model* drawModel = (useProxyModel && fireProxyModel_) ? fireProxyModel_.get() : model_;
    drawModel->DrawMeshOnly();
}

void MeshRenderer::DrawLaser(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetLaserGraphicsCommand);
}

void MeshRenderer::DrawSlimeGel(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetSlimeGelGraphicsCommand);
}

void MeshRenderer::DrawShockwave(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetShockwaveGraphicsCommand);
}

void MeshRenderer::DrawLiquidContact(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetLiquidContactGraphicsCommand);
}

void MeshRenderer::DrawDamageCrack(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetDamageCrackGraphicsCommand);
}

void MeshRenderer::DrawUpdraft(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetUpdraftGraphicsCommand, true);
}

void MeshRenderer::DrawStunBind(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetStunBindGraphicsCommand, true);
}

void MeshRenderer::DrawCrownUnlock(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetCrownUnlockGraphicsCommand, true);
}

void MeshRenderer::DrawPoisonSpore(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetPoisonSporeGraphicsCommand, true);
}

void MeshRenderer::DrawCloud(uint32_t depthSrvHandle, uint32_t colorSrvHandle) {
    DrawSpecialMaterial(depthSrvHandle, colorSrvHandle, &Object3dCommon::SetCloudGraphicsCommand, true);
}

void MeshRenderer::InitializeFireProxyModel() {
    ModelCommon* modelCommon = ModelManager::GetInstance()->GetModelCommon();
    if (!modelCommon) {
        return;
    }

    fireProxyModel_ = std::make_unique<Model>();

    std::vector<Model::VertexData> vertices(4);
    auto setVertex = [](Model::VertexData& vertex, float x, float y, float u, float v) {
        vertex.position = { x, y, 0.0f, 1.0f };
        vertex.texcoord = { u, v };
        vertex.normal = { 0.0f, 0.0f, 1.0f };
        vertex.tangent = { 1.0f, 0.0f, 0.0f };
        vertex.boneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
        vertex.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    setVertex(vertices[0], -1.0f, -1.0f, 0.0f, 1.0f);
    setVertex(vertices[1], -1.0f, 1.0f, 0.0f, 0.0f);
    setVertex(vertices[2], 1.0f, 1.0f, 1.0f, 0.0f);
    setVertex(vertices[3], 1.0f, -1.0f, 1.0f, 1.0f);

    const std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3 };
    fireProxyModel_->CreateFromVertices(modelCommon, vertices, indices);
}

void MeshRenderer::SetModel(const std::string& modelName) {
    modelName_ = modelName;
    model_ = ModelManager::GetInstance()->LoadModel(modelName);
}

void MeshRenderer::SetColor(const Vector4& color) {
    if (materialData_) materialData_->color = color;
}

void MeshRenderer::SetMaterialType(int32_t type) {
    if (materialData_) materialData_->materialType = type;
}

void MeshRenderer::SetIntensity(float intensity) {
    if (directionalLightData_) directionalLightData_->intensity = intensity;
}

Vector4 MeshRenderer::GetColor() const {
    return materialData_ ? materialData_->color : Vector4{ 1,1,1,1 };
}

int32_t MeshRenderer::GetMaterialType() const {
    return materialData_ ? materialData_->materialType : 0;
}

void MeshRenderer::SetMetallic(float metallic) {
    if (materialData_) materialData_->metallic = metallic;
}

void MeshRenderer::SetRoughness(float roughness) {
    if (materialData_) materialData_->roughness = roughness;
}

float MeshRenderer::GetMetallic() const {
    return materialData_ ? materialData_->metallic : 0.0f;
}

float MeshRenderer::GetRoughness() const {
    return materialData_ ? materialData_->roughness : 0.3f;
}

void MeshRenderer::SetEnableNormalMap(bool enable) {
    if (materialData_) materialData_->enableNormalMap = enable ? 1 : 0;
}
bool MeshRenderer::GetEnableNormalMap() const {
    return materialData_ ? (materialData_->enableNormalMap == 1) : false;
}
void MeshRenderer::SetNormalMap(const std::string& texturePath) {
    normalMapPath_ = texturePath;
    if (!texturePath.empty()) {
        normalMapHandle_ = TextureManager::GetInstance()->Load(texturePath, true);
    } else {
        normalMapHandle_ = 0;
    }
}

void MeshRenderer::SetOrmMap(const std::string& texturePath) {
    ormMapPath_ = texturePath;
    if (!texturePath.empty()) {
        ormMapHandle_ = TextureManager::GetInstance()->Load(texturePath, true);
    } else {
        ormMapHandle_ = 0;
    }
}


void MeshRenderer::SetTexture(const std::string& texturePath) {
    texturePath_ = texturePath;
    if (!texturePath.empty()) {
        textureHandle_ = TextureManager::GetInstance()->Load(texturePath);
    } else {
        textureHandle_ = 0;
    }
}


void MeshRenderer::DrawShadow() {
    if (!model_ || !common_ || !shadowWvpResource_) return;
    common_->SetShadowGraphicsCommand();
    // 影用のパイプラインに変更
    common_->SetShadowPipelineState();

    // 軽量版のドローコールを呼ぶ
    model_->DrawShadow(shadowWvpResource_.Get());
}

void MeshRenderer::SetShadowCommonState() {
    if (!common_) return;
    common_->SetShadowGraphicsCommand();
    common_->SetShadowPipelineState();
}

void MeshRenderer::DrawShadowOnly() {
    if (!model_ || !shadowWvpResource_) return;
    model_->DrawShadow(shadowWvpResource_.Get());
}

void MeshRenderer::DrawLocalFog(uint32_t depthSrvHandle) {
    if (!model_ || !common_ || !localFogResource_) return;

    common_->SetLocalFogGraphicsCommand();
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();


    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, depthSrvHandle);


    commandList->SetGraphicsRootConstantBufferView(3, localFogResource_->GetGPUVirtualAddress());

    // [0] にWVP、[1] にボーンが自動セットされる
    model_->DrawShadow(wvpResource_.Get());
}
void MeshRenderer::SetEnableEnvMap(bool enable) {
    if (materialData_) materialData_->enableEnvMap = enable ? 1 : 0;
}
bool MeshRenderer::GetEnableEnvMap() const {
    return materialData_ ? (materialData_->enableEnvMap == 1) : false;
}
void MeshRenderer::SetEnvIntensity(float intensity) {
    if (materialData_) materialData_->envIntensity = intensity;
}
float MeshRenderer::GetEnvIntensity() const {
    return materialData_ ? materialData_->envIntensity : 1.0f;
}

void MeshRenderer::SetEmissive(float emissive) {
    if (materialData_) materialData_->emissive = emissive;
}

float MeshRenderer::GetEmissive() const {
    return materialData_ ? materialData_->emissive : 1.0f;
}
