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
    virtual void OnInitialize(GamePlayScene& scene) { (void)scene; }
    virtual void OnUpdate(GamePlayScene& scene, float deltaTime) {
        (void)scene;
        (void)deltaTime;
    }
    virtual void OnFinalize(GamePlayScene& scene) { (void)scene; }
};

// Scene Asset に指定された名前から、対応するシーン固有処理を生成します。
class SceneControllerFactory {
  public:
    using Creator = std::function<std::unique_ptr<ISceneController>()>;

    static SceneControllerFactory* GetInstance();

    /// <summary>名前と生成関数を登録します。同名の再登録は生成関数を置き換えます。</summary>
    bool RegisterController(const std::string& name, Creator creator);
    std::unique_ptr<ISceneController> Create(const std::string& name) const;
    bool IsRegistered(const std::string& name) const;
    const std::vector<std::string>& GetRegisteredNames() const { return registrationOrder_; }

  private:
    SceneControllerFactory();

    std::unordered_map<std::string, Creator> creators_;
    std::vector<std::string> registrationOrder_;
};
