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
    
}

void MeshRenderer::Update() {
	// 経過時間を更新してGPUに転送
    time_ += 1.0f / 60.0f;
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
        const Camera* camera = CameraManager::GetInstance()->GetMainCamera();

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
            localFogData_->cameraPos = camera->GetEye();
            localFogData_->inverseViewProj = math.Inverse(viewProj);
        } else {
            wvpData_->WVP = Math::MakeIdentity4x4();
            wvpData_->world = Math::MakeIdentity4x4();
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
            const Camera* camera = CameraManager::GetInstance()->GetMainCamera();
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
        normalMapHandle_ = TextureManager::GetInstance()->Load(texturePath);
    } else {
        normalMapHandle_ = 0;
    }
}

void MeshRenderer::SetOrmMap(const std::string& texturePath) {
    ormMapPath_ = texturePath;
    if (!texturePath.empty()) {
        ormMapHandle_ = TextureManager::GetInstance()->Load(texturePath);
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