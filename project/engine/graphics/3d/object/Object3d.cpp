#define NOMINMAX
#include "Object3d.h"
#include "DirectXCommon.h"
#include "ModelManager.h"
#include "SRVManager.h"
#include "CameraManager.h"
#include "SceneManager.h"
#include "GhostRecorder.h"
#include <cassert>
#include <algorithm> // min, max

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

    // WVP用バッファ
    wvpResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    Math math;
    wvpData_->WVP = math.MakeIdentity4x4();
    wvpData_->world = math.MakeIdentity4x4();

    // ライト用バッファ
    directionalLightResource_ = dxCommon->CreateBufferResource(sizeof(DirectionalLight));
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData_->intensity = 1.0f;

    // カメラ座標バッファ
    cameraResource_ = dxCommon->CreateBufferResource(sizeof(CameraForGPU));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));
    cameraData_->worldPosition = { 0.0f, 0.0f, 0.0f };

    // マテリアルバッファ
    materialResource_ = dxCommon->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    // マテリアル初期値
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 白
    materialData_->enableLighting = 1;                 // ライティング有効
    materialData_->uvTransform = Math::MakeIdentity4x4();
    materialData_->selectedLighting = 2;               // Blinn-Phong
    materialData_->shininess = 20.0f;                  // 適度な光沢
    materialData_->materialType = 0;                   // 通常マテリアル

    // Transformの初期化
    transform_.scale = { 1.0f, 1.0f, 1.0f };
    transform_.rotate = { 0.0f, 0.0f, 0.0f };
    transform_.translate = { 0.0f, 0.0f, 0.0f };

    // 行列の初期計算
    UpdateLocalMatrix();
    UpdateWorldMatrix();

    InitializeRecorder(nullptr);
}

OBB Object3d::GetOBB() const {
    OBB obb;
    Math math;

    // =========================================================
    // 1. コライダー自身の「ローカル行列」を作る
    // =========================================================
    // 回転
    Matrix4x4 matRotX = math.MakeRotateXMatrix(colliderConfig_.rotation.x);
    Matrix4x4 matRotY = math.MakeRotateYMatrix(colliderConfig_.rotation.y);
    Matrix4x4 matRotZ = math.MakeRotateZMatrix(colliderConfig_.rotation.z);
    Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));

    // 中心ズレ (Center)
    Matrix4x4 matTrans = math.MakeTranslateMatrix(colliderConfig_.center);

    // コライダー単体の行列
    Matrix4x4 matColliderLocal = math.Multiply(matRot, matTrans);

    // =========================================================
    // 2. オブジェクトの「ワールド行列」と合成する
    // =========================================================
    // transform_.matWorld を使用する形に変更
    Matrix4x4 matFinal = math.Multiply(matColliderLocal, transform_.matWorld);

    // =========================================================
    // 3. 行列から OBB の情報を抜き出す
    // =========================================================
    // A. 中心座標
    obb.center = { matFinal.m[3][0], matFinal.m[3][1], matFinal.m[3][2] };

    // B. 3つの軸
    obb.orientations[0] = math.Normalize({ matFinal.m[0][0], matFinal.m[0][1], matFinal.m[0][2] }); // X軸
    obb.orientations[1] = math.Normalize({ matFinal.m[1][0], matFinal.m[1][1], matFinal.m[1][2] }); // Y軸
    obb.orientations[2] = math.Normalize({ matFinal.m[2][0], matFinal.m[2][1], matFinal.m[2][2] }); // Z軸

    // C. サイズ (半サイズ)
    obb.size = {
        colliderConfig_.size.x * transform_.scale.x,
        colliderConfig_.size.y * transform_.scale.y,
        colliderConfig_.size.z * transform_.scale.z
    };

    return obb;
}

void Object3d::SetModel(const std::string& modelName) {
    modelName_ = modelName;
    model_ = ModelManager::GetInstance()->LoadModel(modelName);
}

