#include "CameraManager.h"
#include "BaseScene.h"
#include "DirectXCommon.h"
#include "Object3D.h"
#include "game/system/combat/BulletManager.h"
#include "Camera.h"
#include "GpuParticleManager.h"
#include "LightManager.h"
#include "PostEffect.h"
#include "SceneManager.h"
#include "ScenePreloader.h"
#include "Skybox.h"
#include "engine/graphics/3d/material/MaterialRenderPolicy.h"
#include "engine/graphics/scene/SceneRenderCommands.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

engine::Sprite* BaseScene::AddSprite(std::unique_ptr<engine::Sprite> sprite) {
    // 所有権をシーンへ移した後、編集用の非所有ポインタを返します。
    auto* sprites = GetMutableSprites();
    if (!sprites || !sprite)
        return nullptr;
    engine::Sprite* raw = sprite.get();
    sprites->push_back(std::move(sprite));
    return raw;
}

bool BaseScene::MoveSprite(engine::Sprite* sprite, int offset) {
    // 前後一つの入れ替えだけを許可し、描画順と UI 上の並びを揃えます。
    auto* sprites = GetMutableSprites();
    if (!sprites || !sprite || (offset != -1 && offset != 1))
        return false;
    auto it = std::find_if(sprites->begin(), sprites->end(), [sprite](const auto& owned) {
        return owned.get() == sprite;
    });
    if (it == sprites->end())
        return false;
    const auto index = static_cast<std::ptrdiff_t>(std::distance(sprites->begin(), it));
    const auto next = index + offset;
    if (next < 0 || next >= static_cast<std::ptrdiff_t>(sprites->size()))
        return false;
    std::swap((*sprites)[index], (*sprites)[next]);
    return true;
}

void BaseScene::AddObject(std::unique_ptr<engine::Object3D> object) {
    // オブジェクトを持たない派生シーンは、この操作を明示的に拒否します。
    (void)object;
    throw std::logic_error("This scene does not own Object3d objects");
}

engine::Object3D* BaseScene::AddObjectImmediate(std::unique_ptr<engine::Object3D> object) {
    // 即時追加も所有権を持つ派生シーンだけが実装します。
    (void)object;
    throw std::logic_error("This scene does not own Object3d objects");
}

std::unique_ptr<engine::Object3D> BaseScene::TakeObjectImmediate(engine::Object3D* object) {
    // 所有権のない基底シーンでは持ち出す対象がありません。
    (void)object;
    return nullptr;
}

SceneLoadManifest BaseScene::BuildAsyncLoadManifest() const {
    // 非同期ロードを使う派生シーンだけが必要な資源を宣言します。
    return {};
}

void BaseScene::BeginLoadingInitialize() {
    // 分割初期化を使わないシーンの既定フックです。
}

bool BaseScene::InitializeLoadingStep() {
    // 既定実装は通常の初期化を一回で終えます。
    Initialize();
    return true;
}

float BaseScene::GetLoadingInitializeProgress() const {
    // 分割ロードを実装しない場合の既定の進捗です。
    return 0.0f;
}

void BaseScene::OnActivated() {
    // 非同期準備中ではなく、現在シーンへ切り替わった時点で描画背景を確定します。
    LightManager::GetInstance()->ApplySceneClearColor();
}

void BaseScene::FixedUpdate(float fixedDeltaTime) {
    // シーンが所有する全オブジェクトを固定刻みで進めます。
    for (auto& object : GetObjects()) {
        if (!object || object->IsReplayRemoved())
            continue;
        object->FixedUpdate(fixedDeltaTime);
    }
}

bool BaseScene::TakePreparedJson(const std::string& path, json& destination) {
    // 先読み済み JSON があれば、再度ディスクを読まずに受け取ります。
    return preparedLoadData_ && preparedLoadData_->TakeJson(path, destination);
}

std::string BaseScene::ResolvePrimaryObjectLayoutPath(const std::string& defaultPath) {
    // Scene Asset の主レイアウト指定は最初の一回だけ消費します。
    if (sceneAssetObjectLayoutConsumed_ || !sceneLoadContext_.IsSceneAsset()) {
        return defaultPath;
    }

    sceneAssetObjectLayoutConsumed_ = true;
    const std::string& assetPath = sceneLoadContext_.objectLayoutPath;
    return assetPath.empty() ? defaultPath : assetPath;
}

