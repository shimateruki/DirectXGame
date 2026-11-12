#pragma once

#include "BaseScene.h"
#include "game/system/scene/SceneGameplayAccess.h"

#include <memory>
#include <vector>

class DirectXCommon;
namespace engine { class InputManager; }
class LevelLoader;
namespace engine { class Object3DCommon; }
class ObjectManager;
namespace engine { class ParticleCommon; }
namespace engine { class ParticleSystem; }
class Player;
class Skybox;
namespace engine { class SpriteCommon; }

/// <summary>
/// Scene Assetをゲームロジックから分離して編集するための専用シーン。
/// </summary>
// 関連：BaseSceneの配置読込を使い、ゲーム固有の進行を持たない編集用シーンを作る。
// 主な入口：Initialize / Update / Finalize
class SceneAssetEditorScene : public BaseScene, public ISceneGameplayAccess {
  public:
    SceneAssetEditorScene() = default;

    ~SceneAssetEditorScene() override = default;

    /// Scene Asset を編集用に読み込み、表示基盤を初期化する。
    void Initialize() override;

    /// Editor 表示に必要なアセットを列挙する。
    SceneLoadManifest BuildAsyncLoadManifest() const override;

    /// 編集シーンが所有する配置と描画資源を解放する。
    void Finalize() override;

    /// 編集対象の配置とプレビュー表示を更新する。
    void Update(float deltaTime) override;

    /// 編集対象の 3D オブジェクトを描画する。
    void Draw() override;

    /// 編集シーンのスプライトを描画する。
    void DrawUi() override;

    /// 編集対象の影を描画する。
    void DrawShadow() override;

    std::string GetName() override { return "Scene Asset Editor"; }

    /// 現在シーンが所有する配置オブジェクトを返す。
    const std::vector<std::unique_ptr<engine::Object3D>>& GetObjects() const override;

    /// オブジェクトの追加を更新時の反映まで保留する。
    void AddObject(std::unique_ptr<engine::Object3D> object) override;

    /// オブジェクトを直ちに登録し、登録後の参照を返す。
    engine::Object3D* AddObjectImmediate(std::unique_ptr<engine::Object3D> object) override;

    /// 指定オブジェクトの所有権をシーンから取り出す。
    std::unique_ptr<engine::Object3D> TakeObjectImmediate(engine::Object3D* object) override;

    /// 指定オブジェクトの削除を予約する。
    void RequestRemoveObject(engine::Object3D* object) override;

    const std::vector<std::unique_ptr<engine::Sprite>>& GetSprites() const override { return sprites_; }


    Skybox* GetSkybox() override { return skybox_.get(); }


    engine::ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    Player* GetPlayer() const override { return player_; }

    void SetPlayer(Player* player) override { player_ = player; }

  private:
    std::vector<std::unique_ptr<engine::Sprite>>* GetMutableSprites() override { return &sprites_; }

    DirectXCommon* dxCommon_ = nullptr;
    engine::InputManager* inputManager_ = nullptr;

    std::unique_ptr<ObjectManager> objectManager_;
    std::unique_ptr<LevelLoader> levelLoader_;

    std::unique_ptr<engine::ParticleSystem> particleSystem_;
    std::unique_ptr<Skybox> skybox_;
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;

    Player* player_ = nullptr;
    uint32_t gpuParticleTextureHandle_ = 0;
};
