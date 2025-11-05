#pragma once
#include <memory>
#include "BaseScene.h"


/// <summary>
/// シーンを管理するクラス
/// </summary>
class SceneManager {
public:
    /// <summary>
    /// デストラクタ
    /// </summary>
    ~SceneManager();

    /// <summary>
    /// 初期化 (最初のシーンを設定)
    /// </summary>
    void Initialize();

    /// <summary>
    /// 終了処理 (現在のシーンを解放)
    /// </summary>
    void Finalize();

    /// <summary>
    /// 更新 (シーン切り替え処理と、現在のシーンの更新)
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// 描画 (現在のシーンの描画)
    /// </summary>
    void Draw();

    /// <summary>
    /// 次のシーンを予約する
    /// </summary>
    /// <param name="nextScene">次に切り替えるシーンのインスタンス</param>
    void SetNextScene(std::unique_ptr<BaseScene> nextScene);

    // <summary>
    /// 現在のシーンのポインタを取得する (Editor用)
    /// </summary>
    BaseScene* GetCurrentScene() const;

private:
    std::unique_ptr<BaseScene> currentScene_ = nullptr;
    std::unique_ptr<BaseScene> nextScene_ = nullptr;
};