std::string BaseScene::ResolvePrimarySpriteLayoutPath(const std::string& defaultPath) {
    // Sprite 側も Scene Asset の主レイアウトを二重に読みません。
    if (sceneAssetSpriteLayoutConsumed_ || !sceneLoadContext_.IsSceneAsset()) {
        return defaultPath;
    }

    sceneAssetSpriteLayoutConsumed_ = true;
    const std::string& assetPath = sceneLoadContext_.spriteLayoutPath;
    return assetPath.empty() ? defaultPath : assetPath;
}

bool BaseScene::Destroy(engine::Object3D* object) {
    // 3D オブジェクトの削除は生存確認と遅延破棄を行う共通経路へ渡します。
    return DestroyObject(object);
}

bool BaseScene::Destroy(engine::Sprite* sprite) {
    // Sprite の削除は Replay 保持状態に応じた共通経路へ渡します。
    return DestroySprite(sprite);
}

void BaseScene::CollectReplaySprites(std::vector<engine::Sprite*>& replaySprites) {
    // Replay に現在の Sprite を非所有参照として渡します。
    auto& sprites = GetSprites();
    replaySprites.reserve(replaySprites.size() + sprites.size());

    for (auto& sprite : sprites) {
        if (sprite) {
            replaySprites.push_back(sprite.get());
        }
    }
}

void BaseScene::CaptureReplaySceneState(json& state) const {
    // 派生シーンの追加状態を書けるよう、基底では空のオブジェクトを用意します。
    state = json::object();
}

void BaseScene::RestoreReplaySceneState(const json& state) {
    // 基底シーンには復元する固有状態がありません。
    (void)state;
}

void BaseScene::ReleaseReplaySprites() {
    // Replay が保持していた Sprite を解放し、削除保留の子参照も切ります。
    std::vector<engine::Sprite*> replaySprites;
    CollectReplaySprites(replaySprites);

    for (engine::Sprite* sprite : replaySprites) {
        if (sprite) {
            sprite->SetReplayRetained(false);
        }
    }

    auto* sprites = GetMutableSprites();
    if (!sprites)
        return;
    sprites->erase(
        std::remove_if(sprites->begin(), sprites->end(), [](const std::unique_ptr<engine::Sprite>& sprite) {
            if (!sprite || !sprite->IsReplayRemoved()) {
                return false;
            }
            sprite->SetParent(nullptr, false);
            return true;
        }),
        sprites->end());
}

void BaseScene::RefreshRenderCameraData() {
    // カメラ切り替え後にシーン内の描画物へ定数を反映します。
    auto& objects = GetObjects();

    for (auto& object : objects) {
        if (object) {
            object->RefreshRenderCameraData();
        }
    }

}

