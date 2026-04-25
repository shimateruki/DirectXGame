#include "BaseGimmick.h"
#include "ModelManager.h"

void BaseGimmick::Initialize(Object3dCommon* common, const std::string& modelName) {
    Object3d::Initialize(common);
    
    // モデル読み込みと設定
    ModelManager::GetInstance()->LoadModel(modelName);
    SetModel(modelName);
    
    // デフォルトの当たり判定設定 (OBB想定)
    ColliderConfig config;
    config.type = ColliderType::kOBB;
    config.size = { 1.0f, 1.0f, 1.0f }; // 仮のサイズ
    SetColliderConfig(config);
    
    // 衝突属性 (必要に応じてサブクラスで上書き)
    SetCollisionAttribute(0b0010); // 例: 敵やギミックなどのオブジェクト属性
    SetCollisionMask(0b1111);      // 全てと当たる
    
    SetClassName("Gimmick");
    SetSaveCategory("Object");     // Json保存カテゴリ
}

void BaseGimmick::Update(float deltaTime) {
    Object3d::Update(deltaTime);
}

bool BaseGimmick::OnCollision(Object3d* other) {
    // 基底クラスでは特に何もしない。派生クラスで処理する
    (void)other;
    return false;
}
