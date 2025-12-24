#define NOMINMAX
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
    directionalLightData_->intensity = 0.0f;

    cameraResource_ = dxCommon->CreateBufferResource(sizeof(CameraForGPU));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));
    cameraData_->worldPosition = { 0.0f, 0.0f, 0.0f }; 


}

OBB Object3d::GetOBB() const
{
    OBB obb;

    // ワールド行列から情報を抽出
    const Matrix4x4& worldMat = GetWorldMatrix();

    // 1. 中心座標 (Translate)
    obb.center = { worldMat.m[3][0], worldMat.m[3][1], worldMat.m[3][2] };

    // 2. 回転軸 (Axis)
    // ※スケールが含まれていると長さが変わるので正規化する
    Math math;
    Vector3 xAxis = { worldMat.m[0][0], worldMat.m[0][1], worldMat.m[0][2] };
    Vector3 yAxis = { worldMat.m[1][0], worldMat.m[1][1], worldMat.m[1][2] };
    Vector3 zAxis = { worldMat.m[2][0], worldMat.m[2][1], worldMat.m[2][2] };

    // 各軸の長さ（ワールドスケール）を取得
    float lenX = math.Length(xAxis);
    float lenY = math.Length(yAxis);
    float lenZ = math.Length(zAxis);

    // 向きベクトルは正規化
    obb.orientations[0] = (lenX > 0.0f) ? (xAxis / lenX) : Vector3{ 1.0f, 0.0f, 0.0f };
    obb.orientations[1] = (lenY > 0.0f) ? (yAxis / lenY) : Vector3{ 0.0f, 1.0f, 0.0f };
    obb.orientations[2] = (lenZ > 0.0f) ? (zAxis / lenZ) : Vector3{ 0.0f, 0.0f, 1.0f };

    // 3. サイズ (半サイズ)
    // collisionSize_ は「全サイズ」で保持されている前提
    Vector3 fullSize = collisionSize_;

    // ワールドスケールを掛けて半サイズを算出（親のスケールも worldMat に反映される）
    Vector3 worldScale = { lenX, lenY, lenZ };
    obb.size.x = (fullSize.x * worldScale.x) * 0.5f;
    obb.size.y = (fullSize.y * worldScale.y) * 0.5f;
    obb.size.z = (fullSize.z * worldScale.z) * 0.5f;

    return obb;
}


void Object3d::SetModel(const std::string& modelName) {
    modelName_ = modelName;
    // 探して、なければ読み込んでくれる
    model_ = ModelManager::GetInstance()->LoadModel(modelName);

}

void Object3d::Update(float deltaTime) {
    if (model_) {
        model_->Update();
    }
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
        worldMatrix_ = math.Multiply(localMatrix_, parent_->GetWorldMatrix());
    }

    // --- 既存の WVP とライティングの処理 ---
    const Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    // カメラがあれば計算する
    if (camera) {
        const Matrix4x4& viewMatrix = camera->GetViewMatrix();
        const Matrix4x4& projectionMatrix = camera->GetProjectionMatrix();

        Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix_, math.Multiply(viewMatrix, projectionMatrix));

        wvpData_->WVP = worldViewProjectionMatrix;
        wvpData_->world = worldMatrix_;
        wvpData_->WorldInverseTranspose = math.Transpose(math.Inverse(worldMatrix_));
        directionalLightData_->direction = math.Normalize(directionalLightData_->direction);
        cameraData_->worldPosition = camera->GetEye();
    }

}



void Object3d::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (model_ == nullptr) {
        return;
    }


    common_->SetPipelineState(blendMode_);

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, model_->GetTextureHandle());
    if (model_) {

        model_->Draw(wvpResource_.Get(), directionalLightResource_.Get(), cameraResource_.Get(), pointLightResource, spotLightResource);
    }
}

void Object3d::SetParent(Object3d* parent) {
    parent_ = parent;
}

CollisionInfo Object3d::CheckCollision(Object3d* other) {

    ColliderType myType = this->GetColliderType();
    ColliderType otherType = other->GetColliderType();
    CollisionInfo collision;
    collision.isColliding = false; // 初期化

    //タイプを指定して
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
    if (myType == ColliderType::kSphere && otherType == ColliderType::kOBB) {
        collision = CheckSphereOBBCollision(this->GetWorldPosition(), this->GetCollisionRadius(), other->GetOBB());
    } else if (myType == ColliderType::kOBB && otherType == ColliderType::kSphere) {
        // 引数の順序を入れ替えて呼び出し、法線を反転
        collision = CheckSphereOBBCollision(other->GetWorldPosition(), other->GetCollisionRadius(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }
    // 2. OBB vs OBB
    else if (myType == ColliderType::kOBB && otherType == ColliderType::kOBB) {
        collision = CheckOBBCollision(this->GetOBB(), other->GetOBB());
    }
    // 3. AABB vs OBB 
    else if (myType == ColliderType::kAABB && otherType == ColliderType::kOBB) {
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
void Object3d::SetColor(const Vector4& color) {
    if (directionalLightData_) {
        directionalLightData_->color = color;
    }
}
void Object3d::SetIntensity(float intensity) {
    if (directionalLightData_) {
        directionalLightData_->intensity = intensity;
    }
}