void BaseScene::DrawCameraPreview(Camera* camera, int previewBufferIndex) {
    // 指定カメラへ描く際は通常パスと特殊材質パスを同じプレビュー先へ出します。
    if (!camera) {
        return;
    }

    ID3D12Resource* pointLight = LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLight = LightManager::GetInstance()->GetSpotLightResource();
    auto& objects = GetObjects();

    if (Skybox* skybox = GetSkybox(); skybox && LightManager::GetInstance()->IsSkyboxEnabled()) {
        skybox->SetTextureHandle(LightManager::GetInstance()->GetSkyboxTextureHandle());
        skybox->Draw(camera->GetConstantBuffer());
    }

    auto drawObject = [&](engine::Object3D* object, bool transparentPass) {
        if (!object || !object->GetIsRenderVisible()) {
            return;
        }

        const int materialType = object->GetMaterialType();
        const auto materialPass = MaterialRenderPolicy::GetPass(materialType);
        const bool isTransparent = materialPass == MaterialRenderPolicy::Pass::Transparent;
        const bool isSpecialMaterial = materialPass == MaterialRenderPolicy::Pass::LocalFog ||
            materialPass == MaterialRenderPolicy::Pass::Special;

        if (isSpecialMaterial || isTransparent != transparentPass) {
            return;
        }

        object->DrawForCamera(camera, pointLight, spotLight, previewBufferIndex);
    };

    // 演出用カメラPreviewでは通常描画用のScene::Draw()を再利用せず、
    // 共有WVPを汚さない専用描画だけを通します。
    for (auto& object : objects) {
        drawObject(object.get(), false);
    }

    // 透過物は不透明物の深度が揃った後に描き、プレビューでも通常描画と順序を合わせます。
    for (auto& object : objects) {
        drawObject(object.get(), true);
    }

    // 所有する半透明演出も、プレビュー先の床・通常モデルの深度が決まってから描く。
    for (auto& object : objects) {
        if (object && object->GetIsRenderVisible()) {
            object->DrawOwnedTransparentVisualsForCamera(camera, pointLight, spotLight, previewBufferIndex);
        }
    }

    // 特殊材質がないカメラでは画面色と深度を参照する追加パスを確保しません。
    bool hasSpecialMaterial = false;

    for (const auto& object : objects) {
        if (object && object->GetIsRenderVisible() &&
            (IsSpecialMaterialType(object->GetMaterialType()) || object->HasOwnedSpecialMaterialVisuals())) {
            hasSpecialMaterial = true;
            break;
        }
    }

    if (!hasSpecialMaterial) {
        return;
    }

    // プレビュー先ごとの画面色・深度を準備し、本編のGrabTextureと混線させません。
    PostEffect* postEffect = PostEffect::GetInstance();
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    ID3D12GraphicsCommandList* commandList = dxCommon ? dxCommon->GetCommandList() : nullptr;
    const int targetTextureIndex = previewBufferIndex == 0
        ? PostEffect::kCameraPreviewTextureIndex
        : PostEffect::kCinematicCameraPreviewTextureIndex;
    if (!postEffect || !commandList ||
        !postEffect->BeginCameraPreviewSpecialPass(commandList, targetTextureIndex)) {
        return;
    }

    // 特殊材質には現在のプレビューバッファに対応するSRVだけを渡します。
    const uint32_t depthSrvHandle = postEffect->GetDepthSrvHandle(targetTextureIndex);
    const uint32_t grabSrvHandle = postEffect->GetGrabSrvHandle(targetTextureIndex);

    for (auto& object : objects) {
        if (!object || !object->GetIsRenderVisible()) {
            continue;
        }

        if (IsSpecialMaterialType(object->GetMaterialType())) {
            object->DrawSpecialMaterialForCamera(
                camera,
                depthSrvHandle,
                grabSrvHandle,
                previewBufferIndex);
        }

        if (object->HasOwnedSpecialMaterialVisuals()) {
            object->DrawOwnedSpecialMaterialVisualsForCamera(
                camera,
                depthSrvHandle,
                grabSrvHandle,
                previewBufferIndex);
        }
    }

    postEffect->EndCameraPreviewSpecialPass(commandList, targetTextureIndex);
}

bool BaseScene::DestroyObject(engine::Object3D* object) {
    // 走査中の即時破棄を避け、先に非表示にして削除を予約します。
    if (!IsAlive(object)) {
        return false;
    }

    object->SetIsVisible(false);
    RequestRemoveObject(object);
    return true;
}

bool BaseScene::DestroySprite(engine::Sprite* sprite) {
    // Replay が保持する Sprite は実破棄せず、記録上の削除として隠します。
    auto* sprites = GetMutableSprites();
    if (!sprites)
        return false;
    auto it = std::find_if(sprites->begin(), sprites->end(), [sprite](const std::unique_ptr<engine::Sprite>& candidate) {
        return candidate && candidate.get() == sprite;
    });

    if (it == sprites->end()) {
        return false;
    }

    if (sprite->IsReplayRetained()) {
        std::vector<engine::Sprite*> children = sprite->GetChildren();

        for (engine::Sprite* child : children) {
            if (child) {
                child->SetParent(nullptr);
            }
        }

        sprite->SetParent(nullptr);
        sprite->SetReplayRemoved(true);
        sprite->SetVisible(false);
        return true;
    }

    std::vector<engine::Sprite*> children = sprite->GetChildren();

    for (engine::Sprite* child : children) {
        if (child) {
            child->SetParent(nullptr);
        }
    }

    sprite->SetParent(nullptr);
    sprite->SetVisible(false);
    sprites->erase(it);
    return true;
}

