#define NOMINMAX
#include "Object3d.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "SRVManager.h"
#include "CameraManager.h"
#include "SceneManager.h"
#include "GhostRecorder.h"
#include <cassert>


Object3d::~Object3d() {
    if (recorder_) {
        delete recorder_;
        recorder_ = nullptr;
    }
}

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

    InitializeRecorder(nullptr);


}

OBB Object3d::GetOBB() const
{
    OBB obb;
    Math math;

    // ワールド行列を取得
    const Matrix4x4& worldMat = GetWorldMatrix();

    // 1. 回転軸 (Axis) と ワールドスケール (Scale) の抽出
    Vector3 xAxis = { worldMat.m[0][0], worldMat.m[0][1], worldMat.m[0][2] };
    Vector3 yAxis = { worldMat.m[1][0], worldMat.m[1][1], worldMat.m[1][2] };
    Vector3 zAxis = { worldMat.m[2][0], worldMat.m[2][1], worldMat.m[2][2] };

    // 各軸の長さ（ワールドスケール）を算出
    float lenX = math.Length(xAxis);
    float lenY = math.Length(yAxis);
    float lenZ = math.Length(zAxis);

    // 軸を正規化 (向きベクトルにする)
    obb.orientations[0] = (lenX > 0.0f) ? (xAxis / lenX) : Vector3{ 1.0f, 0.0f, 0.0f };
    obb.orientations[1] = (lenY > 0.0f) ? (yAxis / lenY) : Vector3{ 0.0f, 1.0f, 0.0f };
    obb.orientations[2] = (lenZ > 0.0f) ? (zAxis / lenZ) : Vector3{ 0.0f, 0.0f, 1.0f };

    // 2. 中心座標 (Center)
    // オブジェクトの原点 (ワールド座標)
    Vector3 worldPos = { worldMat.m[3][0], worldMat.m[3][1], worldMat.m[3][2] };

    // コライダーの中心オフセット (colliderConfig_.center) を適用
    Vector3 offset = colliderConfig_.center;
    obb.center = worldPos + (xAxis * offset.x) + (yAxis * offset.y) + (zAxis * offset.z);

    // 3. サイズ (半サイズ)
    obb.size.x = colliderConfig_.size.x * lenX;
    obb.size.y = colliderConfig_.size.y * lenY;
    obb.size.z = colliderConfig_.size.z * lenZ;

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
    if (recorder_) {
        recorder_->Update();
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

    if (!isVisible_) {
        return;
    }
    common_->SetPipelineState(blendMode_);

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    if (model_) {
        model_->Draw(wvpResource_.Get(), directionalLightResource_.Get(), cameraResource_.Get(), pointLightResource, spotLightResource);
    }
}
void Object3d::SetParent(Object3d* parent) {
    if (parent_) {
        std::vector<Object3d*>& kids = parent_->children_;
        // 削除イディオム (Erase-Remove idiom)
        kids.erase(std::remove(kids.begin(), kids.end(), this), kids.end());
    }


    parent_ = parent;

    if (parent_) {
        parent_->children_.push_back(this);
    }
}
CollisionInfo Object3d::CheckCollision(Object3d* other) {
    CollisionInfo collision;
    collision.isColliding = false; // 初期化

    // 自分のタイプと相手のタイプを取得
    ColliderType myType = this->GetColliderType();
    ColliderType otherType = other->GetColliderType();

    // ====================================================================
    // 1. 同じ形状同士の判定
    // ====================================================================

    // AABB vs AABB
    if (myType == ColliderType::kAABB && otherType == ColliderType::kAABB) {
        collision = CheckAABBCollision(this->GetAABB(), other->GetAABB());
    }
    // Sphere vs Sphere
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kSphere) {
        collision = CheckSphereCollision(
            this->GetWorldPosition(), this->GetCollisionRadius(),
            other->GetWorldPosition(), other->GetCollisionRadius());
    }
    // OBB vs OBB
    else if (myType == ColliderType::kOBB && otherType == ColliderType::kOBB) {
        collision = CheckOBBCollision(this->GetOBB(), other->GetOBB());
    }

    // ====================================================================
    // 2. 異なる形状同士の判定
    // ====================================================================

    // Sphere vs AABB
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kAABB) {
        collision = CheckSphereAABBCollision(
            this->GetWorldPosition(), this->GetCollisionRadius(), other->GetAABB());
    }
    // AABB vs Sphere (引数を入れ替えるため、法線を反転)
    else if (myType == ColliderType::kAABB && otherType == ColliderType::kSphere) {
        collision = CheckSphereAABBCollision(
            other->GetWorldPosition(), other->GetCollisionRadius(), this->GetAABB());
        collision.normal = collision.normal * -1.0f;
    }

    // Sphere vs OBB
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kOBB) {
        collision = CheckSphereOBBCollision(
            this->GetWorldPosition(), this->GetCollisionRadius(), other->GetOBB());
    }
    // OBB vs Sphere (引数を入れ替えるため、法線を反転)
    else if (myType == ColliderType::kOBB && otherType == ColliderType::kSphere) {
        collision = CheckSphereOBBCollision(
            other->GetWorldPosition(), other->GetCollisionRadius(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    }

    // ====================================================================
    // 3. AABB vs OBB の判定 
    // ====================================================================

    // AABB(自分) vs OBB(相手)
    else if (myType == ColliderType::kAABB && otherType == ColliderType::kOBB) {
        // 関数は「AABBからOBBを押し出すベクトル」を返す
        collision = CheckAABBOBBCollision(this->GetAABB(), other->GetOBB());

        collision.normal = collision.normal * -1.0f;
    }
    // OBB(自分) vs AABB(相手)
    else if (myType == ColliderType::kOBB && otherType == ColliderType::kAABB) {
        // CheckAABBOBBCollision(A, B) を呼ぶ
        // A=相手(地面/AABB), B=自分(プレイヤー/OBB)
        collision = CheckAABBOBBCollision(other->GetAABB(), this->GetOBB());

     
    }

    return collision;
}



std::unique_ptr<Object3d> Object3d::Clone() const {
    auto newObj = std::make_unique<Object3d>();

    assert(common_ != nullptr);
    newObj->Initialize(common_);

    // --- 基本情報のコピー ---
    if (!modelName_.empty()) {
        newObj->SetModel(this->modelName_);
    }
    newObj->name_ = this->name_;

    // Transform
    newObj->transform_ = this->transform_;

    // --- コライダー & 物理 ---
    newObj->SetColliderConfig(this->colliderConfig_);
    newObj->collisionAttribute_ = this->collisionAttribute_;
    newObj->collisionMask_ = this->collisionMask_;

    // 1. クラス名 (InvisibleBox か Model かの識別に必須)
    newObj->className_ = this->className_;

    // 2. 可視性 (透明ブロックの設定を引き継ぐために必須)
    newObj->isVisible_ = this->isVisible_;

    // 3. イベントタイプ (ダメージ床などの設定)
    newObj->eventType_ = this->eventType_;

    // 4. パラメータ (HP, Speedなどのゲームデータ)
    newObj->param_ = this->param_;

    // アニメーション設定のコピー
    newObj->animName_ = this->animName_;
    newObj->isAnimLoop_ = this->isAnimLoop_;
    newObj->isAnimRelative_ = this->isAnimRelative_;

    // 複製したオブジェクトもレコーダーを初期化して再生開始！
    newObj->InitializeRecorder(nullptr); 

    // 設定が入っていれば、即座に再生を開始させる
    if (!newObj->animName_.empty()) {
        newObj->recorder_->Play(
            newObj->animName_,
            newObj->isAnimLoop_,
            newObj->isAnimRelative_
        );
    }

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
void Object3d::InitializeRecorder(SceneManager* sceneManager) {
    // すでに持っていたら作り直さない（安全策）
    if (recorder_) {
        delete recorder_;
    }

    // 1. 実体を作る (new)
    recorder_ = new GhostRecorder();

    // 2. 初期化する
    recorder_->Initialize(sceneManager);

    recorder_->SetTarget(this);
}


