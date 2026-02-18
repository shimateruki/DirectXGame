#pragma once

class GameObject;

class Component {
public:
    virtual ~Component() = default;

    // 初期化処理
    virtual void Initialize() {}

    // 更新処理
    virtual void Update(float deltaTime) {}

    // 描画処理 
    virtual void Draw() {}

    // 衝突判定
    virtual void OnCollision(GameObject* other) {}

public:
    // このコンポーネントを持っている持ち主 
    GameObject* owner_ = nullptr;
};