#include "MeshEffectManager.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "IconsFontAwesome5.h"
#include "json.hpp"
#include <fstream>
#include <DebugConsole.h>
#include "SceneManager.h"
#include "BaseScene.h"
#include <exception>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <CollisionManager.h>
using json = nlohmann::json;

namespace {
struct EffectJsonCacheEntry {
    json data;
    std::filesystem::file_time_type writeTime{};
    bool hasWriteTime = false;
};

bool LoadEffectJsonCached(const std::string& jsonFilePath, json& outJson) {
    static std::unordered_map<std::string, EffectJsonCacheEntry> cache;

    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(jsonFilePath, ec);
    const bool hasWriteTime = !ec;

    auto it = cache.find(jsonFilePath);
    if (it != cache.end() && hasWriteTime && it->second.hasWriteTime && it->second.writeTime == writeTime) {
        outJson = it->second.data;
        return true;
    }

    std::ifstream file(jsonFilePath);
    if (!file.is_open()) {
        return false;
    }

    json loaded;
    file >> loaded;

    EffectJsonCacheEntry entry;
    entry.data = loaded;
    entry.writeTime = writeTime;
    entry.hasWriteTime = hasWriteTime;
    cache[jsonFilePath] = entry;

    outJson = std::move(loaded);
    return true;
}
}

MeshEffectManager* MeshEffectManager::GetInstance() {
    static MeshEffectManager instance;
    return &instance;
}

void MeshEffectManager::Initialize(Object3dCommon* common) {
    common_ = common;
    timeScale_ = 1.0f;
}

void MeshEffectManager::BeginFrame() {
    updatedThisFrame_ = false;
}

void MeshEffectManager::PreloadEffect(const std::string& jsonFilePath) {
    if (preloadedEffects_.find(jsonFilePath) != preloadedEffects_.end()) {
        return;
    }

    if (!common_) {
        SceneManager* sm = SceneManager::GetInstance();
        if (sm && sm->GetCurrentScene()) {
            common_ = sm->GetCurrentScene()->GetObject3dCommon();
        }
    }
    if (!common_) {
        return;
    }

    json j;
    try {
        if (!LoadEffectJsonCached(jsonFilePath, j)) {
            DebugConsole::GetInstance()->AddLog(LogLevel::Warning, "[MeshEffectManager] PreloadEffect: failed to open " + jsonFilePath);
            return;
        }
    } catch (const std::exception& e) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[MeshEffectManager] PreloadEffect JSON error: " + jsonFilePath + " / " + e.what());
        return;
    }

    if (j.empty()) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Warning, "[MeshEffectManager] PreloadEffect: failed to open " + jsonFilePath);
        return;
    }

    auto effect = std::make_unique<EffectObject3d>();
    effect->Initialize(common_);
    effect->SetProceduralType(0);
    effect->SetEnableNoiseTexture(false);
    effect->SetEnableColorRamp(false);
    effect->SetEnableDistortion(false);
    effect->SetEnableReveal(true);
    effect->SetDistortionStrength(0.0f);
    effect->SetEdgeFadeStrength(1.0f);

    if (j.contains("ModelName")) {
        const std::string modelName = j["ModelName"].get<std::string>();
        if (!modelName.empty()) {
            effect->SetModel(modelName);
        }
    }
    if (j.contains("TexturePath")) {
        const std::string texturePath = j["TexturePath"].get<std::string>();
        if (!texturePath.empty() && effect->GetMeshRenderer()) {
            effect->GetMeshRenderer()->SetTexture(texturePath);
        }
    }
    if (j.contains("NoiseTexturePath")) {
        const std::string texturePath = j["NoiseTexturePath"].get<std::string>();
        if (!texturePath.empty()) {
            effect->SetNoiseTexture(TextureManager::GetInstance()->Load(
                texturePath, TextureManager::TextureColorSpace::Linear));
            effect->SetEnableNoiseTexture(true);
        }
    }
    if (j.contains("RampTexturePath")) {
        const std::string texturePath = j["RampTexturePath"].get<std::string>();
        if (!texturePath.empty()) {
            effect->SetRampTexture(TextureManager::GetInstance()->Load(
                texturePath, TextureManager::TextureColorSpace::SRGB));
            effect->SetEnableColorRamp(true);
        }
    }

    if (j.contains("ProceduralType")) {
        int procType = j["ProceduralType"];
        effect->SetProceduralType(procType);
        if (procType >= 1) {
            if (j.contains("SlashAngle")) effect->editSlashAngle_ = j["SlashAngle"];
            if (j.contains("InnerRadius")) effect->editInnerRadius_ = j["InnerRadius"];
            if (j.contains("OuterRadius")) effect->editOuterRadius_ = j["OuterRadius"];
            if (j.contains("Thickness")) effect->editThickness_ = j["Thickness"];
            if (j.contains("SpiralPitch")) effect->editSpiralPitch_ = j["SpiralPitch"];
            if (j.contains("ThrustLength")) effect->editThrustLength_ = j["ThrustLength"];
            if (j.contains("ThrustRadius")) effect->editThrustRadius_ = j["ThrustRadius"];
            if (j.contains("SphereRadius")) effect->editSphereRadius_ = j["SphereRadius"];
            if (j.contains("SphereRings")) effect->editSphereRings_ = j["SphereRings"];
            if (j.contains("CylinderRadius")) effect->editCylinderRadius_ = j["CylinderRadius"];
            if (j.contains("CylinderHeight")) effect->editCylinderHeight_ = j["CylinderHeight"];
            if (j.contains("BoxSize")) { effect->editBoxSize_.x = j["BoxSize"][0]; effect->editBoxSize_.y = j["BoxSize"][1]; effect->editBoxSize_.z = j["BoxSize"][2]; }
            if (j.contains("PlaneSize")) { effect->editPlaneSize_.x = j["PlaneSize"][0]; effect->editPlaneSize_.y = j["PlaneSize"][1]; }
            if (j.contains("TorusMajorRadius")) effect->editTorusMajorRadius_ = j["TorusMajorRadius"];
            if (j.contains("TorusMinorRadius")) effect->editTorusMinorRadius_ = j["TorusMinorRadius"];
            if (j.contains("ConeRadius")) effect->editConeRadius_ = j["ConeRadius"];
            if (j.contains("ConeHeight")) effect->editConeHeight_ = j["ConeHeight"];
            if (j.contains("RingOuterRadius")) effect->editRingOuterRadius_ = j["RingOuterRadius"];
            if (j.contains("RingInnerRadius")) effect->editRingInnerRadius_ = j["RingInnerRadius"];
            if (j.contains("TriangleSize")) effect->editTriangleSize_ = j["TriangleSize"];
            if (j.contains("MeshSegments")) effect->editMeshSegments_ = j["MeshSegments"];
            if (j.contains("UvTiling")) { effect->editUvTiling_.x = j["UvTiling"][0]; effect->editUvTiling_.y = j["UvTiling"][1]; }
            effect->UpdateProceduralMesh();
        }
    }

    preloadedEffects_.insert(jsonFilePath);
}

