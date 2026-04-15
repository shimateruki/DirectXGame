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

void MeshEffectManager::SpawnEffect(const std::string& jsonFilePath, Object3d* baseObject) {
    if (!common_) return;

    std::ifstream file(jsonFilePath);
    if (!file.is_open()) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "Failed to open effect json: " + jsonFilePath);
        return;
    }

    json j;
    file >> j;
    file.close();

    // ==========================================
    // ★ 1. 基準となるターゲットの取得とY軸の計算
    // ==========================================
    Vector3 basePos = { 0, 0, 0 };
    float targetWorldY = 0.0f; // ★プレイヤーの「向き」だけを抽出する

    Object3d* target = baseObject;
    if (!target && j.contains("TargetName")) {
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
                    target = findObj(obj.get(), targetName);
                    if (target) break;
                }
            }
        }
    }

    if (target) {
        basePos = target->GetWorldPosition();

        // ★ターゲットの「Y軸回転（向いている方向）」だけを全階層からかき集める
        Object3d* curr = target;
        while (curr) {
            targetWorldY += curr->GetRotation().y;
            curr = curr->GetParent();
        }
    }

    // ==========================================
    // ★ 2. オフセットの読み込みと「回転計算」
    // ==========================================
    Vector3 offsetPos = { 0, 0, 0 };
    Vector3 offsetRot = { 0, 0, 0 };
    if (j.contains("Position")) offsetPos = { j["Position"][0], j["Position"][1], j["Position"][2] };
    if (j.contains("Rotation")) offsetRot = { j["Rotation"][0], j["Rotation"][1], j["Rotation"][2] };

    // ★超重要：エディタで作った「右側」などのオフセット位置を、プレイヤーの向きに合わせて回転させる！
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
        if (j.contains("StartScale")) effect->SetStartScale({ j["StartScale"][0], j["StartScale"][1], j["StartScale"][2] });
        if (j.contains("EndScale")) effect->SetEndScale({ j["EndScale"][0], j["EndScale"][1], j["EndScale"][2] });
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
                if (j.contains("MeshSegments")) effect->editMeshSegments_ = j["MeshSegments"];

                // ★ これを呼ぶことで、ロードした数値をもとに「突き」や「三日月」の形を実際に構築する
                effect->UpdateProceduralMesh();
            }
        }

        // --- ★ 4. 立体化のための座標・回転適用 ---
        // 最終座標 ＝ ターゲット座標 ＋ 向きに合わせて回転させたオフセット
        Vector3 finalPos = { basePos.x + rotatedOffset.x, basePos.y + rotatedOffset.y, basePos.z + rotatedOffset.z };

        // 最終回転 ＝ エディタの回転 ＋ プレイヤーのY軸（向き）だけ足す！ (XやZを足すとエフェクトが地面にめり込む)
        Vector3 finalRot = { offsetRot.x, offsetRot.y + targetWorldY, offsetRot.z };

        Vector3 localZ;
        localZ.x = sinf(finalRot.y) * cosf(finalRot.x);
        localZ.y = -sinf(finalRot.x);
        localZ.z = cosf(finalRot.y) * cosf(finalRot.x);

        if (volumeMode == 1 && i == 1) {
            finalRot.x += 1.570796f;
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