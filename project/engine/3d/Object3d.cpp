#include "engine/3d/Object3d.h"
#include "engine/base/DirectXCommon.h"
#include "engine/3d/ModelManager.h"
#include "engine/base/SRVManager.h"
#include "engine/3d/CameraManager.h"
#include <cassert>

void Object3d::Initialize(Object3dCommon* common) {
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    wvpResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    Math math;
    wvpData_->WVP = math.makeIdentity4x4();
    wvpData_->world = math.makeIdentity4x4();

    directionalLightResource_ = dxCommon->CreateBufferResource(sizeof(DirectionalLight));
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData_->intensity = 1.0f;
}

void Object3d::SetModel(const std::string& modelName) {
    // ModelManagerにモデル名で要求するだけ！
    // 探して、なければ読み込んでくれる
    model_ = ModelManager::GetInstance()->LoadModel(modelName);
}
void Object3d::Update() {
    Math math;
    const Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    const Matrix4x4& viewMatrix = camera->GetViewMatrix();
    const Matrix4x4& projectionMatrix = camera->GetProjectionMatrix();
    Matrix4x4 worldMatrix = math.MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
    Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix, math.Multiply(viewMatrix, projectionMatrix));
    wvpData_->WVP = worldViewProjectionMatrix;
    wvpData_->world = worldMatrix;
    directionalLightData_->direction = math.Normalize(directionalLightData_->direction);
}

void Object3d::Draw() {
    if (model_ == nullptr) {
        return;
    }


    common_->SetPipelineState(blendMode_);

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, model_->GetTextureHandle());
    model_->Draw(wvpResource_.Get(), directionalLightResource_.Get());
}