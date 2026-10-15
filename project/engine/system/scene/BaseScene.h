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
namespace engine { class SpriteCommon; }
class ParticleSystem;
class DebugEditor;
class Camera;
class DirectXCommon;
class BulletManager;
class Skybox;
class ScenePreloadData;
struct SceneLoadManifest;

#include "engine/utility/state/IEditable.h"
#include "SceneLoadContext.h"

/// <summary>シーンの初期化、更新、描画と Editor・ロード連携の共通入口です。</summary>
class BaseScene : public IEditable {
public:
    /// <summary>派生シーンを基底ポインタから安全に破棄する。</summary>
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

    /// <summary>シーン遷移を管理するSceneManagerの非所有参照を設定する。</summary>
    virtual void SetSceneManager(SceneManager* sceneManager) { sceneManager_ = sceneManager; }
    /// <summary>シーン編集を行うDebugEditorの非所有参照を設定する。</summary>
    virtual void SetDebugEditor(DebugEditor* editor) { debugEditor_ = editor; }
    /// <summary>Scene Assetと実行クラスの対応など、ロード時の文脈を設定する。</summary>
    virtual void SetSceneLoadContext(const SceneLoadContext& context) { sceneLoadContext_ = context; }
    /// <summary>非同期で準備したリソースへの共有参照を保持する。</summary>
    void SetPreparedLoadData(std::shared_ptr<ScenePreloadData> data) { preparedLoadData_ = std::move(data); }

    /// <summary>先読み済み JSON を取得し、先読み結果から削除します。</summary>
    /// <param name="path">先読み時に登録したJSONパス。</param>
    /// <param name="destination">見つかったJSONの書き込み先。</param>
    /// <returns>対応する先読みデータを受け取れた場合はtrue。</returns>
    bool TakePreparedJson(const std::string& path, json& destination);
    /// <summary>このシーンへ設定されたSceneManagerの非所有参照を返す。</summary>
    SceneManager* GetSceneManager() const { return sceneManager_; }
    /// <summary>現在のScene Assetロード文脈を返す。</summary>
    const SceneLoadContext& GetSceneLoadContext() const { return sceneLoadContext_; }
    /// <summary>Scene Asset経由で開始したシーンかどうかを返す。</summary>
    bool HasSceneAssetContext() const { return sceneLoadContext_.IsSceneAsset(); }

    /// <summary>Scene AssetにBGM指定があればそれを、なければ既定パスを返す。</summary>
    std::string ResolveSceneBgmPath(const std::string& defaultPath) const {
        return sceneLoadContext_.bgmPath.empty() ? defaultPath : sceneLoadContext_.bgmPath;
    }
    /// <summary>Scene Assetのライト設定パスを優先し、未指定なら既定パスを返す。</summary>
    std::string ResolveSceneLightPath(const std::string& defaultPath) const {
        return sceneLoadContext_.lightPath.empty() ? defaultPath : sceneLoadContext_.lightPath;
    }
    /// <summary>Scene Assetのカメラ設定パスを優先し、未指定なら既定パスを返す。</summary>
    std::string ResolveSceneCameraPath(const std::string& defaultPath) const {
        return sceneLoadContext_.cameraPath.empty() ? defaultPath : sceneLoadContext_.cameraPath;
    }
    /// <summary>Scene Assetのスカイボックスパスを優先し、未指定なら既定パスを返す。</summary>
    std::string ResolveSceneSkyboxPath(const std::string& defaultPath) const {
        return sceneLoadContext_.skyboxPath.empty() ? defaultPath : sceneLoadContext_.skyboxPath;
    }

    /// <summary>Scene Assetのオブジェクト配置パスを初回だけ使い、以後は既定パスを返す。</summary>
    std::string ResolvePrimaryObjectLayoutPath(const std::string& defaultPath);

    /// <summary>Scene Assetのスプライト配置パスを初回だけ使い、以後は既定パスを返す。</summary>
    std::string ResolvePrimarySpriteLayoutPath(const std::string& defaultPath);

    /// <summary>派生シーンが所有する3Dオブジェクトを返す。基底の既定実装は空の一覧。</summary>
    virtual const std::vector<std::unique_ptr<engine::Object3d>>& GetObjects() const {
        static const std::vector<std::unique_ptr<engine::Object3d>> empty;
        return empty;
    }