void Object3d::Update(float deltaTime) {
    if (model_) {
        // アニメーション更新
        if (!animName_.empty()) {
            const Model::Animation* anim = model_->GetAnimation(animName_);
            if (anim) {
                // 時間を進める
                animationTime_ += deltaTime;

                // ループ処理
                float time = animationTime_;
                if (isAnimLoop_ && anim->duration > 0.0f) {
                    time = std::fmod(time, anim->duration);
                } else {
                    time = std::min(time, anim->duration);
                }

                // モデルにポーズを適用
                model_->ApplyAnimation(*anim, time);
            }
        }
        model_->Update();
    }

    if (recorder_) {
        recorder_->Update();
    }
}

// -------------------------------------------------------------
// 行列更新処理 (Transformへの委譲)
// -------------------------------------------------------------

void Object3d::UpdateLocalMatrix() {
    // Transform側で行列計算を行う
    transform_.UpdateMatrix();
}

void Object3d::UpdateWorldMatrix() {
    // 1. Transform側で行列計算
    transform_.UpdateMatrix();

    // 2. GPUへのデータ転送 (WVP計算)
    //    計算結果は transform_.matWorld に格納されている
    if (wvpData_) {
        Math math;
        const Camera* camera = CameraManager::GetInstance()->GetMainCamera();

        if (camera) {
            const Matrix4x4& view = camera->GetViewMatrix();
            const Matrix4x4& proj = camera->GetProjectionMatrix();

            // ビュープロジェクション行列
            Matrix4x4 viewProj = math.Multiply(view, proj);

            // WVP = World * View * Proj
            wvpData_->WVP = math.Multiply(transform_.matWorld, viewProj);

            // World行列そのもの
            wvpData_->world = transform_.matWorld;

            // 法線変換用の逆転置行列
            wvpData_->WorldInverseTranspose = math.Transpose(math.Inverse(transform_.matWorld));

            // カメラ座標
            cameraData_->worldPosition = camera->GetEye();
        } else {
            // カメラがない場合の安全策 (単位行列)
            wvpData_->WVP = math.MakeIdentity4x4();
            wvpData_->world = math.MakeIdentity4x4();
        }

        // 平行光源の更新
        if (directionalLightData_) {
            directionalLightData_->direction = math.Normalize(directionalLightData_->direction);
        }
    }
}

// -------------------------------------------------------------

void Object3d::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    if (!isVisible_) {
        return;
    }
    common_->SetPipelineState(blendMode_);

    if (model_) {
        model_->Draw(wvpResource_.Get(), directionalLightResource_.Get(), cameraResource_.Get(), pointLightResource, spotLightResource, materialResource_.Get());
    }
}

void Object3d::SetParent(Object3d* parent) {
    // 古い親から削除
    if (parent_) {
        std::vector<Object3d*>& kids = parent_->children_;
        kids.erase(std::remove(kids.begin(), kids.end(), this), kids.end());
    }

    parent_ = parent;

    // 新しい親に登録
    if (parent_) {
        parent_->children_.push_back(this);
        // Transform側にも親を伝える (ポインタ渡し)
        transform_.parent = parent_->GetTransform();
    } else {
        // 親なし
        transform_.parent = nullptr;
    }

    // 親が変わったので行列再計算
    UpdateWorldMatrix();
}