bool BaseScene::IsAlive(engine::Object3D* object) {
    // 生存判定は所有配列内に同じポインタがあるかで確認します。
    if (!object) {
        return false;
    }

    auto& objects = GetObjects();
    return std::any_of(objects.begin(), objects.end(), [object](const std::unique_ptr<engine::Object3D>& candidate) {
        return candidate && candidate.get() == object;
    });
}

bool BaseScene::IsAlive(engine::Sprite* sprite) {
    // Replay 上で削除済みの Sprite は所有配列にあっても生存扱いにしません。
    if (!sprite) {
        return false;
    }

    auto& sprites = GetSprites();
    return std::any_of(sprites.begin(), sprites.end(), [sprite](const std::unique_ptr<engine::Sprite>& candidate) {
        return candidate && candidate.get() == sprite && !candidate->IsReplayRemoved();
    });
}

bool BaseScene::IsSpecialMaterialType(int materialType) const {
    // Scene Color や深度を参照する材質を専用パスへ振り分けます。
    return MaterialRenderPolicy::IsSpecial(materialType);
}

bool BaseScene::IsHiddenByFirstPerson(engine::Object3D* object, engine::Object3D* viewActor, bool isFirstPerson) const {
    // 一人称視点では視点の主体とその子オブジェクトを隠します。
    if (!isFirstPerson || !object || !viewActor) {
        return false;
    }

    engine::Object3D* current = object;

    while (current) {
        if (current == viewActor) {
            return true;
        }

        current = current->GetParent();
    }

    return false;
}

bool BaseScene::DrawLocalFogObjects(const std::vector<std::unique_ptr<engine::Object3D>>& objects, engine::Object3D* viewActor, bool isFirstPerson) {
    // 対象がないフレームは専用パスの開始と終了を省略します。
    auto* commands = GetRenderCommands();
    if (!commands) {
        return false;
    }

    if (commands->IsCameraPreviewRendering()) {
        return false;
    }

    bool hasFog = false;

    for (auto& obj : objects) {
        if (obj && obj->GetIsRenderVisible() && obj->GetMaterialType() == 7 && !IsHiddenByFirstPerson(obj.get(), viewActor, isFirstPerson)) {
            hasFog = true;
            break;
        }
    }

    if (!hasFog) {
        return false;
    }

    commands->BeginDepthRead();

    for (auto& obj : objects) {
        if (obj && obj->GetIsRenderVisible() && obj->GetMaterialType() == 7 && !IsHiddenByFirstPerson(obj.get(), viewActor, isFirstPerson)) {
            obj->DrawLocalFog(commands->GetDepthSrvHandle());
        }
    }

    commands->EndDepthRead();
    return true;
}