    /// <summary>派生シーンが所有するSpriteを返す。基底の既定実装は空の一覧。</summary>
    virtual const std::vector<std::unique_ptr<engine::Sprite>>& GetSprites() const {
        static const std::vector<std::unique_ptr<engine::Sprite>> empty;
        return empty;
    }

    /// <summary>Spriteの所有権をシーンへ移し、編集用の非所有参照を返す。</summary>
    /// <returns>Spriteを受け入れた場合はそのポインタ。所有コンテナがない場合はnullptr。</returns>
    engine::Sprite* AddSprite(std::unique_ptr<engine::Sprite> sprite);

    /// <summary>Spriteの描画順を隣と入れ替える。</summary>
    /// <param name="offset">前面へ移す場合は+1、背面へ移す場合は-1。</param>
    /// <returns>対象があり、指定方向へ移動できた場合はtrue。</returns>
    bool MoveSprite(engine::Sprite* sprite, int offset);

    /// <summary>ReplayDebugger 向けに、シーンが所有する Sprite を列挙します。</summary>
    /// <remarks>個別の unique_ptr で保持する HUD は派生シーンで追加してください。</remarks>
    virtual void CollectReplaySprites(std::vector<engine::Sprite*>& sprites);

    /// <summary>Replay 用のシーン固有状態を保存します。</summary>
    virtual void CaptureReplaySceneState(json& state) const;

    /// <summary>Replay 用に保存したシーン固有状態を復元します。</summary>
    virtual void RestoreReplaySceneState(const json& state);

    /// <summary>Replay が保持する Sprite 参照を解放します。</summary>
    void ReleaseReplaySprites();

    /// <summary>Editor やロード処理から Object3d を追加する入口です。</summary>
    virtual void AddObject(std::unique_ptr<engine::Object3d> object);

    /// <summary>EditorとReplayが同じフレームで参照できるよう、Object3dを即時登録する。</summary>
    /// <returns>登録したObject3dの非所有参照。</returns>
    /// <remarks>Object3dを所有しない基底の既定実装は例外を投げる。</remarks>
    virtual engine::Object3d* AddObjectImmediate(std::unique_ptr<engine::Object3d> object);

    /// <summary>UndoやPrefab編集のため、Object3dをシーンから外して所有権を返す。</summary>
    /// <returns>取り出したObject3d。見つからない場合はnullptr。</returns>
    virtual std::unique_ptr<engine::Object3d> TakeObjectImmediate(engine::Object3d* object);

    /// <summary>派生シーンが管理するオブジェクトの削除を予約する拡張点。</summary>
    virtual void RequestRemoveObject(engine::Object3d* object) { (void)object; }

    /// <summary>Replay に記録した動的 Object3d を派生シーンで再生成します。</summary>
    virtual std::unique_ptr<engine::Object3d> CreateReplayObject(const json& descriptor) {
        (void)descriptor;
        return nullptr;
    }

    /// <summary>Replay の動的 Object3d 再生成後、シーン固有の参照を接続し直します。</summary>
    virtual void OnReplayObjectsRecreated() {}

    /// <summary>指定オブジェクトの生存を確認し、削除を予約する。</summary>
    /// <returns>対象がこのシーンに存在し、削除を予約できた場合はtrue。</returns>
    bool Destroy(engine::Object3d* object);

    /// <summary>指定Spriteを削除する。Replay保持中なら非表示にして記録上の削除とする。</summary>
    /// <returns>対象がこのシーンに存在した場合はtrue。</returns>
    bool Destroy(engine::Sprite* sprite);

    /// <summary>所有中のObject3dを非表示にし、走査後の破棄を予約する。</summary>
    bool DestroyObject(engine::Object3d* object);

    /// <summary>所有中のSpriteをReplayの保持状態に合わせて削除する。</summary>
    bool DestroySprite(engine::Sprite* sprite);

    /// <summary>Object3dのポインタがこのシーンの所有配列にあるか返す。</summary>
    bool IsAlive(engine::Object3d* object);

    /// <summary>Spriteがこのシーンに属し、Replay上でも削除されていないか返す。</summary>
    bool IsAlive(engine::Sprite* sprite);

    /// <summary>シーンの描画対象に現在のカメラ情報を反映する。</summary>
    virtual void RefreshRenderCameraData();