CollisionInfo Object3d::CheckCollision(Object3d* other) {
    CollisionInfo collision;
    collision.isColliding = false;

    ColliderType myType = this->GetColliderType();
    ColliderType otherType = other->GetColliderType();

    // 同じ形状同士
    if (myType == ColliderType::kAABB && otherType == ColliderType::kAABB) {
        collision = CheckAABBCollision(this->GetAABB(), other->GetAABB());
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kSphere) {
        collision = CheckSphereCollision(
            this->GetWorldPosition(), this->GetCollisionRadius(),
            other->GetWorldPosition(), other->GetCollisionRadius());
    } else if (myType == ColliderType::kOBB && otherType == ColliderType::kOBB) {
        collision = CheckOBBCollision(this->GetOBB(), other->GetOBB());
    }
    // 異なる形状同士
    else if (myType == ColliderType::kSphere && otherType == ColliderType::kAABB) {
        collision = CheckSphereAABBCollision(
            this->GetWorldPosition(), this->GetCollisionRadius(), other->GetAABB());
    } else if (myType == ColliderType::kAABB && otherType == ColliderType::kSphere) {
        collision = CheckSphereAABBCollision(
            other->GetWorldPosition(), other->GetCollisionRadius(), this->GetAABB());
        collision.normal = collision.normal * -1.0f;
    } else if (myType == ColliderType::kSphere && otherType == ColliderType::kOBB) {
        collision = CheckSphereOBBCollision(
            this->GetWorldPosition(), this->GetCollisionRadius(), other->GetOBB());
    } else if (myType == ColliderType::kOBB && otherType == ColliderType::kSphere) {
        collision = CheckSphereOBBCollision(
            other->GetWorldPosition(), other->GetCollisionRadius(), this->GetOBB());
        collision.normal = collision.normal * -1.0f;
    } else if (myType == ColliderType::kAABB && otherType == ColliderType::kOBB) {
        collision = CheckAABBOBBCollision(this->GetAABB(), other->GetOBB());
        collision.normal = collision.normal * -1.0f;
    } else if (myType == ColliderType::kOBB && otherType == ColliderType::kAABB) {
        collision = CheckAABBOBBCollision(other->GetAABB(), this->GetOBB());
    }

    return collision;
}

void Object3d::SetIntensity(float intensity) {
    if (directionalLightData_) {
        directionalLightData_->intensity = intensity;
    }
}

void Object3d::InitializeRecorder(SceneManager* sceneManager) {
    if (recorder_) {
        delete recorder_;
    }
    recorder_ = new GhostRecorder();
    recorder_->Initialize(sceneManager);
    recorder_->SetTarget(this);
}

void Object3d::CopyFrom(const Object3d* other) {
    if (!other) return;

    if (!other->modelName_.empty()) {
        this->SetModel(other->modelName_);
    }
    this->name_ = other->name_;

    // Transform
    this->transform_ = other->transform_;

    this->SetColliderConfig(other->colliderConfig_);
    this->collisionAttribute_ = other->collisionAttribute_;
    this->collisionMask_ = other->collisionMask_;

    this->className_ = other->className_;
    this->isVisible_ = other->isVisible_;
    this->eventType_ = other->eventType_;
    this->enemyType_ = other->enemyType_;
    this->param_ = other->param_;

    this->animName_ = other->animName_;
    this->isAnimLoop_ = other->isAnimLoop_;
    this->isAnimRelative_ = other->isAnimRelative_;

    this->blendMode_ = other->blendMode_;
    this->SetMaterialType(other->GetMaterialType());
    this->SetColor(const_cast<Object3d*>(other)->GetColor());

    this->InitializeRecorder(nullptr);

    if (!this->animName_.empty() && this->recorder_) {
        this->recorder_->Play(
            this->animName_,
            this->isAnimLoop_,
            this->isAnimRelative_
        );
    }
}

std::unique_ptr<Object3d> Object3d::Clone() const {
    auto newObj = std::make_unique<Object3d>();
    assert(common_ != nullptr);
    newObj->Initialize(common_);
    newObj->CopyFrom(this);
    return newObj;
}

// ---------------------------------------------------------
// JSON Export
// ---------------------------------------------------------
json Object3d::ExportToJson() {
    json j;
    j["name"] = name_;
    j["modelName"] = modelName_;

    j["scale"] = { transform_.scale.x, transform_.scale.y, transform_.scale.z };
    j["rotate"] = { transform_.rotate.x, transform_.rotate.y, transform_.rotate.z };

    j["collider"] = {
        {"type", static_cast<int>(colliderConfig_.type)},
        {"size", { colliderConfig_.size.x, colliderConfig_.size.y, colliderConfig_.size.z }},
        {"center", { colliderConfig_.center.x, colliderConfig_.center.y, colliderConfig_.center.z }},
        { "rotation", { colliderConfig_.rotation.x, colliderConfig_.rotation.y, colliderConfig_.rotation.z } }
    };

    j["animation"] = {
        {"animName", animName_},
        {"isAnimLoop", isAnimLoop_},
        {"isAnimRelative", isAnimRelative_}
    };

    j["eventType"] = static_cast<int>(eventType_);
    j["enemyType"] = enemyType_;
    j["blendMode"] = static_cast<int>(blendMode_);
    j["materialType"] = materialData_->materialType;
    return j;
}

