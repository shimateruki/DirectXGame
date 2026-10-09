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

/// <summary>シーンの初期化、更新、描画と Editor・ロード連携の共通入口です。</summary>
class BaseScene : public IEditable {
public:
    virtual ~BaseScene() = default;

    /// <summary>Editor で表示するシーン設定名を返します。</summary>
    virtual std::string GetName() override { return "Scene Settings"; }

    /// <summary>派生シーンの設定 UI を描画します。</summary>
    virtual void DrawImGui() override {}

    /// <summary>シーン固有のオブジェクト、カメラ、UI を初期化します。</summary>
    virtual void Initialize() = 0;

    /// <summary>ロード画面と並行する段階初期化を開始します。</summary>
    /// <remarks>既定実装は何もしません。次の InitializeLoadingStep() が Initialize() を一度呼びます。</remarks>
    virtual void BeginLoadingInitialize();

    /// <summary>段階初期化を一段進め、完了状態を返します。</summary>
    virtual bool InitializeLoadingStep();

    /// <summary>段階初期化の進行割合を返します。</summary>
    virtual float GetLoadingInitializeProgress() const;

    /// <summary>先読み後、現行シーンへ切り替える直前にメインスレッドで呼ばれます。</summary>
    virtual void OnActivated();

    /// <summary>ワーカーで先読みする Scene Asset と依存リソースを列挙します。</summary>
    virtual SceneLoadManifest BuildAsyncLoadManifest() const;

    /// <summary>シーンのゲームロジックをフレーム時間で更新します。</summary>
    virtual void Update(float deltaTime) = 0;

    /// <summary>固定刻みの物理・ゲーム処理を更新します。</summary>
    virtual void FixedUpdate(float fixedDeltaTime);

    /// <summary>シーンの 3D・2D 要素を描画します。</summary>
    virtual void Draw() = 0;

    /// <summary>登録状態とシーン固有リソースを解放します。</summary>
    virtual void Finalize() = 0;

    // SceneManager と Editor は非所有参照、先読み結果は共有所有します。
    virtual void SetSceneManager(SceneManager* sceneManager) { sceneManager_ = sceneManager; }
    virtual void SetDebugEditor(DebugEditor* editor) { debugEditor_ = editor; }
    virtual void SetSceneLoadContext(const SceneLoadContext& context) { sceneLoadContext_ = context; }
    void SetPreparedLoadData(std::shared_ptr<ScenePreloadData> data) { preparedLoadData_ = std::move(data); }

    /// <summary>先読み済み JSON を取得し、先読み結果から削除します。</summary>
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

    /// <summary>Scene Assetのオブジェクト配置パスを初回だけ使い、以後は既定パスを返す。</summary>
    std::string ResolvePrimaryObjectLayoutPath(const std::string& defaultPath);

    /// <summary>Scene Assetのスプライト配置パスを初回だけ使い、以後は既定パスを返す。</summary>
    std::string ResolvePrimarySpriteLayoutPath(const std::string& defaultPath);

    // LevelLoader と Editor が派生シーンの所有コンテナへアクセスするための窓口です。
    virtual std::vector<std::unique_ptr<Object3d>>& GetObjects() {
        static std::vector<std::unique_ptr<Object3d>> empty;
        return empty;
    }

    virtual std::vector<std::unique_ptr<Sprite>>& GetSprites() {
        static std::vector<std::unique_ptr<Sprite>> empty;
        return empty;
    }

    /// <summary>ReplayDebugger 向けに、シーンが所有する Sprite を列挙します。</summary>
    /// <remarks>個別の unique_ptr で保持する HUD は派生シーンで追加してください。</remarks>
    virtual void CollectReplaySprites(std::vector<Sprite*>& sprites);

    /// <summary>Replay 用のシーン固有状態を保存します。</summary>
    virtual void CaptureReplaySceneState(json& state) const;

    /// <summary>Replay 用に保存したシーン固有状態を復元します。</summary>
    virtual void RestoreReplaySceneState(const json& state);

    /// <summary>Replay が保持する Sprite 参照を解放します。</summary>
    void ReleaseReplaySprites();

    /// <summary>Editor やロード処理から Object3d を追加する入口です。</summary>
    virtual void AddObject(std::unique_ptr<Object3d> object) { (void)object; }

    /// <summary>派生シーンが管理するオブジェクトの削除を予約する拡張点。</summary>
    virtual void RequestRemoveObject(Object3d* object) { (void)object; }

    /// <summary>Replay に記録した動的 Object3d を派生シーンで再生成します。</summary>
    virtual std::unique_ptr<Object3d> CreateReplayObject(const json& descriptor) {
        (void)descriptor;
        return nullptr;
    }

    /// <summary>Replay の動的 Object3d 再生成後、シーン固有の参照を接続し直します。</summary>
    virtual void OnReplayObjectsRecreated() {}

    /// <summary>Object3d を安全に削除予約または削除します。</summary>
    // 破棄と生存確認は、シーン所有コンテナ上のObject3d／Spriteを対象とする。
    /// <summary>指定オブジェクトをシーンから安全に取り除く。</summary>
    bool Destroy(Object3d* object);

