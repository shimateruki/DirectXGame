#include "MeshEffectManager.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "IconsFontAwesome5.h"
#include "json.hpp"
#include <fstream>
#include <DebugConsole.h>
#include "SceneManager.h"
#include "BaseScene.h"
#include <functional>
#include <CollisionManager.h>
using json = nlohmann::json;

MeshEffectManager* MeshEffectManager::GetInstance() {
    static MeshEffectManager instance;
    return &instance;
}

void MeshEffectManager::Initialize(Object3dCommon* common) {
    common_ = common;
}

void MeshEffectManager::Update(float deltaTime) {
    // リストの中を回して、寿命が切れたエフェクトを削除する
    for (auto it = activeEffects_.begin(); it != activeEffects_.end();) {
        if (!(*it)->IsPlaying()) {


            if ((*it)->editHasCollision_) {
                CollisionManager::GetInstance()->RemoveObject(it->get());
            }

            // 再生が終了していたらリストから削除（メモリも自動解放される）
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

void MeshEffectManager::Draw(ID3D12Resource* pLight, ID3D12Resource* sLight) {
    for (auto& effect : activeEffects_) {
        effect->Draw(pLight, sLight);
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

    std::ifstream file(jsonFilePath);
    if (!file.is_open()) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[MeshEffectManager] Failed to open effect json: " + jsonFilePath);
        return;
    }

    json j;
    file >> j;
    file.close();

    // ==========================================
    // ★ 1. 基準となるターゲットの取得（位置と向きを分離！）
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

    // ① 位置は指定されたボーン（posTarget）に正確に合わせる
    if (posTarget) {
        basePos = posTarget->GetWorldPosition();
    }

    // ② 向き（Y回転）はボーンのねじれを完全に無視し、一番大元（ルート）の向きだけを取る！
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
    // ★ 2. オフセットの読み込みと「回転計算」
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

    // ★超重要：エディタで作った「右側」などのオフセット位置を、プレイヤーの大元の向きに合わせて回転させる！
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
    // ★ 3. ループ生成
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
        if (j.contains("NoiseTexturePath")) { std::string np = j["NoiseTexturePath"]; if (!np.empty()) { effect->SetNoiseTexture(TextureManager::GetInstance()->Load(np)); effect->SetEnableNoiseTexture(true); } }
        if (j.contains("RampTexturePath")) { std::string rp = j["RampTexturePath"]; if (!rp.empty()) { effect->SetRampTexture(TextureManager::GetInstance()->Load(rp)); effect->SetEnableColorRamp(true); } }
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
        // ★ 当たり判定(Collision)の復元とマネージャーへの登録
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
                // ★ 修正箇所: 現在の設定を取り出して、正しく上書きする！
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

        // --- ★ 4. 立体化のための座標・回転適用 ---
        Vector3 finalPos = { basePos.x + rotatedOffset.x, basePos.y + rotatedOffset.y, basePos.z + rotatedOffset.z };

        // ★回転はシンプルにY軸だけを足す！（行列バグ防止）
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

    std::ifstream file(jsonFilePath);
    if (!file.is_open()) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[MeshEffectManager] SpawnEffectAt: failed to open " + jsonFilePath);
        return;
    }

    json j;
    file >> j;
    file.close();

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
        if (j.contains("NoiseTexturePath")) { std::string np = j["NoiseTexturePath"]; if (!np.empty()) { effect->SetNoiseTexture(TextureManager::GetInstance()->Load(np)); effect->SetEnableNoiseTexture(true); } }
        if (j.contains("RampTexturePath")) { std::string rp = j["RampTexturePath"]; if (!rp.empty()) { effect->SetRampTexture(TextureManager::GetInstance()->Load(rp)); effect->SetEnableColorRamp(true); } }
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