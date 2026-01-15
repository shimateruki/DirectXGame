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

OBB Object3d::GetOBB() const {
    OBB obb;
    Math math;

    // =========================================================
    // 1. コライダー自身の「ローカル行列」を作る
    // =========================================================
    // ここで colliderConfig_.rotation を使って回転行列を作ります

    // 回転 (Z * X * Y 順など、エンジンの仕様に合わせますが基本はこれ)
    Matrix4x4 matRotX = math.MakeRotateXMatrix(colliderConfig_.rotation.x);
    Matrix4x4 matRotY = math.MakeRotateYMatrix(colliderConfig_.rotation.y);
    Matrix4x4 matRotZ = math.MakeRotateZMatrix(colliderConfig_.rotation.z);
    Matrix4x4 matRot = math.Multiply(matRotZ, math.Multiply(matRotX, matRotY));

    // 中心ズレ (Center)
    Matrix4x4 matTrans = math.MakeTranslateMatrix(colliderConfig_.center);

    // コライダー単体の行列 (回転させてから、ズラス)
    Matrix4x4 matColliderLocal = math.Multiply(matRot, matTrans);


    // =========================================================
    // 2. オブジェクトの「ワールド行列」と合成する
    // =========================================================
    // これで [親の回転] + [子の回転] が合わさった最終的な行列になります
    Matrix4x4 matFinal = math.Multiply(matColliderLocal, worldMatrix_);


    // =========================================================
    // 3. 行列から OBB の情報を抜き出す
    // =========================================================

    // A. 中心座標 (行列の平行移動成分 [3][0]~[3][2])
    obb.center = { matFinal.m[3][0], matFinal.m[3][1], matFinal.m[3][2] };

    // B. 3つの軸 (行列の回転成分 X, Y, Z軸)
    obb.orientations[0] = math.Normalize({ matFinal.m[0][0], matFinal.m[0][1], matFinal.m[0][2] }); // X軸
    obb.orientations[1] = math.Normalize({ matFinal.m[1][0], matFinal.m[1][1], matFinal.m[1][2] }); // Y軸
    obb.orientations[2] = math.Normalize({ matFinal.m[2][0], matFinal.m[2][1], matFinal.m[2][2] }); // Z軸

    // C. サイズ (半サイズ)
    // コライダーの元サイズ * オブジェクトのスケール
    obb.size = {
        colliderConfig_.size.x * transform_.scale.x,
        colliderConfig_.size.y * transform_.scale.y,
        colliderConfig_.size.z * transform_.scale.z
    };

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


void Object3d::CopyFrom(const Object3d* other) {
    if (!other) return;

    // --- 基本情報のコピー ---
    if (!other->modelName_.empty()) {
        this->SetModel(other->modelName_);
    }
    this->name_ = other->name_;

    // Transform
    this->transform_ = other->transform_;

    // --- コライダー & 物理 ---
    this->SetColliderConfig(other->colliderConfig_);
    this->collisionAttribute_ = other->collisionAttribute_;
    this->collisionMask_ = other->collisionMask_;

    // 1. クラス名
    this->className_ = other->className_;

    // 2. 可視性
    this->isVisible_ = other->isVisible_;

    // 3. イベントタイプ
    this->eventType_ = other->eventType_;
    this->enemyType_ = other->enemyType_;

    // 4. パラメータ
    this->param_ = other->param_;

    // アニメーション設定のコピー
    this->animName_ = other->animName_;
    this->isAnimLoop_ = other->isAnimLoop_;
    this->isAnimRelative_ = other->isAnimRelative_;

    // レコーダー初期化
    this->InitializeRecorder(nullptr);

    // 設定が入っていれば、即座に再生を開始させる
    if (!this->animName_.empty() && this->recorder_) {
        this->recorder_->Play(
            this->animName_,
            this->isAnimLoop_,
            this->isAnimRelative_
        );
    }
}

// Cloneはシンプルに
std::unique_ptr<Object3d> Object3d::Clone() const {
    auto newObj = std::make_unique<Object3d>();

    // 初期化
    assert(common_ != nullptr);
    newObj->Initialize(common_);
    // 中身をコピー 
    newObj->CopyFrom(this);
    return newObj;
}

// ---------------------------------------------------------
// 自身の情報をJSONデータとして出力（プリセット保存用）
// ---------------------------------------------------------
json Object3d::ExportToJson() {
    json j;

    // 1. 基本情報
    j["name"] = name_;
    j["modelName"] = modelName_;

    // 2. Transform (位置は配置時に決めるので保存しないが、スケールと回転は必須)
    j["scale"] = { transform_.scale.x, transform_.scale.y, transform_.scale.z };
    j["rotate"] = { transform_.rotate.x, transform_.rotate.y, transform_.rotate.z };

    // 3. コライダー設定 (CollisionConfig)
    // ここが大事！サイズだけでなく、位置ズレ(center)や種類も保存する
    j["collider"] = {
        {"type", static_cast<int>(colliderConfig_.type)},
        {"size", { colliderConfig_.size.x, colliderConfig_.size.y, colliderConfig_.size.z }},
        {"center", { colliderConfig_.center.x, colliderConfig_.center.y, colliderConfig_.center.z }},
        { "rotation", { colliderConfig_.rotation.x, colliderConfig_.rotation.y, colliderConfig_.rotation.z } }
    };

    // 4. アニメーション設定
    // これを保存しないと、配置した瞬間に棒立ちになったり、ループしなかったりする
    j["animation"] = {
        {"animName", animName_},
        {"isAnimLoop", isAnimLoop_},
        {"isAnimRelative", isAnimRelative_}
    };

    // 5. ゲームロジック用パラメータ
    j["eventType"] = static_cast<int>(eventType_);
    j["enemyType"] = enemyType_;

    return j;
}

// ---------------------------------------------------------
// JSONデータから設定を読み込んで反映（プリセット適用用）
// ---------------------------------------------------------
void Object3d::ImportFromJson(const json& j) {
    // 1. 基本情報
    if (j.contains("modelName")) {
        modelName_ = j["modelName"];
        // モデルが変わるなら再ロードが必要かもしれない（設計による）
        // ModelManager::Load(modelName_); 
    }
    // 名前は上書きしない（配置時にユニークな名前をつけることが多いため）
    // if (j.contains("name")) name_ = j["name"]; 

    // 2. Transform
    if (j.contains("scale")) {
        transform_.scale = { j["scale"][0], j["scale"][1], j["scale"][2] };
    }
    if (j.contains("rotate")) {
        transform_.rotate = { j["rotate"][0], j["rotate"][1], j["rotate"][2] };
    }

    // 3. コライダー設定
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

    // 4. アニメーション設定
    if (j.contains("animation")) {
        const auto& anim = j["animation"];
        if (anim.contains("animName")) animName_ = anim["animName"];
        if (anim.contains("isAnimLoop")) isAnimLoop_ = anim["isAnimLoop"];
        if (anim.contains("isAnimRelative")) isAnimRelative_ = anim["isAnimRelative"];

        //  アニメーション設定を読み込んだら、Recorder側にも反映・再生開始が必要
        if (recorder_ && !animName_.empty()) {
            recorder_->Play(animName_, isAnimLoop_, isAnimRelative_);
        }
    }

    // 5. ゲームロジック用パラメータ
    if (j.contains("eventType")) {
        eventType_ = static_cast<EventType>(j["eventType"]);
    }
    if (j.contains("enemyType")) {
        enemyType_ = j["enemyType"];
	}
}