    /// <summary>指定スプライトをシーンから安全に取り除く。</summary>
    bool Destroy(Sprite* sprite);

    /// <summary>Object3d用の破棄処理を行う。</summary>
    bool DestroyObject(Object3d* object);

    /// <summary>Sprite用の破棄処理を行う。</summary>
    bool DestroySprite(Sprite* sprite);

    /// <summary>オブジェクトがこのシーンで有効か返す。</summary>
    bool IsAlive(Object3d* object);

    /// <summary>スプライトがこのシーンで有効か返す。</summary>
    bool IsAlive(Sprite* sprite);

    /// <summary>シーンの描画対象に現在のカメラ情報を反映する。</summary>
    virtual void RefreshRenderCameraData();

    // 派生シーンが所有する描画・演出リソースを、共通コードへ公開します。
    virtual Object3dCommon* GetObject3dCommon() { return nullptr; }
    virtual SpriteCommon* GetSpriteCommon() { return nullptr; }
    virtual ParticleSystem* GetParticleSystem() { return nullptr; }
    virtual Skybox* GetSkybox() { return nullptr; }

    // LevelLoader とゲーム進行処理向けの Player・ルール参照です。
    virtual Player* GetPlayer() const { return nullptr; }
    virtual void SetPlayer(Player* player) { (void)player; }
    virtual GameRule* GetGameRule() { return nullptr; }

    /// <summary>targetID に紐づくイベント受信オブジェクトへ通知します。</summary>
    void TriggerEvent(int targetID);

    /// <summary>指定IDへ接続されたイベントの有効状態を切り替える。</summary>
    void SetEventActive(int targetID, bool active);

    /// <summary>イベントIDに対応するオブジェクトを探す。</summary>
    virtual Object3d* FindObjectByEventID(int eventID);

    /// <summary>永続GUIDでシーン内のオブジェクトを探す。</summary>
    Object3d* FindObjectByPersistentGuid(const std::string& guid);

    /// <summary>永続GUIDでシーン内のオブジェクトを読み取り専用で探す。</summary>
    const Object3d* FindObjectByPersistentGuid(const std::string& guid) const;

    /// <summary>旧シーンの GUID 不足と重複を補正します。</summary>
    /// <returns>GUID を再発行した Object3d の数。</returns>
    std::size_t EnsureUniquePersistentObjectGuids();

    /// <summary>派生シーンのUI描画を行う拡張点。</summary>
    virtual void DrawUI() {}

    /// <summary>指定カメラから見たシーンを独立バッファへ描画する。</summary>
    virtual void DrawCameraPreview(Camera* camera, int previewBufferIndex = 0);

    void SetLoadedFilename(const std::string& name) { loadedFilename_ = name; }
    std::string GetLoadedFilename() const { return loadedFilename_; }
    virtual void DrawShadow() {}

    /// <summary>名前に一致するシーン内スプライトを探す。</summary>
    Sprite* GetSpriteByName(const std::string& name);
    void SetLoadedSpriteFilename(const std::string& name) { loadedSpriteFilename_ = name; }
    std::string GetLoadedSpriteFilename() const { return loadedSpriteFilename_; }

protected:
    // 特殊描画対象の判定と、一人称視点で隠すオブジェクトの除外に使う。
    bool IsSpecialMaterialType(int materialType) const;
    bool IsHiddenByFirstPerson(Object3d* object, Player* player, bool isFirstPerson) const;

    // 敵の予兆を不透明モデルの後、パーティクルや特殊材質の前に描画します。
    void DrawEnemyTelegraphs(ID3D12Resource* pointLight, ID3D12Resource* spotLight);

    /// <summary>対象一覧からローカルフォグを描画し、描画したか返す。</summary>
    bool DrawLocalFogObjects(std::vector<std::unique_ptr<Object3d>>& objects, DirectXCommon* dxCommon, Player* player = nullptr, bool isFirstPerson = false);

    // 水、炎、ポータルなど通常パスと分ける特殊マテリアルを描画します。
    bool DrawSpecialMaterialObjects(std::vector<std::unique_ptr<Object3d>>& objects, DirectXCommon* dxCommon, BulletManager* bulletManager = nullptr, Player* player = nullptr, bool isFirstPerson = false);

    // シーンカメラに合わせて GPU パーティクルを描画します。
    bool DrawGPUParticles(DirectXCommon* dxCommon, Camera* camera, uint32_t textureHandle, bool grabAlreadyUpdated = false);

    // 各マネージャーは非所有、事前ロード結果はシーンで共有所有します。
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* debugEditor_ = nullptr;
    SceneLoadContext sceneLoadContext_;
    std::shared_ptr<ScenePreloadData> preparedLoadData_;
    std::string loadedFilename_ = "scene_layout.json";
    std::string loadedSpriteFilename_ = "sprite_layout.json";
    bool sceneAssetObjectLayoutConsumed_ = false;
    bool sceneAssetSpriteLayoutConsumed_ = false;
};
