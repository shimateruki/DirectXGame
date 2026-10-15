#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class GamePlayScene;

// GamePlaySceneを共有したまま、Scene Asset固有の処理だけを差し替えます。
class ISceneController {
  public:
    virtual ~ISceneController() = default;
    /// 共通 GamePlayScene の初期化後に、Scene Asset 固有の設定を適用する。
    virtual void OnInitialize(GamePlayScene& scene) { (void)scene; }
    /// 共通更新の中で、Scene Asset 固有の進行を更新する。
    virtual void OnUpdate(GamePlayScene& scene, float deltaTime) {
        (void)scene;
        (void)deltaTime;
    }
    /// Scene Asset 固有の保持状態を片付ける。
    virtual void OnFinalize(GamePlayScene& scene) { (void)scene; }
};

// Scene Asset に指定された名前から、対応するシーン固有処理を生成します。
class SceneControllerFactory {
  public:
    using Creator = std::function<std::unique_ptr<ISceneController>()>;

    /// 登録済み Controller を保持する共有 Factory を返す。
    static SceneControllerFactory* GetInstance();

    /// <summary>名前と生成関数を登録します。同名の再登録は生成関数を置き換えます。</summary>
    bool RegisterController(const std::string& name, Creator creator);
    /// 登録名から Controller を生成する。未登録名では空のポインタを返す。
    std::unique_ptr<ISceneController> Create(const std::string& name) const;
    /// 指定した名前の生成関数が登録済みか返す。
    bool IsRegistered(const std::string& name) const;
    const std::vector<std::string>& GetRegisteredNames() const { return registrationOrder_; }

  private:
    SceneControllerFactory();

    std::unordered_map<std::string, Creator> creators_;
    std::vector<std::string> registrationOrder_;
};