bool BaseScene::DrawSpecialMaterialObjects(const std::vector<std::unique_ptr<engine::Object3D>>& objects, BulletManager* bulletManager, engine::Object3D* viewActor, bool isFirstPerson) {
    // 深度・背景色を読む材質をまとめ、必要な時だけ Grab Texture を更新します。
    auto* commands = GetRenderCommands();
    if (!commands) {
        return false;
    }

    if (commands->IsCameraPreviewRendering()) {
        return false;
    }

    bool hasSpecialObjects = false;

    for (auto& obj : objects) {
        if (obj && obj->GetIsRenderVisible() &&
            (IsSpecialMaterialType(obj->GetMaterialType()) || obj->HasOwnedSpecialMaterialVisuals()) &&
            !IsHiddenByFirstPerson(obj.get(), viewActor, isFirstPerson)) {
            hasSpecialObjects = true;
            break;
        }
    }

    const bool hasSpecialBullets = bulletManager && bulletManager->HasSpecialMaterialBullets();

    auto* cameraManager = CameraManager::GetInstance();
    auto* activeCamera = cameraManager->GetActiveCamera();
    engine::Object3D* silhouetteSubject = nullptr;
    if (!isFirstPerson && cameraManager->GetVisibilityFrame().enabled &&
        activeCamera && activeCamera == cameraManager->GetMainCamera() &&
        activeCamera->GetVisibilitySettings().silhouetteColor[3] > 0.0f) {
        // シーンにまだ所属している実体だけ使う。装飾を何重にも重ねず本体を一度描く。
        for (const auto& object : objects) {
            if (object && object.get() == activeCamera->GetFollowTarget() && object->GetIsRenderVisible()) {
                silhouetteSubject = object.get(); break;
            }
        }
    }
    if (!hasSpecialObjects && !hasSpecialBullets && !silhouetteSubject) {
        return false;
    }

    const bool grabUpdated = hasSpecialObjects || hasSpecialBullets;
    if (grabUpdated) commands->CopySceneColor();

    // 特殊マテリアルはScene深度をSRVとして読むため、描画中だけDSVを読み取り状態へ切り替えます。
    commands->BeginDepthRead();

    if (hasSpecialObjects) {
        const uint32_t depthSrvHandle = commands->GetDepthSrvHandle();
        const uint32_t grabSrvHandle = commands->GetSceneColorSrvHandle();

        for (auto& obj : objects) {
            if (!obj || !obj->GetIsRenderVisible() || IsHiddenByFirstPerson(obj.get(), viewActor, isFirstPerson)) {
                continue;
            }

            obj->DrawSpecialMaterial(depthSrvHandle, grabSrvHandle);
            obj->DrawOwnedSpecialMaterialVisuals(depthSrvHandle, grabSrvHandle);
        }
    }

    if (hasSpecialBullets) {
        bulletManager->DrawSpecial(commands->GetDepthSrvHandle(), commands->GetSceneColorSrvHandle());
    }

    if (silhouetteSubject && silhouetteSubject->GetMeshRenderer()) {
        silhouetteSubject->GetMeshRenderer()->DrawVisibilitySilhouette(
            commands->GetDepthSrvHandle(), commands->GetSceneColorSrvHandle());
    }
    commands->EndDepthRead();
    return grabUpdated;
}

bool BaseScene::DrawGpuParticles(Camera* camera, uint32_t textureHandle, bool grabAlreadyUpdated) {
    // 背景色が必要な粒子だけコピーを追加し、既済みなら再利用します。
    GpuParticleManager* particleManager = GpuParticleManager::GetInstance();
    auto* commands = GetRenderCommands();

    if (!commands || !camera || particleManager->IsEmpty()) {
        return grabAlreadyUpdated;
    }

    if (commands->IsCameraPreviewRendering()) {
        return grabAlreadyUpdated;
    }

    // 加算・半透明パーティクルは背景色を参照しないため、全画面コピーを省略します。
    if (!grabAlreadyUpdated && particleManager->RequiresSceneColorCopy()) {
        commands->CopySceneColor();
        grabAlreadyUpdated = true;
    }

    commands->BeginDepthRead();
    commands->RecordGpuParticles(*particleManager, *camera, textureHandle);
    commands->EndDepthRead();
    return grabAlreadyUpdated;
}

void BaseScene::TriggerEvent(int targetId) {
    // IDが -1 (設定なし) なら何もしない
    if (targetId == -1)
        return;

    // シーン内の全オブジェクトを取得 
    auto& objects = GetObjects();

    for (auto& obj : objects) {
        // 「俺の受信ID、呼ばれた番号と同じだ！」
        if (obj->GetEventId() == targetId) {
            // アクションを実行！
            obj->OnTrigger();
        }
    }
}

void BaseScene::SetEventActive(int targetId, bool active) {
    // 同じ受信 ID の全オブジェクトへオン・オフ状態を伝えます。
    if (targetId == -1)
        return;

    auto& objects = GetObjects();

    for (auto& obj : objects) {
        if (obj->GetEventId() == targetId) {
            obj->OnSwitchEvent(active);
        }
    }
}

engine::Object3D* BaseScene::FindObjectByEventId(int eventId) {
    // IDなしなら無視
    if (eventId == -1)
        return nullptr;

    auto& objects = GetObjects();

    for (auto& obj : objects) {
        if (obj->GetEventId() == eventId) {
            return obj.get(); // 見つけたオブジェクトそのものを返す！
        }
    }

    return nullptr;
}

