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
// BaseSceneは、各ゲームシーンが共通して持つ初期化、更新、描画、保存対象アクセスの土台です。
class BaseScene : public IEditable {
public:
    virtual ~BaseScene() = default;

    // --- IEditable ---
    virtual std::string GetName() override { return "Scene Settings"; }
    virtual void DrawImGui() override {}

    // --- 必須オーバーライド ---
        // シーン固有のオブジェクト、カメラ、UI、管理クラスを初期化します。
virtual void Initialize() = 0;
        // シーン内のゲームロジックをフレーム時間に合わせて更新します。
virtual void Update(float deltaTime) = 0;
        // シーン内の3D/2D要素を描画します。
virtual void Draw() = 0;
        // シーン終了時にリソースや登録状態を片付けます。
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

        // エディタやロード処理からObject3dを追加するための入口です。
virtual void AddObject(std::unique_ptr<Object3d> object) { (void)object; }
    virtual void RequestRemoveObject(Object3d* object) { (void)object; }
        // Object3dを安全に削除予約または削除します。
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
        // targetIDに紐づくイベント受信オブジェクトへ通知します。
void TriggerEvent(int targetID);
    void SetEventActive(int targetID, bool active);
    virtual Object3d* FindObjectByEventID(int eventID);
    virtual void DrawUI() {}
    virtual void DrawCameraPreview(Camera* camera, int previewBufferIndex = 0);

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
        // 水、炎、ポータルなど通常描画と分けたい特殊マテリアルをまとめて描画します。
bool DrawSpecialMaterialObjects(std::vector<std::unique_ptr<Object3d>>& objects, DirectXCommon* dxCommon, BulletManager* bulletManager = nullptr, Player* player = nullptr, bool isFirstPerson = false);
        // シーンのカメラ情報を使ってGPUパーティクルを描画します。
bool DrawGPUParticles(DirectXCommon* dxCommon, Camera* camera, uint32_t textureHandle, bool grabAlreadyUpdated = false);

    SceneManager* sceneManager_ = nullptr;
    DebugEditor* debugEditor_ = nullptr;
    std::string loadedFilename_ = "scene_layout.json";
    std::string loadedSpriteFilename_ = "sprite_layout.json";
};