// ---------------------------------------------------------
// JSON Import
// ---------------------------------------------------------
void Object3d::ImportFromJson(const json& j) {
    if (j.contains("modelName")) {
        modelName_ = j["modelName"];
        // ModelManager::Load(modelName_); 
    }

    if (j.contains("scale")) {
        transform_.scale = { j["scale"][0], j["scale"][1], j["scale"][2] };
    }
    if (j.contains("rotate")) {
        transform_.rotate = { j["rotate"][0], j["rotate"][1], j["rotate"][2] };
    }

    // インポート直後に行列を更新しておく
    transform_.UpdateMatrix();

    if (j.contains("collider")) {
        const auto& col = j["collider"];
        if (col.contains("type")) colliderConfig_.type = static_cast<ColliderType>(col["type"]);
        if (col.contains("size")) {
            colliderConfig_.size = { col["size"][0], col["size"][1], col["size"][2] };
        }
        if (col.contains("center")) {
            colliderConfig_.center = { col["center"][0], col["center"][1], col["center"][2] };
        }
        if (col.contains("rotation")) {
            colliderConfig_.rotation = { col["rotation"][0], col["rotation"][1], col["rotation"][2] };
        }
    }

    if (j.contains("animation")) {
        const auto& anim = j["animation"];
        if (anim.contains("animName")) animName_ = anim["animName"];
        if (anim.contains("isAnimLoop")) isAnimLoop_ = anim["isAnimLoop"];
        if (anim.contains("isAnimRelative")) isAnimRelative_ = anim["isAnimRelative"];

        if (recorder_ && !animName_.empty()) {
            recorder_->Play(animName_, isAnimLoop_, isAnimRelative_);
        }
    }

    if (j.contains("eventType")) {
        eventType_ = static_cast<EventType>(j["eventType"]);
    }
    if (j.contains("enemyType")) {
        enemyType_ = j["enemyType"];
    }
    if (j.contains("blendMode")) {
        blendMode_ = static_cast<BlendMode>(j["blendMode"]);
    }
    if (j.contains("materialType")) {
        SetMaterialType(j["materialType"]);
    }
}

void Object3d::SetMaterialType(int32_t type) {
    if (materialData_) {
        materialData_->materialType = type;
    }
}

void Object3d::SetColor(const Vector4& color) {
    if (materialData_) {
        materialData_->color = color;
    }
}

void Object3d::SetShininess(float shininess) {
    if (materialData_) {
        materialData_->shininess = shininess;
    }
}

AABB Object3d::GetAABB() const {
    OBB obb = GetOBB();
    Vector3 axisX = obb.orientations[0] * obb.size.x;
    Vector3 axisY = obb.orientations[1] * obb.size.y;
    Vector3 axisZ = obb.orientations[2] * obb.size.z;

    Vector3 corners[8] = {
        obb.center - axisX - axisY - axisZ,
        obb.center + axisX - axisY - axisZ,
        obb.center - axisX + axisY - axisZ,
        obb.center + axisX + axisY - axisZ,
        obb.center - axisX - axisY + axisZ,
        obb.center + axisX - axisY + axisZ,
        obb.center - axisX + axisY + axisZ,
        obb.center + axisX + axisY + axisZ
    };

    Vector3 minPos = corners[0];
    Vector3 maxPos = corners[0];

    for (int i = 1; i < 8; ++i) {
        minPos.x = std::min(minPos.x, corners[i].x);
        minPos.y = std::min(minPos.y, corners[i].y);
        minPos.z = std::min(minPos.z, corners[i].z);

        maxPos.x = std::max(maxPos.x, corners[i].x);
        maxPos.y = std::max(maxPos.y, corners[i].y);
        maxPos.z = std::max(maxPos.z, corners[i].z);
    }

    return { minPos, maxPos };
}