void MeshEffectManager::Update(float deltaTime) {
    if (updatedThisFrame_) {
        return;
    }
    updatedThisFrame_ = true;
    deltaTime *= timeScale_;

    // リストの中を回して、寿命が切れたエフェクトを削除する
    for (auto it = activeEffects_.begin(); it != activeEffects_.end();) {
        if (!(*it)->IsPlaying()) {
            EffectObject3d* finishedEffect = it->get();
            if ((*it)->editHasCollision_) {
                CollisionManager::GetInstance()->RemoveObject(it->get());
            }

            // 再生が終了したエフェクトは、所有スコープの追跡情報も同時に破棄します。
            effectScopes_.erase(finishedEffect);
            if (previewEffectForDebug_ == finishedEffect) {
                previewEffectForDebug_ = nullptr;
            }
            it = activeEffects_.erase(it);
        }
        else {
            // 再生中なら更新
            (*it)->Update(deltaTime);
            (*it)->UpdateLocalMatrix();
            (*it)->UpdateWorldMatrix();
            ++it;
        }
    }
}

void MeshEffectManager::UpdateEditorPreviewStep(float deltaTime) {
    updatedThisFrame_ = false;
    Update(deltaTime);
}

void MeshEffectManager::Draw(ID3D12Resource* pLight, ID3D12Resource* sLight) {
    for (auto& effect : activeEffects_) {
        effect->Draw(pLight, sLight);
    }
}

bool MeshEffectManager::RequiresSceneColorCopy() const {
    return std::any_of(
        activeEffects_.begin(),
        activeEffects_.end(),
        [](const std::unique_ptr<EffectObject3d>& effect) {
            return effect && effect->IsPlaying() && effect->IsDistortionEnabled();
        });
}

MeshEffectManager::EffectScopeId MeshEffectManager::CreateEffectScope() {
    EffectScopeId scopeId = nextEffectScopeId_++;
    if (scopeId == kInvalidEffectScope) {
        scopeId = nextEffectScopeId_++;
    }
    return scopeId;
}

