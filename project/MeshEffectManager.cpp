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

    // 新しいエフェクトの作成
    auto effect = std::make_unique<EffectObject3d>();
    effect->Initialize(common_);

    // ==========================================
    // JSONからパラメータを復元 (Editorと同じ処理)
    // ==========================================
    if (j.contains("ModelName")) {
        effect->SetModel(j["ModelName"].get<std::string>());
    }

    if (j.contains("TexturePath")) {
        std::string texPath = j["TexturePath"];
        if (!texPath.empty()) {
            if (auto renderer = effect->GetMeshRenderer()) renderer->SetTexture(texPath);
        }
    }

    if (j.contains("NoiseTexturePath")) {
        std::string noisePath = j["NoiseTexturePath"];
        if (!noisePath.empty()) {
            effect->SetNoiseTexture(TextureManager::GetInstance()->Load(noisePath));
            effect->SetEnableNoiseTexture(true);
        }
        else {
            effect->SetEnableNoiseTexture(false);
        }
    }

    if (j.contains("RampTexturePath")) {
        std::string rampPath = j["RampTexturePath"];
        if (!rampPath.empty()) {
            effect->SetRampTexture(TextureManager::GetInstance()->Load(rampPath));
            effect->SetEnableColorRamp(true);
        }
        else {
            effect->SetEnableColorRamp(false);
        }
    }

    // アニメーション (Start/End)
    if (j.contains("StartScale")) effect->SetStartScale({ j["StartScale"][0], j["StartScale"][1], j["StartScale"][2] });
    if (j.contains("EndScale"))   effect->SetEndScale({ j["EndScale"][0], j["EndScale"][1], j["EndScale"][2] });
    if (j.contains("StartColor")) effect->SetStartColor({ j["StartColor"][0], j["StartColor"][1], j["StartColor"][2], j["StartColor"][3] });
    if (j.contains("EndColor"))   effect->SetEndColor({ j["EndColor"][0], j["EndColor"][1], j["EndColor"][2], j["EndColor"][3] });

    // シェーダーパラメータ
    if (j.contains("ScrollSpeed")) effect->SetScrollSpeed({ j["ScrollSpeed"][0], j["ScrollSpeed"][1] });
    if (j.contains("Intensity")) effect->SetIntensity(j["Intensity"]);
    if (j.contains("DistortionStrength")) effect->SetDistortionStrength(j["DistortionStrength"]);
    if (j.contains("DistortionSpeed")) effect->SetDistortionSpeed(j["DistortionSpeed"]);
    if (j.contains("EdgeFadeStrength")) effect->SetEdgeFadeStrength(j["EdgeFadeStrength"]);
    if (j.contains("EnableDistortion")) effect->SetEnableDistortion(j["EnableDistortion"]);
    if (j.contains("BlendMode")) effect->SetBlendMode(static_cast<BlendMode>(j["BlendMode"].get<int>()));
    if (j.contains("EnableReveal")) {
        effect->SetEnableReveal(j["EnableReveal"]);
    }
    else {
        effect->SetEnableReveal(true); // 互換性維持
    }
    if (j.contains("EasingType")) {
        effect->SetEasingType(j["EasingType"]);
    }
    // ==========================================
    // ★ 基準となる座標・角度の決定ロジック
    // ==========================================
    Vector3 basePos = { 0, 0, 0 };
    Vector3 baseRot = { 0, 0, 0 };

    // ① 引数でターゲットが直接指定されていれば、それを最優先（ボスの使い回し等）
    if (baseObject) {
        basePos = baseObject->GetWorldPosition();
        baseRot = baseObject->GetRotation();
    }
    // ② 引数が無い場合、JSONに「TargetName」があればシーン内を全自動検索！
    else if (j.contains("TargetName")) {
        std::string targetName = j["TargetName"];
        if (!targetName.empty()) {
            SceneManager* sm = SceneManager::GetInstance();
            if (sm && sm->GetCurrentScene()) {
                auto& objects = sm->GetCurrentScene()->GetObjects();

                // シーン内の親子階層を全て探す再帰関数
                std::function<Object3d* (Object3d*, const std::string&)> findObj = [&](Object3d* obj, const std::string& name) -> Object3d* {
                    if (!obj) return nullptr;
                    if (obj->GetName() == name) return obj;
                    for (auto* child : obj->GetChildren()) {
                        Object3d* found = findObj(child, name);
                        if (found) return found;
                    }
                    return nullptr;
                    };

                Object3d* foundTarget = nullptr;
                for (auto& obj : objects) {
                    foundTarget = findObj(obj.get(), targetName);
                    if (foundTarget) break; // 見つけたら探索終了
                }

                // シーン内にターゲットが見つかったら、そこを基準にする
                if (foundTarget) {
                    basePos = foundTarget->GetWorldPosition();
                    baseRot = foundTarget->GetRotation();
                }
            }
        }
    }

    // ==========================================
    // ★ エディタで作った「微調整オフセット」の加算
    // ==========================================
    Vector3 offsetPos = { 0, 0, 0 };
    Vector3 offsetRot = { 0, 0, 0 };
    if (j.contains("Position")) offsetPos = { j["Position"][0], j["Position"][1], j["Position"][2] };
    if (j.contains("Rotation")) offsetRot = { j["Rotation"][0], j["Rotation"][1], j["Rotation"][2] };

    // 最終的な座標 = (基準の座標) + (オフセット)
    effect->SetTranslate({ basePos.x + offsetPos.x, basePos.y + offsetPos.y, basePos.z + offsetPos.z });
    effect->SetRotation({ baseRot.x + offsetRot.x, baseRot.y + offsetRot.y, baseRot.z + offsetRot.z });

    // ==========================================
    // 再生開始
    // ==========================================
    float lifetime = 1.0f;
    if (j.contains("Lifetime")) lifetime = j["Lifetime"];
    effect->Play(lifetime);

    // リストに追加して管理を任せる
    activeEffects_.push_back(std::move(effect));
}