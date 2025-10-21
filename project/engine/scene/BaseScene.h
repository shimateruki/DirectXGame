#pragma once
#include"SceneManager.h"
/// <summary>
/// シーンの基底クラス
/// </summary>
class BaseScene {
public:
    /// <summary>
    /// デストラクタ
    /// </summary>
    virtual ~BaseScene() = default;

    /// <summary>
    /// 初期化
    /// </summary>
    virtual void Initialize() = 0;

    /// <summary>
    /// 更新
    /// </summary>
    virtual void Update() = 0;

    /// <summary>
    /// 描画
    /// </summary>
    virtual void Draw() = 0;

    virtual void Finalize() = 0;

    /// <summary>
    /// シーンマネージャのポインタを設定する（仮想関数）
    /// </summary>
    virtual void SetSceneManager(SceneManager* sceneManager) {
        sceneManager_ = sceneManager; // ポインタをメンバ変数に保持
    }

protected:
    SceneManager* sceneManager_ = nullptr;
};