void MeshEffectManager::StopEffectScope(EffectScopeId scopeId) {
    if (scopeId == kInvalidEffectScope) {
        return;
    }

    for (auto it = activeEffects_.begin(); it != activeEffects_.end();) {
        EffectObject3d* effect = it->get();
        const auto scopeIt = effectScopes_.find(effect);
        if (scopeIt == effectScopes_.end() || scopeIt->second != scopeId) {
            ++it;
            continue;
        }

        if (effect && effect->editHasCollision_) {
            CollisionManager::GetInstance()->RemoveObject(effect);
        }
        if (previewEffectForDebug_ == effect) {
            previewEffectForDebug_ = nullptr;
        }
        effectScopes_.erase(scopeIt);
        it = activeEffects_.erase(it);
    }
}
void MeshEffectManager::SpawnEffect(const std::string& jsonFilePath, Object3d* baseObject, const Vector3& extOffset, const Vector3& extRot, const Vector3& extScale) {
    // common_ が null のとき、現在シーンから自動取得を試みる（Initialize呼び忘れ対策）
    if (!common_) {
        SceneManager* sm = SceneManager::GetInstance();
        if (sm && sm->GetCurrentScene()) {
            common_ = sm->GetCurrentScene()->GetObject3dCommon();
        }
    }
    if (!common_) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[MeshEffectManager] SpawnEffect ABORT: common_ is null!");
        return;
    }

    json j;
    try {
        if (!LoadEffectJsonCached(jsonFilePath, j)) {
            DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[MeshEffectManager] Failed to open effect json: " + jsonFilePath);
            return;
        }
    } catch (const std::exception& e) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[MeshEffectManager] SpawnEffect JSON error: " + jsonFilePath + " / " + e.what());
        return;
    }

    // ==========================================
    // 追従先から位置と向きを分けて取得し、Bone回転による不要な捻れを避けます。
    // ==========================================
    Vector3 basePos = { 0, 0, 0 };
    float targetWorldY = 0.0f; // ★プレイヤーの「向き」

    Object3d* posTarget = baseObject; // 位置の基準
    Object3d* rotTarget = baseObject; // 向きの基準

    // JSONでTargetNameが指定されている場合、特定のボーンなどを探す
    if (j.contains("TargetName")) {
        std::string targetName = j["TargetName"];
        if (!targetName.empty()) {
            SceneManager* sm = SceneManager::GetInstance();
            if (sm && sm->GetCurrentScene()) {
                auto& objects = sm->GetCurrentScene()->GetObjects();
                std::function<Object3d* (Object3d*, const std::string&)> findObj = [&](Object3d* obj, const std::string& name) -> Object3d* {
                    if (!obj) return nullptr;
                    if (obj->GetName() == name) return obj;
                    for (auto* child : obj->GetChildren()) {
                        Object3d* found = findObj(child, name);
                        if (found) return found;
                    }
                    return nullptr;
                    };
                for (auto& obj : objects) {
                    Object3d* found = findObj(obj.get(), targetName);
                    if (found) {
                        posTarget = found; // 見つかったボーンを位置の基準にする
                        // rotTarget が null の場合のみ、見つけたオブジェクトをセットする
                        if (!rotTarget) rotTarget = found;
                        break;
                    }
                }
            }
        }
    }

    // 位置は指定Boneへ追従させます。
    if (posTarget) {
        basePos = posTarget->GetWorldPosition();
    }

    // YawはBone回転を使わず、Root Objectの向きだけを基準にします。
    if (rotTarget) {
        Object3d* rootObj = rotTarget;
        // 親を辿って一番上のノード（プレイヤー本体など）を見つける
        while (rootObj && rootObj->GetParent()) {
            rootObj = rootObj->GetParent();
        }
        if (rootObj) {
            targetWorldY = rootObj->GetRotation().y;
        }
    }

    // ==========================================
    // Local Offsetを読み込み、Root ObjectのYawで回転します。
    // ==========================================
    Vector3 offsetPos = { 0, 0, 0 };
    Vector3 offsetRot = { 0, 0, 0 };
    if (j.contains("Position")) offsetPos = { j["Position"][0], j["Position"][1], j["Position"][2] };
    if (j.contains("Rotation")) offsetRot = { j["Rotation"][0], j["Rotation"][1], j["Rotation"][2] };

    // シーケンサーからの追加オフセットを合成
    offsetPos.x += extOffset.x;
    offsetPos.y += extOffset.y;
    offsetPos.z += extOffset.z;

    offsetRot.x += extRot.x;
    offsetRot.y += extRot.y;
    offsetRot.z += extRot.z;

    // Editorで設定したLocal OffsetをRoot ObjectのYawへ合わせて回転します。
    float s = sinf(targetWorldY);
    float c = cosf(targetWorldY);
    Vector3 rotatedOffset;
    rotatedOffset.x = offsetPos.x * c + offsetPos.z * s;
    rotatedOffset.y = offsetPos.y;
    rotatedOffset.z = -offsetPos.x * s + offsetPos.z * c;

    int volumeMode = j.contains("VolumeMode") ? (int)j["VolumeMode"] : 0;
    int numSpawns = (volumeMode == 2) ? 3 : (volumeMode == 1 ? 2 : 1);
    float lifetime = j.contains("Lifetime") ? (float)j["Lifetime"] : 1.0f;

    // ==========================================
    // 設定されたLoop数だけEffect Objectを生成します。
    // ==========================================
    for (int i = 0; i < numSpawns; ++i) {
        auto effect = std::make_unique<EffectObject3d>();
        effect->Initialize(common_);

        // 初期化リセット
        effect->SetProceduralType(0); effect->SetEnableNoiseTexture(false); effect->SetEnableColorRamp(false);
        effect->SetEnableDistortion(false); effect->SetEnableReveal(true); effect->SetDistortionStrength(0.0f); effect->SetEdgeFadeStrength(1.0f);

        // --- パラメータ復元 ---
        if (j.contains("ModelName")) effect->SetModel(j["ModelName"].get<std::string>());
        if (j.contains("TexturePath")) { std::string tp = j["TexturePath"]; if (!tp.empty() && effect->GetMeshRenderer()) effect->GetMeshRenderer()->SetTexture(tp); }
        if (j.contains("NoiseTexturePath")) { std::string np = j["NoiseTexturePath"]; if (!np.empty()) { effect->SetNoiseTexture(TextureManager::GetInstance()->Load(np, TextureManager::TextureColorSpace::Linear)); effect->SetEnableNoiseTexture(true); } }
        if (j.contains("RampTexturePath")) { std::string rp = j["RampTexturePath"]; if (!rp.empty()) { effect->SetRampTexture(TextureManager::GetInstance()->Load(rp, TextureManager::TextureColorSpace::SRGB)); effect->SetEnableColorRamp(true); } }
        if (j.contains("StartScale")) effect->SetStartScale({ j["StartScale"][0] * extScale.x, j["StartScale"][1] * extScale.y, j["StartScale"][2] * extScale.z });
        if (j.contains("EndScale")) effect->SetEndScale({ j["EndScale"][0] * extScale.x, j["EndScale"][1] * extScale.y, j["EndScale"][2] * extScale.z });
        if (j.contains("StartColor")) effect->SetStartColor({ j["StartColor"][0], j["StartColor"][1], j["StartColor"][2], j["StartColor"][3] });
        if (j.contains("EndColor")) effect->SetEndColor({ j["EndColor"][0], j["EndColor"][1], j["EndColor"][2], j["EndColor"][3] });
        if (j.contains("ScrollSpeed")) effect->SetScrollSpeed({ j["ScrollSpeed"][0], j["ScrollSpeed"][1] });
        if (j.contains("Intensity")) effect->SetIntensity(j["Intensity"]);
        if (j.contains("DistortionStrength")) effect->SetDistortionStrength(j["DistortionStrength"]);
        if (j.contains("DistortionSpeed")) effect->SetDistortionSpeed(j["DistortionSpeed"]);
        if (j.contains("EdgeFadeStrength")) effect->SetEdgeFadeStrength(j["EdgeFadeStrength"]);
        if (j.contains("EnableDistortion")) effect->SetEnableDistortion(j["EnableDistortion"]);
        if (j.contains("BlendMode")) effect->SetBlendMode(static_cast<BlendMode>(j["BlendMode"].get<int>()));
        if (j.contains("EnableReveal")) effect->SetEnableReveal(j["EnableReveal"]);
        if (j.contains("EasingType")) effect->SetEasingType(j["EasingType"]);
        if (j.contains("AlphaReference")) effect->SetAlphaReference(j["AlphaReference"]);

        // =========================================================
        // プロシージャルパラメータの完全復元と構築
        // =========================================================
        if (j.contains("ProceduralType")) {
            int procType = j["ProceduralType"];
            effect->SetProceduralType(procType);

            if (procType >= 1) { // プロシージャルを使用する場合
                if (j.contains("SlashAngle")) effect->editSlashAngle_ = j["SlashAngle"];
                if (j.contains("InnerRadius")) effect->editInnerRadius_ = j["InnerRadius"];
                if (j.contains("OuterRadius")) effect->editOuterRadius_ = j["OuterRadius"];
                if (j.contains("Thickness")) effect->editThickness_ = j["Thickness"];
                if (j.contains("SpiralPitch")) effect->editSpiralPitch_ = j["SpiralPitch"];
                if (j.contains("ThrustLength")) effect->editThrustLength_ = j["ThrustLength"];
                if (j.contains("ThrustRadius")) effect->editThrustRadius_ = j["ThrustRadius"];
                if (j.contains("SphereRadius")) effect->editSphereRadius_ = j["SphereRadius"];
                if (j.contains("SphereRings")) effect->editSphereRings_ = j["SphereRings"];
                if (j.contains("CylinderRadius")) effect->editCylinderRadius_ = j["CylinderRadius"];
                if (j.contains("CylinderHeight")) effect->editCylinderHeight_ = j["CylinderHeight"];
                if (j.contains("BoxSize")) { effect->editBoxSize_.x = j["BoxSize"][0]; effect->editBoxSize_.y = j["BoxSize"][1]; effect->editBoxSize_.z = j["BoxSize"][2]; }
                if (j.contains("PlaneSize")) { effect->editPlaneSize_.x = j["PlaneSize"][0]; effect->editPlaneSize_.y = j["PlaneSize"][1]; }
                if (j.contains("TorusMajorRadius")) effect->editTorusMajorRadius_ = j["TorusMajorRadius"];
                if (j.contains("TorusMinorRadius")) effect->editTorusMinorRadius_ = j["TorusMinorRadius"];
                if (j.contains("ConeRadius")) effect->editConeRadius_ = j["ConeRadius"];
                if (j.contains("ConeHeight")) effect->editConeHeight_ = j["ConeHeight"];
                if (j.contains("RingOuterRadius")) effect->editRingOuterRadius_ = j["RingOuterRadius"];
                if (j.contains("RingInnerRadius")) effect->editRingInnerRadius_ = j["RingInnerRadius"];
                if (j.contains("TriangleSize")) effect->editTriangleSize_ = j["TriangleSize"];
                if (j.contains("MeshSegments")) effect->editMeshSegments_ = j["MeshSegments"];
                if (j.contains("UvTiling")) { effect->editUvTiling_.x = j["UvTiling"][0]; effect->editUvTiling_.y = j["UvTiling"][1]; }

                effect->UpdateProceduralMesh();
            }
        }

        // =========================================================
        // Collider設定を復元し、有効な場合だけCollision Managerへ登録します。
        // =========================================================
        if (j.contains("Collision")) {
            effect->editHasCollision_ = j["Collision"]["HasCollision"];

            if (effect->editHasCollision_) {
                effect->editCollisionShape_ = j["Collision"]["Shape"];
                effect->editCollisionSize_ = { j["Collision"]["Size"][0], j["Collision"]["Size"][1], j["Collision"]["Size"][2] };
                effect->editCollisionOffset_ = { j["Collision"]["Offset"][0], j["Collision"]["Offset"][1], j["Collision"]["Offset"][2] };

                ColliderType cType = ColliderType::kNone;
                if (effect->editCollisionShape_ == 0) cType = ColliderType::kSphere;
                else if (effect->editCollisionShape_ == 1) cType = ColliderType::kAABB;
                else if (effect->editCollisionShape_ == 2) cType = ColliderType::kOBB;

                // =======================================================
                // 既存設定を取得し、対象Slotだけを更新して他項目を維持します。
                // =======================================================
                Object3d::ColliderConfig cConfig = effect->GetColliderConfig();
                cConfig.type = cType;
                cConfig.size = effect->editCollisionSize_;
                cConfig.center = effect->editCollisionOffset_;
                effect->SetColliderConfig(cConfig);

                // --- 属性とマスクの設定 ---
                effect->SetCollisionAttribute(kPlayerAttack); // 例: kPlayerAttack 相当
                effect->SetCollisionMask(kEnemy);      // 例: kEnemy 相当

                CollisionManager::GetInstance()->AddObject(effect.get());
            }
        }

        // 厚みを持たせるための位置と回転を適用します。
        Vector3 finalPos = { basePos.x + rotatedOffset.x, basePos.y + rotatedOffset.y, basePos.z + rotatedOffset.z };

        // 追従回転はYawだけを加算し、Bone姿勢との二重適用を避けます。
        Vector3 finalRot = { offsetRot.x, offsetRot.y + targetWorldY, offsetRot.z };

        Vector3 localZ;
        localZ.x = sinf(finalRot.y) * cosf(finalRot.x);
        localZ.y = -sinf(finalRot.x);
        localZ.z = cosf(finalRot.y) * cosf(finalRot.x);

        if (volumeMode == 1 && i == 1) {
            finalRot.x += 1.570796f; // 90度
        }
        else if (volumeMode == 2) {
            float gap = 0.02f;
            if (i == 1) { finalPos.x += localZ.x * gap; finalPos.y += localZ.y * gap; finalPos.z += localZ.z * gap; }
            if (i == 2) { finalPos.x -= localZ.x * gap; finalPos.y -= localZ.y * gap; finalPos.z -= localZ.z * gap; }
        }

        if (posTarget) {
            effect->SetTargetObject(posTarget);
            effect->SetOffsets(offsetPos, offsetRot);
        }
        effect->SetTranslate(finalPos);
        effect->SetRotation(finalRot);

        // --- 5. 再生開始 ---
        effect->Play(lifetime);
        effect->Update(0.0f);
        effect->UpdateLocalMatrix();
        effect->UpdateWorldMatrix();

        activeEffects_.push_back(std::move(effect));
    }
}

