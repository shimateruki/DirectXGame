#pragma once

#include <string>

// シーンの基底クラス（インターフェース）
class BaseScene {
public:
    virtual ~BaseScene() = default;

    /// <summary>
    /// 初期化
    /// </summary>
    virtual void Initialize() = 0;

    /// <summary>
    /// 終了処理
    /// </summary>
    virtual void Finalize() = 0;

    /// <summary>
    /// 毎フレーム更新
    /// </summary>
    virtual void Update() = 0;

    /// <summary>
    /// 描画
    /// </summary>
    virtual void Draw() = 0;

    /// <summary>
    /// 次のシーンをリクエスト
    /// </summary>
    /// <param name="sceneName">次のシーン名</param>
    void RequestNextScene(const std::string& sceneName) { nextSceneName_ = sceneName; }

    /// <summary>
    /// 次のシーン名を取得
    /// </summary>
    /// <returns>次のシーン名</returns>
    const std::string& GetNextSceneName() const { return nextSceneName_; }

protected:
    std::string nextSceneName_; // 次のシーン名
};