#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 継承先でも頻繁に使うため、基底クラス側で共通 include しておく
#include "Object3d.h"
#include "Sprite.h"

// 前方宣言
class SceneManager;
class Object3dCommon;
class SpriteCommon;
class ParticleSystem;
class DebugEditor;
class Camera;
class DirectXCommon;
class BulletManager;
class Player;

#include "engine/utility/state/IEditable.h"

/// <summary>
/// すべてのシーンが実装する基底クラス。
/// </summary>
class BaseScene : public IEditable {
public:
    virtual ~BaseScene() = default;

    // --- IEditable ---
    virtual std::string GetName() override { return "Scene Settings"; }
    virtual void DrawImGui() override {}

    // --- 必須オーバーライド ---
    virtual void Initialize() = 0;
    virtual void Update(float deltaTime) = 0;
    virtual void Draw() = 0;
    virtual void Finalize() = 0;

    // --- マネージャ設定 ---
    virtual void SetSceneManager(SceneManager* sceneManager) { sceneManager_ = sceneManager; }
    virtual void SetDebugEditor(DebugEditor* editor) { debugEditor_ = editor; }

    // --- オブジェクト管理 (LevelLoader / Editor 用) ---
    virtual std::vector<std::unique_ptr<Object3d>>& GetObjects() {
        static std::vector<std::unique_ptr<Object3d>> empty;
        return empty;
    }

    virtual std::vector<std::unique_ptr<Sprite>>& GetSprites() {
        static std::vector<std::unique_ptr<Sprite>> empty;
        return empty;
    }

    virtual void AddObject(std::unique_ptr<Object3d> object) { (void)object; }
    virtual void RequestRemoveObject(Object3d* object) { (void)object; }
    bool Destroy(Object3d* object);
    bool Destroy(Sprite* sprite);
    bool DestroyObject(Object3d* object);
    bool DestroySprite(Sprite* sprite);
    bool IsAlive(Object3d* object);
    bool IsAlive(Sprite* sprite);
    virtual void RefreshRenderCameraData();

    // --- 共通リソース取得 ---
    virtual Object3dCommon* GetObject3dCommon() { return nullptr; }
    virtual SpriteCommon* GetSpriteCommon() { return nullptr; }
    virtual ParticleSystem* GetParticleSystem() { return nullptr; }

    // --- Player 連携 (LevelLoader 用) ---
    virtual Player* GetPlayer() const { return nullptr; }
    virtual void SetPlayer(Player* player) { (void)player; }

    // --- イベント連携 ---
    void TriggerEvent(int targetID);
    void SetEventActive(int targetID, bool active);
    virtual Object3d* FindObjectByEventID(int eventID);
    virtual void DrawUI() {}

    void SetLoadedFilename(const std::string& name) { loadedFilename_ = name; }
    std::string GetLoadedFilename() const { return loadedFilename_; }
    virtual void DrawShadow() {}
    Sprite* GetSpriteByName(const std::string& name);
    void SetLoadedSpriteFilename(const std::string& name) { loadedSpriteFilename_ = name; }
    std::string GetLoadedSpriteFilename() const { return loadedSpriteFilename_; }

protected:
    bool IsSpecialMaterialType(int materialType) const;
    bool IsHiddenByFirstPerson(Object3d* object, Player* player, bool isFirstPerson) const;
    bool DrawLocalFogObjects(std::vector<std::unique_ptr<Object3d>>& objects, DirectXCommon* dxCommon, Player* player = nullptr, bool isFirstPerson = false);
    bool DrawSpecialMaterialObjects(std::vector<std::unique_ptr<Object3d>>& objects, DirectXCommon* dxCommon, BulletManager* bulletManager = nullptr, Player* player = nullptr, bool isFirstPerson = false);
    bool DrawGPUParticles(DirectXCommon* dxCommon, Camera* camera, uint32_t textureHandle, bool grabAlreadyUpdated = false);

    SceneManager* sceneManager_ = nullptr;
    DebugEditor* debugEditor_ = nullptr;
    std::string loadedFilename_ = "scene_layout.json";
    std::string loadedSpriteFilename_ = "sprite_layout.json";
};