// ==========================================================
//  TrailEmitter 専用: ワールド座標を直接指定してSpawn
//  JSONの Position / Rotation フィールドを無視し、
//  worldPos / worldRot をそのまま最終座標として使う
// ==========================================================
void MeshEffectManager::SpawnEffectAt(const std::string& jsonFilePath, const Vector3& worldPos, const Vector3& worldRot, const Vector3& scale) {
    // common_ が null なら現在シーンから自己修復
    if (!common_) {
        SceneManager* sm = SceneManager::GetInstance();
        if (sm && sm->GetCurrentScene()) {
            common_ = sm->GetCurrentScene()->GetObject3dCommon();
        }
    }
    if (!common_) return;

    json j;
    try {
        if (!LoadEffectJsonCached(jsonFilePath, j)) {
            DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[MeshEffectManager] SpawnEffectAt: failed to open " + jsonFilePath);
            return;
        }
    } catch (const std::exception& e) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[MeshEffectManager] SpawnEffectAt JSON error: " + jsonFilePath + " / " + e.what());
        return;
    }

    int volumeMode = j.contains("VolumeMode") ? (int)j["VolumeMode"] : 0;
    int numSpawns  = (volumeMode == 2) ? 3 : (volumeMode == 1 ? 2 : 1);
    float lifetime = j.contains("Lifetime") ? (float)j["Lifetime"] : 1.0f;

    // localZ ベクトル (volumeMode 2 の立体化オフセット計算用)
    Vector3 localZ;
    localZ.x = sinf(worldRot.y) * cosf(worldRot.x);
    localZ.y = -sinf(worldRot.x);
    localZ.z = cosf(worldRot.y) * cosf(worldRot.x);

    for (int i = 0; i < numSpawns; ++i) {
        auto effect = std::make_unique<EffectObject3d>();
        effect->Initialize(common_);

        // 初期化リセット
        effect->SetProceduralType(0); effect->SetEnableNoiseTexture(false); effect->SetEnableColorRamp(false);
        effect->SetEnableDistortion(false); effect->SetEnableReveal(true); effect->SetDistortionStrength(0.0f); effect->SetEdgeFadeStrength(1.0f);

        // --- パラメータ復元 (SpawnEffect と同じ) ---
        if (j.contains("ModelName")) effect->SetModel(j["ModelName"].get<std::string>());
        if (j.contains("TexturePath")) { std::string tp = j["TexturePath"]; if (!tp.empty() && effect->GetMeshRenderer()) effect->GetMeshRenderer()->SetTexture(tp); }
        if (j.contains("NoiseTexturePath")) { std::string np = j["NoiseTexturePath"]; if (!np.empty()) { effect->SetNoiseTexture(TextureManager::GetInstance()->Load(np, TextureManager::TextureColorSpace::Linear)); effect->SetEnableNoiseTexture(true); } }
        if (j.contains("RampTexturePath")) { std::string rp = j["RampTexturePath"]; if (!rp.empty()) { effect->SetRampTexture(TextureManager::GetInstance()->Load(rp, TextureManager::TextureColorSpace::SRGB)); effect->SetEnableColorRamp(true); } }
        if (j.contains("StartScale")) effect->SetStartScale({ j["StartScale"][0] * scale.x, j["StartScale"][1] * scale.y, j["StartScale"][2] * scale.z });
        if (j.contains("EndScale"))   effect->SetEndScale(  { j["EndScale"][0]   * scale.x, j["EndScale"][1]   * scale.y, j["EndScale"][2]   * scale.z });
        if (j.contains("StartColor")) effect->SetStartColor({ j["StartColor"][0], j["StartColor"][1], j["StartColor"][2], j["StartColor"][3] });
        if (j.contains("EndColor"))   effect->SetEndColor(  { j["EndColor"][0],   j["EndColor"][1],   j["EndColor"][2],   j["EndColor"][3]   });
        if (j.contains("ScrollSpeed"))       effect->SetScrollSpeed({ j["ScrollSpeed"][0], j["ScrollSpeed"][1] });
        if (j.contains("Intensity"))         effect->SetIntensity(j["Intensity"]);
        if (j.contains("DistortionStrength"))effect->SetDistortionStrength(j["DistortionStrength"]);
        if (j.contains("DistortionSpeed"))   effect->SetDistortionSpeed(j["DistortionSpeed"]);
        if (j.contains("EdgeFadeStrength"))  effect->SetEdgeFadeStrength(j["EdgeFadeStrength"]);
        if (j.contains("EnableDistortion"))  effect->SetEnableDistortion(j["EnableDistortion"]);
        if (j.contains("BlendMode"))         effect->SetBlendMode(static_cast<BlendMode>(j["BlendMode"].get<int>()));
        if (j.contains("EnableReveal"))      effect->SetEnableReveal(j["EnableReveal"]);
        if (j.contains("EasingType"))        effect->SetEasingType(j["EasingType"]);
        if (j.contains("AlphaReference"))    effect->SetAlphaReference(j["AlphaReference"]);

        // プロシージャルメッシュ
        if (j.contains("ProceduralType")) {
            int procType = j["ProceduralType"];
            effect->SetProceduralType(procType);
            if (procType >= 1) {
                if (j.contains("SlashAngle"))      effect->editSlashAngle_      = j["SlashAngle"];
                if (j.contains("InnerRadius"))     effect->editInnerRadius_     = j["InnerRadius"];
                if (j.contains("OuterRadius"))     effect->editOuterRadius_     = j["OuterRadius"];
                if (j.contains("Thickness"))       effect->editThickness_       = j["Thickness"];
                if (j.contains("SpiralPitch"))     effect->editSpiralPitch_     = j["SpiralPitch"];
                if (j.contains("ThrustLength"))    effect->editThrustLength_    = j["ThrustLength"];
                if (j.contains("ThrustRadius"))    effect->editThrustRadius_    = j["ThrustRadius"];
                if (j.contains("SphereRadius"))    effect->editSphereRadius_    = j["SphereRadius"];
                if (j.contains("SphereRings"))     effect->editSphereRings_     = j["SphereRings"];
                if (j.contains("CylinderRadius"))  effect->editCylinderRadius_  = j["CylinderRadius"];
                if (j.contains("CylinderHeight"))  effect->editCylinderHeight_  = j["CylinderHeight"];
                if (j.contains("BoxSize"))    { effect->editBoxSize_.x = j["BoxSize"][0]; effect->editBoxSize_.y = j["BoxSize"][1]; effect->editBoxSize_.z = j["BoxSize"][2]; }
                if (j.contains("PlaneSize"))  { effect->editPlaneSize_.x = j["PlaneSize"][0]; effect->editPlaneSize_.y = j["PlaneSize"][1]; }
                if (j.contains("TorusMajorRadius")) effect->editTorusMajorRadius_ = j["TorusMajorRadius"];
                if (j.contains("TorusMinorRadius")) effect->editTorusMinorRadius_ = j["TorusMinorRadius"];
                if (j.contains("ConeRadius"))  effect->editConeRadius_  = j["ConeRadius"];
                if (j.contains("ConeHeight"))  effect->editConeHeight_  = j["ConeHeight"];
                if (j.contains("RingOuterRadius")) effect->editRingOuterRadius_ = j["RingOuterRadius"];
                if (j.contains("RingInnerRadius")) effect->editRingInnerRadius_ = j["RingInnerRadius"];
                if (j.contains("TriangleSize"))effect->editTriangleSize_ = j["TriangleSize"];
                if (j.contains("MeshSegments"))effect->editMeshSegments_ = j["MeshSegments"];
                if (j.contains("UvTiling")) { effect->editUvTiling_.x = j["UvTiling"][0]; effect->editUvTiling_.y = j["UvTiling"][1]; }
                effect->UpdateProceduralMesh();
            }
        }

        // --- 座標・回転を直接適用 (JSONのPosition/Rotationは無視) ---
        Vector3 finalPos = worldPos;
        Vector3 finalRot = worldRot;

        // VolumeMode 対応
        if (volumeMode == 1 && i == 1) {
            finalRot.x += 1.570796f; // 90度クロス
        } else if (volumeMode == 2) {
            float gap = 0.02f;
            if (i == 1) { finalPos.x += localZ.x * gap; finalPos.y += localZ.y * gap; finalPos.z += localZ.z * gap; }
            if (i == 2) { finalPos.x -= localZ.x * gap; finalPos.y -= localZ.y * gap; finalPos.z -= localZ.z * gap; }
        }

        effect->SetTranslate(finalPos);
        effect->SetRotation(finalRot);
        effect->Play(lifetime);
        effect->Update(0.0f);
        effect->UpdateLocalMatrix();
        effect->UpdateWorldMatrix();

        activeEffects_.push_back(std::move(effect));
    }
}

