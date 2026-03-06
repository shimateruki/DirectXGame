#include "MeshRenderer.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "LightManager.h"
#include <cassert>

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
    
}

void MeshRenderer::Update() {
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
        } else {
            wvpData_->WVP = Math::MakeIdentity4x4();
            wvpData_->world = Math::MakeIdentity4x4();
        }

        // ライト更新
        if (directionalLightData_) {
            directionalLightData_->direction = math.Normalize(directionalLightData_->direction);
        }
    }
}

void MeshRenderer::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (!model_ || !common_) return;

    common_->SetPipelineState(blendMode_);

    // ModelのDrawを呼ぶ
    model_->Draw(
        wvpResource_.Get(),
        LightManager::GetInstance()->GetDirectionalLightResource(),
        cameraResource_.Get(),
        pointLightResource,
        spotLightResource,
        materialResource_.Get(), normalMapHandle_
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