engine::Object3D* BaseScene::FindObjectByPersistentGuid(const std::string& guid) {
    // 保存用 GUID の形式を確かめてからシーン内を探索します。
    if (!engine::Object3D::IsPersistentGuidValid(guid)) {
        return nullptr;
    }

    for (const auto& object : GetObjects()) {
        if (object && object->GetPersistentGuid() == guid) {
            return object.get();
        }
    }

    return nullptr;
}

const engine::Object3D* BaseScene::FindObjectByPersistentGuid(const std::string& guid) const {
    // 読み取り側も同じ GUID 探索規則を使います。
    return const_cast<BaseScene*>(this)->FindObjectByPersistentGuid(guid);
}

std::size_t BaseScene::EnsureUniquePersistentObjectGuids() {
    // 重複 GUID だけ再発行し、Scene Asset の安定した参照を保ちます。
    std::unordered_set<std::string> usedGuids;
    usedGuids.reserve(GetObjects().size());
    std::size_t regeneratedCount = 0;

    for (const auto& object : GetObjects()) {
        if (!object)
            continue;

        const std::string currentGuid = object->EnsurePersistentGuid();

        if (usedGuids.insert(currentGuid).second) {
            continue;
        }

        do {
            object->RegeneratePersistentGuid();
        } while (!usedGuids.insert(object->GetPersistentGuid()).second);
        ++regeneratedCount;
    }

    return regeneratedCount;
}

// 名前からスプライトを取得する
engine::Sprite* BaseScene::GetSpriteByName(const std::string& name) {
    auto& sprites = GetSprites();

    for (auto& sprite : sprites) {
        if (sprite && sprite->GetName() == name) {
            return sprite.get();
        }
    }

    return nullptr; // 見つからなかった場合
}

// CommonはFramework所有。SceneはManager経由で借りるだけで、解放操作を持たない。
engine::Object3DCommon* BaseScene::GetObject3DCommon() {
    if (GetRenderPreparation() == engine::RenderPreparation::SpritesOnly || !sceneManager_) return nullptr;
    auto* resources = sceneManager_->GetRenderCommonResources();
    return resources ? resources->GetObject3DCommon() : nullptr;
}

engine::SpriteCommon* BaseScene::GetSpriteCommon() {
    auto* resources = sceneManager_ ? sceneManager_->GetRenderCommonResources() : nullptr;
    return resources ? resources->GetSpriteCommon() : nullptr;
}

engine::ParticleCommon* BaseScene::GetParticleCommon() const {
    if (GetRenderPreparation() == engine::RenderPreparation::SpritesOnly || !sceneManager_) return nullptr;
    auto* resources = sceneManager_->GetRenderCommonResources();
    return resources ? resources->GetParticleCommon() : nullptr;
}

engine::SceneRenderCommands* BaseScene::GetRenderCommands() const {
    auto* resources = sceneManager_ ? sceneManager_->GetRenderCommonResources() : nullptr;
    return resources ? resources->GetSceneRenderCommands() : nullptr;
}

void BaseScene::BeginSpriteDrawing() {
    auto* commands = GetRenderCommands();
    auto* sprites = GetSpriteCommon();
    if (!commands || !sprites) {
        throw std::logic_error("Scene sprite drawing requires prepared render resources");
    }
    commands->BindSprites(*sprites);
}

void BaseScene::PrepareGpuParticles(GpuParticlePresetScope scope) {
    // GPU基盤の取得・受け渡しは共通層に置き、シーンは準備範囲だけを選ぶ。
    auto* particles = GpuParticleManager::GetInstance();
    auto* dxCommon = DirectXCommon::GetInstance();
#ifndef USE_IMGUI
    // 配布版の軽量起動では、先読み結果が空でも「指定対象なし」として維持する。
    // 先読み自体が無い場合だけ従来のInitializeへ戻す。
    if (scope == GpuParticlePresetScope::PreparedScene) {
        if (GetPreparedLoadData()) {
            particles->InitializeWithPresets(dxCommon, GetPreparedLoadData()->gpuParticlePresetNames);
        } else {
            particles->Initialize(dxCommon);
        }
        return;
    }
#else
    // Editorで他のプリセットを選べるよう、軽量範囲の指定でも全件を読み込む。
    (void)scope;
#endif
    particles->Initialize(dxCommon);
    particles->LoadAllPresets("Resources/json/gpu_particles/");
}
