#include "Object3d.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "SRVManager.h"
#include "CameraManager.h"
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
    // 探して、なければ読み込んでくれる
    model_ = ModelManager::GetInstance()->LoadModel(modelName);
    modelName_ = modelName;
}

void Object3d::Update(float deltaTime) {
    // 派生クラス (Playerなど) でオーバーライドされる用
}

void Object3d::UpdateLocalMatrix() {
    Math math;

    // ★ localMatrix_ を計算する
    localMatrix_ = math.MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);

    // ★ 親がいない場合、ローカル行列 = ワールド行列とする
    if (parent_ == nullptr) {
        worldMatrix_ = localMatrix_;
    }

}


void Object3d::UpdateWorldMatrix() {
    Math math;

    // --- 親子関係の処理 ---
    if (parent_ != nullptr) {
        // 親のワールド行列を取得し、それに自分のローカル行列を乗算する
        worldMatrix_ = math.Multiply(localMatrix_, parent_->GetWorldMatrix());
    }
     

    // --- 既存の WVP とライティングの処理（ここから）---
    const Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    const Matrix4x4& viewMatrix = camera->GetViewMatrix();
    const Matrix4x4& projectionMatrix = camera->GetProjectionMatrix();

    // ★ 計算対象の行列を worldMatrix_ に変更
    Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix_, math.Multiply(viewMatrix, projectionMatrix));

    wvpData_->WVP = worldViewProjectionMatrix;
    wvpData_->world = worldMatrix_; // ★ worldMatrix_ をセット
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

void Object3d::SetParent(Object3d* parent) {
    parent_ = parent;
}

CollisionInfo Object3d::CheckCollision(Object3d* other) {

    ColliderType myType = this->GetColliderType();
    ColliderType otherType = other->GetColliderType();
    CollisionInfo collision;
    collision.isColliding = false; // 初期化

    // (↓ Character::OnCollision から移動してきたロジック)
    if (myType == ColliderType::kAABB && otherType == ColliderType::kAABB) {
        collision = CheckAABBCollision(this->GetAABB(), other->GetAABB());
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kSphere) {
        collision = CheckSphereCollision(
            this->GetWorldPosition(), this->GetCollisionRadius(),
            other->GetWorldPosition(), other->GetCollisionRadius());
    } else if (myType == ColliderType::kAABB && otherType == ColliderType::kSphere) {
        collision = CheckSphereAABBCollision(
            other->GetWorldPosition(), other->GetCollisionRadius(), this->GetAABB());
        collision.normal = collision.normal * -1.0f; // 法線を反転
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kAABB) {
        collision = CheckSphereAABBCollision(
            this->GetWorldPosition(), this->GetCollisionRadius(), other->GetAABB());
    }

    return collision;
}


std::unique_ptr<Object3d> Object3d::Clone() const {
    auto newObj = std::make_unique<Object3d>();

    assert(common_ != nullptr);
    newObj->Initialize(common_);

    //  modelName_ (文字列) を使ってモデルをセット
    if (!modelName_.empty()) {
        newObj->SetModel(this->modelName_);
    }

    // 4. Transform 情報をコピー
    newObj->transform_ = this->transform_;

    // 5. 名前をコピー
    newObj->name_ = this->name_;

    // ★ 2コライダー情報をコピー
    newObj->collisionAttribute_ = this->collisionAttribute_;
    newObj->collisionMask_ = this->collisionMask_;
    newObj->colliderType_ = this->colliderType_;
    newObj->collisionSize_ = this->collisionSize_;


    return newObj;
}