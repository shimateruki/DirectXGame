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
class GameRule;
class Skybox;
class ScenePreloadData;
struct SceneLoadManifest;

#include "engine/utility/state/IEditable.h"
#include "SceneLoadContext.h"

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
        // ロード画面を更新しながら初期化するための段階実行インターフェースです。
        // 既定実装は従来の Initialize() を1回だけ呼ぶため、既存Sceneとの互換性を保ちます。
virtual void BeginLoadingInitialize();
virtual bool InitializeLoadingStep();
virtual float GetLoadingInitializeProgress() const;
        // 非同期初期化完了後、現在Sceneへ切り替える直前にメインスレッドで呼ばれます。
virtual void OnActivated();
        // ワーカースレッドで先読みするScene Assetと依存リソースを列挙します。
virtual SceneLoadManifest BuildAsyncLoadManifest() const;
        // シーン内のゲームロジックをフレーム時間に合わせて更新します。
virtual void Update(float deltaTime) = 0;
        // 固定刻みで動かす物理・ゲーム処理をScene内Objectへ通知します。
virtual void FixedUpdate(float fixedDeltaTime);
        // シーン内の3D/2D要素を描画します。
virtual void Draw() = 0;
        // シーン終了時にリソースや登録状態を片付けます。
virtual void Finalize() = 0;

    // --- マネージャ設定 ---
    virtual void SetSceneManager(SceneManager* sceneManager) { sceneManager_ = sceneManager; }
    virtual void SetDebugEditor(DebugEditor* editor) { debugEditor_ = editor; }
    virtual void SetSceneLoadContext(const SceneLoadContext& context) { sceneLoadContext_ = context; }
    void SetPreparedLoadData(std::shared_ptr<ScenePreloadData> data) { preparedLoadData_ = std::move(data); }
    bool TakePreparedJson(const std::string& path, json& destination);
    SceneManager* GetSceneManager() const { return sceneManager_; }
    const SceneLoadContext& GetSceneLoadContext() const { return sceneLoadContext_; }
    bool HasSceneAssetContext() const { return sceneLoadContext_.IsSceneAsset(); }
    std::string ResolveSceneBgmPath(const std::string& defaultPath) const {
        return sceneLoadContext_.bgmPath.empty() ? defaultPath : sceneLoadContext_.bgmPath;
    }
    std::string ResolveSceneLightPath(const std::string& defaultPath) const {
        return sceneLoadContext_.lightPath.empty() ? defaultPath : sceneLoadContext_.lightPath;
    }
    std::string ResolveSceneCameraPath(const std::string& defaultPath) const {
        return sceneLoadContext_.cameraPath.empty() ? defaultPath : sceneLoadContext_.cameraPath;
    }
    std::string ResolveSceneSkyboxPath(const std::string& defaultPath) const {
        return sceneLoadContext_.skyboxPath.empty() ? defaultPath : sceneLoadContext_.skyboxPath;
    }
    std::string ResolvePrimaryObjectLayoutPath(const std::string& defaultPath);
    std::string ResolvePrimarySpriteLayoutPath(const std::string& defaultPath);

    // --- オブジェクト管理 (LevelLoader / Editor 用) ---
    virtual std::vector<std::unique_ptr<Object3d>>& GetObjects() {
        static std::vector<std::unique_ptr<Object3d>> empty;
        return empty;
    }

    virtual std::vector<std::unique_ptr<Sprite>>& GetSprites() {
        static std::vector<std::unique_ptr<Sprite>> empty;
        return empty;
    }

    // ReplayDebuggerへ、Sceneが現在所有している永続Spriteを列挙します。
    // 個別unique_ptrで保持するHUDは派生Sceneで追加してください。
    virtual void CollectReplaySprites(std::vector<Sprite*>& sprites);
    virtual void CaptureReplaySceneState(json& state) const;
    virtual void RestoreReplaySceneState(const json& state);
    void ReleaseReplaySprites();

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
    virtual Skybox* GetSkybox() { return nullptr; }

    // --- Player 連携 (LevelLoader 用) ---
    virtual Player* GetPlayer() const { return nullptr; }
    virtual void SetPlayer(Player* player) { (void)player; }
    virtual GameRule* GetGameRule() { return nullptr; }

    // --- イベント連携 ---
        // targetIDに紐づくイベント受信オブジェクトへ通知します。
void TriggerEvent(int targetID);
    void SetEventActive(int targetID, bool active);
    virtual Object3d* FindObjectByEventID(int eventID);
    Object3d* FindObjectByPersistentGuid(const std::string& guid);
    const Object3d* FindObjectByPersistentGuid(const std::string& guid) const;
    /// 旧SceneのGUID不足と重複を補正します。戻り値は再発行したObject数です。
    std::size_t EnsureUniquePersistentObjectGuids();
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
    SceneLoadContext sceneLoadContext_;
    std::shared_ptr<ScenePreloadData> preparedLoadData_;
    std::string loadedFilename_ = "scene_layout.json";
    std::string loadedSpriteFilename_ = "sprite_layout.json";
    bool sceneAssetObjectLayoutConsumed_ = false;
    bool sceneAssetSpriteLayoutConsumed_ = false;
};