void MeshEffectManager::SpawnEffectAtScoped(
    EffectScopeId scopeId,
    const std::string& jsonFilePath,
    const Vector3& worldPos,
    const Vector3& worldRot,
    const Vector3& scale) {
    const size_t firstNewEffect = activeEffects_.size();
    SpawnEffectAt(jsonFilePath, worldPos, worldRot, scale);

    if (scopeId == kInvalidEffectScope) {
        return;
    }
    for (size_t index = firstNewEffect; index < activeEffects_.size(); ++index) {
        effectScopes_[activeEffects_[index].get()] = scopeId;
    }
}

void MeshEffectManager::SpawnRingWaveEffect(const Vector3& position) {
    if (!common_) {
        auto sm = SceneManager::GetInstance();
        if (sm->GetCurrentScene()) {
            common_ = sm->GetCurrentScene()->GetObject3dCommon();
        }
    }
    if (!common_) return;

    auto effect = std::make_unique<EffectObject3d>();
    effect->Initialize(common_);

    // エフェクトの基本設定リセット
    effect->SetEnableNoiseTexture(false);
    effect->SetEnableColorRamp(false);
    effect->SetEnableDistortion(false);
    effect->SetEnableReveal(false);
    effect->SetDistortionStrength(0.0f);
    effect->SetEdgeFadeStrength(1.0f);

    // Ring状の波紋Effectを生成する既定設定です。
    effect->SetProceduralType(10); // 10: Ring
    
    // UVスクロールで波紋を外側に広げる (V方向スクロール)
    // AddressV=CLAMP にしているので、1回の波で終わる
    effect->SetScrollSpeed({ 0.0f, -1.5f }); 
    
    // スケールを徐々に大きくする
    effect->SetStartScale({ 1.0f, 1.0f, 1.0f });
    effect->SetEndScale({ 6.0f, 6.0f, 6.0f });
    
    // 青白い発光色を保ちながらAlphaを減衰させます。
    effect->SetStartColor({ 0.5f, 0.8f, 1.0f, 1.0f });
    effect->SetEndColor({ 0.5f, 0.8f, 1.0f, 0.0f });
    
    // 発光の強さ
    effect->SetIntensity(2.0f);
    
    // 加算合成（透けるように光らせる）
    effect->SetBlendMode(BlendMode::kAdd);

    // リングの幅を設定
    effect->editRingInnerRadius_ = 0.5f;
    effect->editRingOuterRadius_ = 1.5f;
    effect->editMeshSegments_ = 32;

    // メッシュを更新
    effect->UpdateProceduralMesh();

    // テクスチャ設定 (スライドの gradationLine.png)
    if (effect->GetMeshRenderer()) {
        effect->GetMeshRenderer()->SetTexture("Resources/sprite/effect/gradationLine.png");
    }

    // 配置
    effect->SetTranslate(position);
    effect->SetRotation({ 0.0f, 0.0f, 0.0f });
    
    // アニメーション再生（0.6秒）
    effect->Play(0.6f);
    effect->Update(0.0f);
    effect->UpdateLocalMatrix();
    effect->UpdateWorldMatrix();

    activeEffects_.push_back(std::move(effect));
}