    /// <summary>派生シーンが所有する3D描画基盤への非所有参照を返す。</summary>
    virtual Object3dCommon* GetObject3dCommon() { return nullptr; }
    /// <summary>派生シーンが所有するSprite描画基盤への非所有参照を返す。</summary>
    virtual engine::SpriteCommon* GetSpriteCommon() { return nullptr; }
    /// <summary>派生シーンが所有する粒子システムへの非所有参照を返す。</summary>
    virtual ParticleSystem* GetParticleSystem() { return nullptr; }
    /// <summary>派生シーンが所有するスカイボックスへの非所有参照を返す。</summary>
    virtual Skybox* GetSkybox() { return nullptr; }

    /// <summary>targetID に紐づくイベント受信オブジェクトへ通知します。</summary>
    void TriggerEvent(int targetID);

    /// <summary>指定IDへ接続されたイベントの有効状態を切り替える。</summary>
    void SetEventActive(int targetID, bool active);

    /// <summary>イベントIDに対応するオブジェクトを探す。</summary>
    virtual engine::Object3d* FindObjectByEventID(int eventID);

    /// <summary>永続GUIDでシーン内のオブジェクトを探す。</summary>
    engine::Object3d* FindObjectByPersistentGuid(const std::string& guid);

    /// <summary>永続GUIDでシーン内のオブジェクトを読み取り専用で探す。</summary>
    const engine::Object3d* FindObjectByPersistentGuid(const std::string& guid) const;

    /// <summary>旧シーンの GUID 不足と重複を補正します。</summary>
    /// <returns>GUID を再発行した Object3d の数。</returns>
    std::size_t EnsureUniquePersistentObjectGuids();

    /// <summary>派生シーンのUI描画を行う拡張点。</summary>
    virtual void DrawUI() {}

    /// <summary>指定カメラから見たシーンを独立バッファへ描画する。</summary>
    virtual void DrawCameraPreview(Camera* camera, int previewBufferIndex = 0);

    /// <summary>現在読み込んだ3D配置ファイル名を設定・取得する。</summary>
    void SetLoadedFilename(const std::string& name) { loadedFilename_ = name; }
    std::string GetLoadedFilename() const { return loadedFilename_; }
    /// <summary>派生シーンが影用パスを描く拡張点。基底では何もしない。</summary>
    virtual void DrawShadow() {}

    /// <summary>名前に一致するシーン内スプライトを探す。</summary>
    engine::Sprite* GetSpriteByName(const std::string& name);
    /// <summary>現在読み込んだSprite配置ファイル名を設定・取得する。</summary>
    void SetLoadedSpriteFilename(const std::string& name) { loadedSpriteFilename_ = name; }
    std::string GetLoadedSpriteFilename() const { return loadedSpriteFilename_; }

protected:
    /// <summary>派生シーンの可変Sprite所有コンテナを返す。基底ではnullptr。</summary>
    virtual std::vector<std::unique_ptr<engine::Sprite>>* GetMutableSprites() { return nullptr; }

    /// <summary>通常の不透明パスから分けて描く材質か判定する。</summary>
    bool IsSpecialMaterialType(int materialType) const;
    /// <summary>一人称視点で視点Actorまたはその子として隠す対象か判定する。</summary>
    bool IsHiddenByFirstPerson(engine::Object3d* object, engine::Object3d* viewActor, bool isFirstPerson) const;

    // 敵の予兆を不透明モデルの後、パーティクルや特殊材質の前に描画します。

    /// <summary>対象一覧からローカルフォグを描画し、描画したか返す。</summary>
    bool DrawLocalFogObjects(const std::vector<std::unique_ptr<engine::Object3d>>& objects, DirectXCommon* dxCommon, engine::Object3d* viewActor = nullptr, bool isFirstPerson = false);

    /// <summary>水やポータルなどの特殊材質を描き、背景色コピーの有無を返す。</summary>
    /// <returns>特殊材質用の背景色をコピーして描画した場合はtrue。</returns>
    bool DrawSpecialMaterialObjects(const std::vector<std::unique_ptr<engine::Object3d>>& objects, DirectXCommon* dxCommon, BulletManager* bulletManager = nullptr, engine::Object3d* viewActor = nullptr, bool isFirstPerson = false);

    /// <summary>GPU粒子を描画し、背景色コピーが済んでいるか返す。</summary>
    /// <param name="grabAlreadyUpdated">先行する特殊材質パスで背景色をコピー済みならtrue。</param>
    /// <returns>呼び出し後に背景色コピーが済んでいればtrue。</returns>
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