// ==========================================================
// 横方向UV ScrollとColor補間を使うCylinder Portal Effectです。
//   - Cylinderメッシュ (ProceduralType 5)
//   - U方向（横方向）にUVスクロール
// StartColorからEndColorへ補間し、時間変化するPortal表現にします。
//   - alphaReference = 0.0 なのでテクスチャのαが0以外なら描画
//   - Culling=NONE, DepthWrite=ZERO (パイプライン側で設定済み)
// ==========================================================
void MeshEffectManager::SpawnPortalEffect(const Vector3& position, float lifetime) {
    if (!common_) {
        auto sm = SceneManager::GetInstance();
        if (sm->GetCurrentScene()) {
            common_ = sm->GetCurrentScene()->GetObject3dCommon();
        }
    }
    if (!common_) return;

    auto effect = std::make_unique<EffectObject3d>();
    effect->Initialize(common_);

    // --- デフォルトリセット ---
    effect->SetProceduralType(0);
    effect->SetEnableNoiseTexture(false);
    effect->SetEnableColorRamp(false);
    effect->SetEnableDistortion(false);
    effect->SetEnableReveal(false);        // Revealは使わない
    effect->SetDistortionStrength(0.0f);
    effect->SetEdgeFadeStrength(1.0f);

    // ProceduralType 5のCylinder Meshを使用します。
    effect->SetProceduralType(5);
    effect->editCylinderRadius_ = 1.5f;   // 半径
    effect->editCylinderHeight_ = 3.0f;   // 高さ
    effect->editMeshSegments_   = 32;     // 分割数（スライドの kCylinderDivide と同じ）
    // UVタイリング: U方向に4回タイリングで縞々模様に
    effect->editUvTiling_ = { 4.0f, 1.0f };
    effect->UpdateProceduralMesh();

    // U方向へTextureをScrollします。
    effect->SetScrollSpeed({ 0.8f, 0.0f }); // 横方向スクロール

    // 青紫から水色へColorを補間します。
    effect->SetStartColor({ 0.3f, 0.1f, 1.0f, 0.9f }); // 青紫
    effect->SetEndColor(  { 0.0f, 0.8f, 1.0f, 0.6f }); // 水色

    // 発光強度
    effect->SetIntensity(2.5f);

    // 完全透明PixelだけをDiscardし、半透明部分は描画します。
    effect->SetAlphaReference(0.0f);

    // 加算合成でポータルらしく光らせる
    effect->SetBlendMode(BlendMode::kAdd);

    // グラデーションラインテクスチャで縦縞を作る
    if (effect->GetMeshRenderer()) {
        effect->GetMeshRenderer()->SetTexture("Resources/sprite/effect/gradationLine.png");
    }

    // Scaleを小さく往復させて脈動を表現します。
    effect->SetStartScale({ 1.0f, 1.0f, 1.0f });
    effect->SetEndScale(  { 1.1f, 1.0f, 1.1f });

    // 配置
    effect->SetTranslate(position);
    effect->SetRotation({ 0.0f, 0.0f, 0.0f });

    // 再生
    effect->Play(lifetime);
    effect->Update(0.0f);
    effect->UpdateLocalMatrix();
    effect->UpdateWorldMatrix();

    activeEffects_.push_back(std::move(effect));
}
