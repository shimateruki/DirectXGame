#include "MeshEffectManager.h"
#include <numbers>
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
// 解析済みのプリセットと取得できた更新時刻。時刻が一致するときだけ再読込を省く。
// 担当：解析済みエフェクトJSONと、再読込判定用の更新時刻。
// 関連：MeshEffectManagerの内部処理だけで使う補助型。
struct EffectJsonCacheEntry {
    json data;
    std::filesystem::file_time_type writeTime{};
    bool hasWriteTime = false;
};

bool LoadEffectJsonCached(const std::string& jsonFilePath, json& outJson) {
    // 更新時刻が同じ設定は解析済みJSONを再利用し、エフェクト生成時のI/Oを減らす。
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

void MeshEffectManager::Initialize(engine::Object3DCommon* common) {
    // エフェクト自身も生成時のcommonを保持します。管理側のポインタだけを
    // 差し替えると、前Sceneの描画基盤を参照したエフェクトが描画されます。
    if (common_ != common)
        Clear();
    common_ = common;
    timeScale_ = 1.0f;
}

void MeshEffectManager::ClearActiveEffects() {
    // 所有物を消す前に、衝突管理とデバッグ表示の非所有参照を解除します。
    for (const auto& effect : activeEffects_) {
        // 編集途中で判定フラグをOFFにした場合も、以前の登録を残しません。
        if (effect) {
            CollisionManager::GetInstance()->RemoveObject(effect.get());
            reusableEffects_.erase(effect.get());
        }
    }

    previewEffectForDebug_ = nullptr;
    activeEffects_.clear();
    effectScopes_.clear();
    timelineScopes_.clear();
    timelineOffsets_.clear();
    updatedThisFrame_ = false;
}

void MeshEffectManager::Clear() {
    ClearActiveEffects();
    recycledEffects_.clear();
    reusableEffects_.clear();
    preloadedEffects_.clear();
    EffectObject3D::ClearProceduralMeshCache();
    common_ = nullptr;
    timeScale_ = 1.0f;
}

void MeshEffectManager::RecycleEffect(std::unique_ptr<EffectObject3D> effect) {
    if (!effect) return;
    const bool reusable = reusableEffects_.erase(effect.get()) != 0;
    // 編集対象の個体は回収せず、未保存の変更が他の演出へ混ざるのを防ぎます。
    if (previewEffectForDebug_ == effect.get()) {
        previewEffectForDebug_ = nullptr;
        return;
    }
    if (reusable && !effect->GetMeshSettings().hasCollision && !effect->GetParent() &&
        recycledEffects_.size() < kMaxRecycledEffects) {
        CollisionManager::GetInstance()->RemoveObject(effect.get());
        recycledEffects_.push_back(std::move(effect));
    }
}

void MeshEffectManager::BeginFrame() {
    updatedThisFrame_ = false;
}

void MeshEffectManager::PreloadEffect(const std::string& jsonFilePath) {
    // 同じ設定を複数回GPUへ準備しないよう、ロード済みパスを先に確認する。
    if (preloadedEffects_.find(jsonFilePath) != preloadedEffects_.end()) {
        return;
    }

    if (!common_) {
        SceneManager* sm = SceneManager::GetInstance();

        if (sm && sm->GetCurrentScene()) {
            common_ = sm->GetCurrentScene()->GetObject3DCommon();
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

    auto effect = std::make_unique<EffectObject3D>();

    // 設定ファイルに値がない項目は安全な初期値に戻してから、存在する項目だけ適用する。
    effect->Initialize(common_);
    effect->SetProceduralType(0);
    effect->SetEnableNoiseTexture(false);
    effect->SetEnableColorRamp(false);

    if (j.contains("UseTextureColor")) {
        effect->SetUseTextureColor(j["UseTextureColor"].get<bool>());
    }

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
        // ノイズは線形値、Rampは表示色として読み込み、用途に応じた色空間を使う。
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
        // プロシージャル形状だけは全パラメータを反映した後に一度生成する。
        int procType = j["ProceduralType"];
        effect->SetProceduralType(procType);

        if (procType >= 1) {
            if (j.contains("SlashAngle"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::slashAngle, j["SlashAngle"]);
            if (j.contains("InnerRadius"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::innerRadius, j["InnerRadius"]);
            if (j.contains("OuterRadius"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::outerRadius, j["OuterRadius"]);
            if (j.contains("Thickness"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::thickness, j["Thickness"]);
            if (j.contains("SpiralPitch"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::spiralPitch, j["SpiralPitch"]);
            if (j.contains("ThrustLength"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::thrustLength, j["ThrustLength"]);
            if (j.contains("ThrustRadius"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::thrustRadius, j["ThrustRadius"]);
            if (j.contains("SphereRadius"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::sphereRadius, j["SphereRadius"]);
            if (j.contains("SphereRings"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::sphereRings, j["SphereRings"]);
            if (j.contains("CylinderRadius"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::cylinderRadius, j["CylinderRadius"]);
            if (j.contains("CylinderHeight"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::cylinderHeight, j["CylinderHeight"]);
            if (j.contains("BoxSize")) {
                effect->SetMeshComponent(&EffectObject3D::MeshSettings::boxSize, &engine::Vector3::x, j["BoxSize"][0]);
                effect->SetMeshComponent(&EffectObject3D::MeshSettings::boxSize, &engine::Vector3::y, j["BoxSize"][1]);
                effect->SetMeshComponent(&EffectObject3D::MeshSettings::boxSize, &engine::Vector3::z, j["BoxSize"][2]);
            }
            if (j.contains("PlaneSize")) {
                effect->SetMeshComponent(&EffectObject3D::MeshSettings::planeSize, &engine::Vector2::x, j["PlaneSize"][0]);
                effect->SetMeshComponent(&EffectObject3D::MeshSettings::planeSize, &engine::Vector2::y, j["PlaneSize"][1]);
            }
            if (j.contains("TorusMajorRadius"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::torusMajorRadius, j["TorusMajorRadius"]);
            if (j.contains("TorusMinorRadius"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::torusMinorRadius, j["TorusMinorRadius"]);
            if (j.contains("ConeRadius"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::coneRadius, j["ConeRadius"]);
            if (j.contains("ConeHeight"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::coneHeight, j["ConeHeight"]);
            if (j.contains("RingOuterRadius"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::ringOuterRadius, j["RingOuterRadius"]);
            if (j.contains("RingInnerRadius"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::ringInnerRadius, j["RingInnerRadius"]);
            if (j.contains("TriangleSize"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::triangleSize, j["TriangleSize"]);
            if (j.contains("MeshSegments"))
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::meshSegments, j["MeshSegments"]);
            if (j.contains("UvTiling")) {
                effect->SetMeshComponent(&EffectObject3D::MeshSettings::uvTiling, &engine::Vector2::x, j["UvTiling"][0]);
                effect->SetMeshComponent(&EffectObject3D::MeshSettings::uvTiling, &engine::Vector2::y, j["UvTiling"][1]);
            }
            effect->UpdateProceduralMesh();
        }
    }

    // プリロードで確保した表示物も、単発演出の初回に再利用します。
    if (recycledEffects_.size() < kMaxRecycledEffects)
        recycledEffects_.push_back(std::move(effect));
    preloadedEffects_.insert(jsonFilePath);
}

// 同じフレームの二重更新を避け、タイムライン管理外のエフェクトの寿命を進めます。
void MeshEffectManager::Update(float deltaTime) {
    if (updatedThisFrame_) {
        return;
    }

    updatedThisFrame_ = true;
    deltaTime *= timeScale_;

    // リストの中を回して、寿命が切れたエフェクトを削除する
    for (auto it = activeEffects_.begin(); it != activeEffects_.end();) {
        const auto scope = effectScopes_.find(it->get());

        if (scope != effectScopes_.end() && timelineScopes_.count(scope->second)) {
            ++it; // 寿命も攻撃側が管理するので、一時停止中に勝手に消さない。
            continue;
        }

        if (!(*it)->IsPlaying()) {
            EffectObject3D* finishedEffect = it->get();

            if ((*it)->GetMeshSettings().hasCollision) {
                CollisionManager::GetInstance()->RemoveObject(it->get());
            }

            // 再生が終了したエフェクトは、所有スコープの追跡情報も同時に破棄します。
            effectScopes_.erase(finishedEffect);

            RecycleEffect(std::move(*it));
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

// Editorの単独ステップではフレーム済みフラグを戻してから更新します。
void MeshEffectManager::UpdateEditorPreviewStep(float deltaTime) {
    updatedThisFrame_ = false;
    Update(deltaTime);
}

void MeshEffectManager::Draw(ID3D12Resource* pLight, ID3D12Resource* sLight) {
    for (auto& effect : activeEffects_) {
        const auto scope = effectScopes_.find(effect.get());

        if (scope != effectScopes_.end() && timelineScopes_.count(scope->second)) {
            // 再生停止中でも、プレビューカメラを動かした結果は描画へ反映します。
            effect->RefreshRenderCameraData();
        }

        effect->Draw(pLight, sLight);
    }
}

// 再生中の歪みエフェクトがあれば、背景色テクスチャの複製が必要と返します。
bool MeshEffectManager::RequiresSceneColorCopy() const {
    return std::any_of(
        activeEffects_.begin(),
        activeEffects_.end(),
        [](const std::unique_ptr<EffectObject3D>& effect) {
            return effect && effect->IsPlaying() && effect->IsDistortionEnabled();
        });
}

// 個別停止に使うIDを払い出し、無効値は飛ばします。
MeshEffectManager::EffectScopeId MeshEffectManager::CreateEffectScope() {
    EffectScopeId scopeId = nextEffectScopeId_++;

    if (scopeId == kInvalidEffectScope) {
        scopeId = nextEffectScopeId_++;
    }

    return scopeId;
}

void MeshEffectManager::StopEffectScope(EffectScopeId scopeId) {
    // 指定タイムラインに属するエフェクトだけを削除し、別スコープの演出は保持する。
    if (scopeId == kInvalidEffectScope) {
        return;
    }

    timelineScopes_.erase(scopeId);

    for (auto it = activeEffects_.begin(); it != activeEffects_.end();) {
        EffectObject3D* effect = it->get();
        const auto scopeIt = effectScopes_.find(effect);

        if (scopeIt == effectScopes_.end() || scopeIt->second != scopeId) {
            ++it;
            continue;
        }

        if (effect && effect->GetMeshSettings().hasCollision) {
            // 見た目の削除と同時に衝突Managerの登録も外す。
            CollisionManager::GetInstance()->RemoveObject(effect);
        }

        effectScopes_.erase(scopeIt);
        timelineOffsets_.erase(effect);
        RecycleEffect(std::move(*it));
        it = activeEffects_.erase(it);
    }
}

// 同じScopeの旧演出を止めてから生成し、攻撃判定は別トラックに任せます。
// 生成に失敗した場合は追加した演出を破棄してfalseを返します。
bool MeshEffectManager::StartTimelineEffect(EffectScopeId scopeId, const std::string& path) {
    if (scopeId == kInvalidEffectScope)
        return false;
    StopEffectScope(scopeId);
    const size_t firstEffect = activeEffects_.size();
    try {
        SpawnEffectAtScoped(scopeId, path, {}, {}, {1, 1, 1});
    } catch (const std::exception& exception) {
        while (activeEffects_.size() > firstEffect) {
            effectScopes_.erase(activeEffects_.back().get());
            reusableEffects_.erase(activeEffects_.back().get());
            activeEffects_.pop_back();
        }

        StopEffectScope(scopeId);
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[AttackTimeline] " + path + ": " + exception.what());
        return false;
    }

    bool found = false;

    for (auto& effect : activeEffects_) {
        const auto it = effectScopes_.find(effect.get());
        if (it == effectScopes_.end() || it->second != scopeId)
            continue;

        // 表示物のプリセットに衝突設定があっても、判定は専用トラックだけが担当します。
        if (effect->GetMeshSettings().hasCollision)
            CollisionManager::GetInstance()->RemoveObject(effect.get());
        effect->SetMeshSetting(&EffectObject3D::MeshSettings::hasCollision, false);
        effect->SetTargetObject(nullptr);
        effect->SetAutoLoop(false);
        timelineOffsets_[effect.get()] = {effect->GetTranslate(), effect->GetRotation()};
        found = true;
    }

    if (found)
        timelineScopes_.insert(scopeId);
    return found;
}

void MeshEffectManager::EvaluateTimelineEffect(EffectScopeId scopeId, float time, float duration,
    const engine::Vector3& position, const engine::Vector3& rotation, const engine::Vector3& scale,
    bool preservePresetScale) {
    if (!timelineScopes_.count(scopeId))
        return;
    for (auto& effect : activeEffects_) {
        const auto it = effectScopes_.find(effect.get());
        if (it == effectScopes_.end() || it->second != scopeId)
            continue;
        const float resolvedDuration = duration > 0.0f ? duration : effect->GetLifetime();
        effect->Play(resolvedDuration);
        effect->Update(std::clamp(time, 0.0f, (std::max)(0.0f, resolvedDuration)));

        // プリセット内部の位置・スケールアニメーションに重ねず、制作者のキーを優先します。
        const auto& offset = timelineOffsets_.at(effect.get());

        // VolumeModeの交差面・厚み用の相対配置を残して、部品全体を動かします。
        effect->SetTranslate(engine::Math::Transform(offset.position, engine::Math::MakeAffineMatrix(scale, rotation, position)));
        effect->SetRotation(rotation + offset.rotation);
        const auto presetScale = effect->GetScale();
        effect->SetScale(preservePresetScale ? engine::Vector3{
            presetScale.x * scale.x, presetScale.y * scale.y, presetScale.z * scale.z} : scale);
        effect->UpdateLocalMatrix();
        effect->UpdateWorldMatrix();
    }
}

void MeshEffectManager::SpawnEffect(const std::string& jsonFilePath, engine::Object3D* baseObject, const engine::Vector3& extOffset, const engine::Vector3& extRot, const engine::Vector3& extScale) {
    // common_ が null のとき、現在シーンから自動取得を試みる（Initialize呼び忘れ対策）
    if (!common_) {
        SceneManager* sm = SceneManager::GetInstance();

        if (sm && sm->GetCurrentScene()) {
            common_ = sm->GetCurrentScene()->GetObject3DCommon();
        }
    }

    if (!common_) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[MeshEffectManager] SpawnEffect ABORT: common_ is null!");
        return;
    }

    // 描画基盤を確定してから設定を読み、失敗時には半端なエフェクトを登録しません。
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
    // ★ 1. 基準となるターゲットの取得（位置と向きを分離！）
    // ==========================================
    engine::Vector3 basePos = engine::Vector3::ZERO;
    float targetWorldY = 0.0f; // ★プレイヤーの「向き」

    engine::Object3D* posTarget = baseObject; // 位置の基準
    engine::Object3D* rotTarget = baseObject; // 向きの基準

    // JSONでTargetNameが指定されている場合、特定のボーンなどを探す
    if (j.contains("TargetName")) {
        std::string targetName = j["TargetName"];

        if (!targetName.empty()) {
            SceneManager* sm = SceneManager::GetInstance();

            if (sm && sm->GetCurrentScene()) {
                auto& objects = sm->GetCurrentScene()->GetObjects();
                std::function<engine::Object3D* (engine::Object3D*, const std::string&)> findObj = [&](engine::Object3D* obj, const std::string& name) -> engine::Object3D* {
                    if (!obj)
                        return nullptr;
                    if (obj->GetName() == name)
                        return obj;
                    for (auto* child : obj->GetChildren()) {
                        engine::Object3D* found = findObj(child, name);
                        if (found)
                            return found;
                    }

                    return nullptr;
                    };

                for (auto& obj : objects) {
                    engine::Object3D* found = findObj(obj.get(), targetName);

                    if (found) {
                        posTarget = found; // 見つかったボーンを位置の基準にする

                        // rotTarget が null の場合のみ、見つけたオブジェクトをセットする
                        if (!rotTarget)
                            rotTarget = found;
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
        engine::Object3D* rootObj = rotTarget;

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
    engine::Vector3 offsetPos = engine::Vector3::ZERO;
    engine::Vector3 offsetRot = engine::Vector3::ZERO;

    if (j.contains("Position"))
        offsetPos = { j["Position"][0], j["Position"][1], j["Position"][2] };
    if (j.contains("Rotation"))
        offsetRot = { j["Rotation"][0], j["Rotation"][1], j["Rotation"][2] };

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
    engine::Vector3 rotatedOffset;
    rotatedOffset.x = offsetPos.x * c + offsetPos.z * s;
    rotatedOffset.y = offsetPos.y;
    rotatedOffset.z = -offsetPos.x * s + offsetPos.z * c;

    // VolumeModeは同じ演出を複数面に展開するため、ここで生成個数へ変換します。
    int volumeMode = j.contains("VolumeMode") ? (int)j["VolumeMode"] : 0;
    int numSpawns = (volumeMode == 2) ? 3 : (volumeMode == 1 ? 2 : 1);
    float lifetime = j.contains("Lifetime") ? (float)j["Lifetime"] : 1.0f;

    // ==========================================
    // ★ 3. ループ生成
    // ==========================================
    for (int i = 0; i < numSpawns; ++i) {
        auto effect = std::make_unique<EffectObject3D>();
        effect->Initialize(common_);

        // 初期化リセット
        effect->SetProceduralType(0);
        effect->SetEnableNoiseTexture(false);
        effect->SetEnableColorRamp(false);
        effect->SetEnableDistortion(false);
        effect->SetEnableReveal(true);
        effect->SetDistortionStrength(0.0f);
        effect->SetEdgeFadeStrength(1.0f);

        // --- パラメータ復元 ---
        if (j.contains("ModelName"))
            effect->SetModel(j["ModelName"].get<std::string>());
        if (j.contains("TexturePath")) {
            std::string tp = j["TexturePath"];
            if (!tp.empty() && effect->GetMeshRenderer())
                effect->GetMeshRenderer()->SetTexture(tp);
        }
        if (j.contains("NoiseTexturePath")) {
            std::string np = j["NoiseTexturePath"];
            if (!np.empty()) {
                effect->SetNoiseTexture(TextureManager::GetInstance()->Load(np, TextureManager::TextureColorSpace::Linear));
                effect->SetEnableNoiseTexture(true);
            }
        }
        if (j.contains("RampTexturePath")) {
            std::string rp = j["RampTexturePath"];
            if (!rp.empty()) {
                effect->SetRampTexture(TextureManager::GetInstance()->Load(rp, TextureManager::TextureColorSpace::SRGB));
                effect->SetEnableColorRamp(true);
            }
        }
        if (j.contains("StartScale"))
            effect->SetStartScale({ j["StartScale"][0] * extScale.x, j["StartScale"][1] * extScale.y, j["StartScale"][2] * extScale.z });
        if (j.contains("EndScale"))
            effect->SetEndScale({ j["EndScale"][0] * extScale.x, j["EndScale"][1] * extScale.y, j["EndScale"][2] * extScale.z });
        if (j.contains("StartColor"))
            effect->SetStartColor({ j["StartColor"][0], j["StartColor"][1], j["StartColor"][2], j["StartColor"][3] });
        if (j.contains("EndColor"))
            effect->SetEndColor({ j["EndColor"][0], j["EndColor"][1], j["EndColor"][2], j["EndColor"][3] });
        if (j.contains("ScrollSpeed"))
            effect->SetScrollSpeed({ j["ScrollSpeed"][0], j["ScrollSpeed"][1] });
        if (j.contains("Intensity"))
            effect->SetIntensity(j["Intensity"]);
        if (j.contains("UseTextureColor"))
            effect->SetUseTextureColor(j["UseTextureColor"].get<bool>());
        if (j.contains("DistortionStrength"))
            effect->SetDistortionStrength(j["DistortionStrength"]);
        if (j.contains("DistortionSpeed"))
            effect->SetDistortionSpeed(j["DistortionSpeed"]);
        if (j.contains("EdgeFadeStrength"))
            effect->SetEdgeFadeStrength(j["EdgeFadeStrength"]);
        if (j.contains("EnableDistortion"))
            effect->SetEnableDistortion(j["EnableDistortion"]);
        if (j.contains("BlendMode"))
            effect->SetBlendMode(static_cast<BlendMode>(j["BlendMode"].get<int>()));
        if (j.contains("EnableReveal"))
            effect->SetEnableReveal(j["EnableReveal"]);
        if (j.contains("RevealMode"))
            effect->SetRevealMode(j["RevealMode"]);
        if (j.contains("EasingType"))
            effect->SetEasingType(j["EasingType"]);
        if (j.contains("AlphaReference"))
            effect->SetAlphaReference(j["AlphaReference"]);

        // =========================================================
        // プロシージャルパラメータの完全復元と構築
        // =========================================================
        if (j.contains("ProceduralType")) {
            int procType = j["ProceduralType"];
            effect->SetProceduralType(procType);

            if (procType >= 1) { // プロシージャルを使用する場合
                if (j.contains("SlashAngle"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::slashAngle, j["SlashAngle"]);
                if (j.contains("InnerRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::innerRadius, j["InnerRadius"]);
                if (j.contains("OuterRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::outerRadius, j["OuterRadius"]);
                if (j.contains("Thickness"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::thickness, j["Thickness"]);
                if (j.contains("SpiralPitch"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::spiralPitch, j["SpiralPitch"]);
                if (j.contains("ThrustLength"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::thrustLength, j["ThrustLength"]);
                if (j.contains("ThrustRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::thrustRadius, j["ThrustRadius"]);
                if (j.contains("SphereRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::sphereRadius, j["SphereRadius"]);
                if (j.contains("SphereRings"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::sphereRings, j["SphereRings"]);
                if (j.contains("CylinderRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::cylinderRadius, j["CylinderRadius"]);
                if (j.contains("CylinderHeight"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::cylinderHeight, j["CylinderHeight"]);
                if (j.contains("BoxSize")) {
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::boxSize, &engine::Vector3::x, j["BoxSize"][0]);
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::boxSize, &engine::Vector3::y, j["BoxSize"][1]);
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::boxSize, &engine::Vector3::z, j["BoxSize"][2]);
                }
                if (j.contains("PlaneSize")) {
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::planeSize, &engine::Vector2::x, j["PlaneSize"][0]);
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::planeSize, &engine::Vector2::y, j["PlaneSize"][1]);
                }
                if (j.contains("TorusMajorRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::torusMajorRadius, j["TorusMajorRadius"]);
                if (j.contains("TorusMinorRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::torusMinorRadius, j["TorusMinorRadius"]);
                if (j.contains("ConeRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::coneRadius, j["ConeRadius"]);
                if (j.contains("ConeHeight"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::coneHeight, j["ConeHeight"]);
                if (j.contains("RingOuterRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::ringOuterRadius, j["RingOuterRadius"]);
                if (j.contains("RingInnerRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::ringInnerRadius, j["RingInnerRadius"]);
                if (j.contains("TriangleSize"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::triangleSize, j["TriangleSize"]);
                if (j.contains("MeshSegments"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::meshSegments, j["MeshSegments"]);
                if (j.contains("UvTiling")) {
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::uvTiling, &engine::Vector2::x, j["UvTiling"][0]);
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::uvTiling, &engine::Vector2::y, j["UvTiling"][1]);
                }

                effect->UpdateProceduralMesh();
            }
        }

        // =========================================================
        // ★ 当たり判定(Collision)の復元とマネージャーへの登録
        // =========================================================
        if (j.contains("Collision")) {
            effect->SetMeshSetting(&EffectObject3D::MeshSettings::hasCollision, j["Collision"]["HasCollision"]);

            if (effect->GetMeshSettings().hasCollision) {
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::collisionShape, j["Collision"]["Shape"]);
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::collisionSize, { j["Collision"]["Size"][0], j["Collision"]["Size"][1], j["Collision"]["Size"][2] });
                effect->SetMeshSetting(&EffectObject3D::MeshSettings::collisionOffset, { j["Collision"]["Offset"][0], j["Collision"]["Offset"][1], j["Collision"]["Offset"][2] });

                ColliderType cType = ColliderType::None;
                if (effect->GetMeshSettings().collisionShape == 0)
                    cType = ColliderType::Sphere;
                else if (effect->GetMeshSettings().collisionShape == 1) cType = ColliderType::Aabb;
                else if (effect->GetMeshSettings().collisionShape == 2) cType = ColliderType::Obb;

                // =======================================================
                // ★ 修正箇所: 現在の設定を取り出して、正しく上書きする！
                // =======================================================
                engine::Object3D::ColliderConfig cConfig = effect->GetColliderConfig();
                cConfig.type = cType;
                cConfig.size = effect->GetMeshSettings().collisionSize;
                cConfig.center = effect->GetMeshSettings().collisionOffset;
                effect->SetColliderConfig(cConfig);

                // --- 属性とマスクの設定 ---
                effect->SetCollisionAttribute(CollisionCategories::Get("player_attack")); // 例: player_attack 相当
                effect->SetCollisionMask(CollisionCategories::Get("enemy"));      // 例: enemy 相当

                CollisionManager::GetInstance()->AddObject(effect.get());
            }
        }

        // --- ★ 4. 立体化のための座標・回転適用 ---
        engine::Vector3 finalPos = { basePos.x + rotatedOffset.x, basePos.y + rotatedOffset.y, basePos.z + rotatedOffset.z };

        // ★回転はシンプルにY軸だけを足す！（行列バグ防止）
        engine::Vector3 finalRot = { offsetRot.x, offsetRot.y + targetWorldY, offsetRot.z };

        engine::Vector3 localZ;
        localZ.x = sinf(finalRot.y) * cosf(finalRot.x);
        localZ.y = -sinf(finalRot.x);
        localZ.z = cosf(finalRot.y) * cosf(finalRot.x);

        if (volumeMode == 1 && i == 1) {
            finalRot.x += (std::numbers::pi_v<float> * 0.5f); // 90度
        }
        else if (volumeMode == 2) {
            float gap = 0.02f;

            if (i == 1) {
                finalPos.x += localZ.x * gap;
                finalPos.y += localZ.y * gap;
                finalPos.z += localZ.z * gap;
            }
            if (i == 2) {
                finalPos.x -= localZ.x * gap;
                finalPos.y -= localZ.y * gap;
                finalPos.z -= localZ.z * gap;
            }
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
void MeshEffectManager::SpawnEffectAt(const std::string& jsonFilePath, const engine::Vector3& worldPos, const engine::Vector3& worldRot,
    const engine::Vector3& scale, float lifetimeOverride) {
    // common_ が null なら現在シーンから自己修復
    if (!common_) {
        SceneManager* sm = SceneManager::GetInstance();

        if (sm && sm->GetCurrentScene()) {
            common_ = sm->GetCurrentScene()->GetObject3DCommon();
        }
    }

    if (!common_)
        return;

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
    // 個体ごとの攻撃速度を指定した場合は、見た目の進行も同じ秒数に揃える。
    if (lifetimeOverride > 0.0f)
        lifetime = lifetimeOverride;

    // localZ ベクトル (volumeMode 2 の立体化オフセット計算用)
    engine::Vector3 localZ;
    localZ.x = sinf(worldRot.y) * cosf(worldRot.x);
    localZ.y = -sinf(worldRot.x);
    localZ.z = cosf(worldRot.y) * cosf(worldRot.x);

    for (int i = 0; i < numSpawns; ++i) {
        std::unique_ptr<EffectObject3D> effect;
        if (!recycledEffects_.empty()) {
            effect = std::move(recycledEffects_.back());
            recycledEffects_.pop_back();
            effect->ResetForReuse();
        } else {
            effect = std::make_unique<EffectObject3D>();
            effect->Initialize(common_);
        }

        // 初期化リセット
        effect->SetProceduralType(0);
        effect->SetEnableNoiseTexture(false);
        effect->SetEnableColorRamp(false);
        effect->SetEnableDistortion(false);
        effect->SetEnableReveal(true);
        effect->SetDistortionStrength(0.0f);
        effect->SetEdgeFadeStrength(1.0f);

        // --- パラメータ復元 (SpawnEffect と同じ) ---
        if (j.contains("ModelName"))
            effect->SetModel(j["ModelName"].get<std::string>());
        if (j.contains("TexturePath")) {
            std::string tp = j["TexturePath"];
            if (!tp.empty() && effect->GetMeshRenderer())
                effect->GetMeshRenderer()->SetTexture(tp);
        }
        if (j.contains("NoiseTexturePath")) {
            std::string np = j["NoiseTexturePath"];
            if (!np.empty()) {
                effect->SetNoiseTexture(TextureManager::GetInstance()->Load(np, TextureManager::TextureColorSpace::Linear));
                effect->SetEnableNoiseTexture(true);
            }
        }
        if (j.contains("RampTexturePath")) {
            std::string rp = j["RampTexturePath"];
            if (!rp.empty()) {
                effect->SetRampTexture(TextureManager::GetInstance()->Load(rp, TextureManager::TextureColorSpace::SRGB));
                effect->SetEnableColorRamp(true);
            }
        }
        if (j.contains("StartScale"))
            effect->SetStartScale({ j["StartScale"][0] * scale.x, j["StartScale"][1] * scale.y, j["StartScale"][2] * scale.z });
        if (j.contains("EndScale"))
            effect->SetEndScale(  { j["EndScale"][0]   * scale.x, j["EndScale"][1]   * scale.y, j["EndScale"][2]   * scale.z });
        if (j.contains("StartColor"))
            effect->SetStartColor({ j["StartColor"][0], j["StartColor"][1], j["StartColor"][2], j["StartColor"][3] });
        if (j.contains("EndColor"))
            effect->SetEndColor(  { j["EndColor"][0],   j["EndColor"][1],   j["EndColor"][2],   j["EndColor"][3]   });
        if (j.contains("ScrollSpeed"))
            effect->SetScrollSpeed({ j["ScrollSpeed"][0], j["ScrollSpeed"][1] });
        if (j.contains("Intensity"))
            effect->SetIntensity(j["Intensity"]);
        if (j.contains("UseTextureColor"))
            effect->SetUseTextureColor(j["UseTextureColor"].get<bool>());
        if (j.contains("DistortionStrength"))
            effect->SetDistortionStrength(j["DistortionStrength"]);
        if (j.contains("DistortionSpeed"))
            effect->SetDistortionSpeed(j["DistortionSpeed"]);
        if (j.contains("EdgeFadeStrength"))
            effect->SetEdgeFadeStrength(j["EdgeFadeStrength"]);
        if (j.contains("EnableDistortion"))
            effect->SetEnableDistortion(j["EnableDistortion"]);
        if (j.contains("BlendMode"))
            effect->SetBlendMode(static_cast<BlendMode>(j["BlendMode"].get<int>()));
        if (j.contains("EnableReveal"))
            effect->SetEnableReveal(j["EnableReveal"]);
        if (j.contains("RevealMode"))
            effect->SetRevealMode(j["RevealMode"]);
        if (j.contains("EasingType"))
            effect->SetEasingType(j["EasingType"]);
        if (j.contains("AlphaReference"))
            effect->SetAlphaReference(j["AlphaReference"]);

        // プロシージャルメッシュ
        if (j.contains("ProceduralType")) {
            int procType = j["ProceduralType"];
            effect->SetProceduralType(procType);

            if (procType >= 1) {
                if (j.contains("SlashAngle"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::slashAngle, j["SlashAngle"]);
                if (j.contains("InnerRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::innerRadius, j["InnerRadius"]);
                if (j.contains("OuterRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::outerRadius, j["OuterRadius"]);
                if (j.contains("Thickness"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::thickness, j["Thickness"]);
                if (j.contains("SpiralPitch"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::spiralPitch, j["SpiralPitch"]);
                if (j.contains("ThrustLength"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::thrustLength, j["ThrustLength"]);
                if (j.contains("ThrustRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::thrustRadius, j["ThrustRadius"]);
                if (j.contains("SphereRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::sphereRadius, j["SphereRadius"]);
                if (j.contains("SphereRings"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::sphereRings, j["SphereRings"]);
                if (j.contains("CylinderRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::cylinderRadius, j["CylinderRadius"]);
                if (j.contains("CylinderHeight"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::cylinderHeight, j["CylinderHeight"]);
                if (j.contains("BoxSize"))    {
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::boxSize, &engine::Vector3::x, j["BoxSize"][0]);
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::boxSize, &engine::Vector3::y, j["BoxSize"][1]);
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::boxSize, &engine::Vector3::z, j["BoxSize"][2]);
                }
                if (j.contains("PlaneSize"))  {
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::planeSize, &engine::Vector2::x, j["PlaneSize"][0]);
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::planeSize, &engine::Vector2::y, j["PlaneSize"][1]);
                }
                if (j.contains("TorusMajorRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::torusMajorRadius, j["TorusMajorRadius"]);
                if (j.contains("TorusMinorRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::torusMinorRadius, j["TorusMinorRadius"]);
                if (j.contains("ConeRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::coneRadius, j["ConeRadius"]);
                if (j.contains("ConeHeight"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::coneHeight, j["ConeHeight"]);
                if (j.contains("RingOuterRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::ringOuterRadius, j["RingOuterRadius"]);
                if (j.contains("RingInnerRadius"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::ringInnerRadius, j["RingInnerRadius"]);
                if (j.contains("TriangleSize"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::triangleSize, j["TriangleSize"]);
                if (j.contains("MeshSegments"))
                    effect->SetMeshSetting(&EffectObject3D::MeshSettings::meshSegments, j["MeshSegments"]);
                if (j.contains("UvTiling")) {
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::uvTiling, &engine::Vector2::x, j["UvTiling"][0]);
                    effect->SetMeshComponent(&EffectObject3D::MeshSettings::uvTiling, &engine::Vector2::y, j["UvTiling"][1]);
                }
                effect->UpdateProceduralMesh();
            }
        }

        // --- 座標・回転を直接適用 (JSONのPosition/Rotationは無視) ---
        engine::Vector3 finalPos = worldPos;
        engine::Vector3 finalRot = worldRot;

        // VolumeMode 対応
        if (volumeMode == 1 && i == 1) {
            finalRot.x += (std::numbers::pi_v<float> * 0.5f); // 90度クロス
        } else if (volumeMode == 2) {
            float gap = 0.02f;

            if (i == 1) {
                finalPos.x += localZ.x * gap;
                finalPos.y += localZ.y * gap;
                finalPos.z += localZ.z * gap;
            }
            if (i == 2) {
                finalPos.x -= localZ.x * gap;
                finalPos.y -= localZ.y * gap;
                finalPos.z -= localZ.z * gap;
            }
        }

        effect->SetTranslate(finalPos);
        effect->SetRotation(finalRot);
        effect->Play(lifetime);
        effect->Update(0.0f);
        effect->UpdateLocalMatrix();
        effect->UpdateWorldMatrix();

        // 衝突付きや親子付きのObjectは回収対象にしません。
        if (!effect->GetMeshSettings().hasCollision && !effect->GetParent())
            reusableEffects_.insert(effect.get());
        activeEffects_.push_back(std::move(effect));
    }
}

void MeshEffectManager::SpawnEffectAtScoped(
    EffectScopeId scopeId,
    const std::string& jsonFilePath,
    const engine::Vector3& worldPos,
    const engine::Vector3& worldRot,
    const engine::Vector3& scale,
    float lifetimeOverride) {
    const size_t firstNewEffect = activeEffects_.size();
    SpawnEffectAt(jsonFilePath, worldPos, worldRot, scale, lifetimeOverride);

    if (scopeId == kInvalidEffectScope) {
        return;
    }

    for (size_t index = firstNewEffect; index < activeEffects_.size(); ++index) {
        effectScopes_[activeEffects_[index].get()] = scopeId;
    }
}

// 指定位置へ拡大・減衰する加算合成のリング波紋を生成します。
void MeshEffectManager::SpawnRingWaveEffect(const engine::Vector3& position) {
    if (!common_) {
        auto sm = SceneManager::GetInstance();

        if (sm->GetCurrentScene()) {
            common_ = sm->GetCurrentScene()->GetObject3DCommon();
        }
    }

    if (!common_)
        return;

    auto effect = std::make_unique<EffectObject3D>();
    effect->Initialize(common_);

    // エフェクトの基本設定リセット
    effect->SetEnableNoiseTexture(false);
    effect->SetEnableColorRamp(false);
    effect->SetEnableDistortion(false);
    effect->SetEnableReveal(false);
    effect->SetDistortionStrength(0.0f);
    effect->SetEdgeFadeStrength(1.0f);

    // ★ 課題用: リング波紋エフェクトの設定
    effect->SetProceduralType(10); // 10: Ring

    // UVスクロールで波紋を外側に広げる (V方向スクロール)
    // AddressV=CLAMP にしているので、1回の波で終わる
    effect->SetScrollSpeed({ 0.0f, -1.5f });

    // スケールを徐々に大きくする
    effect->SetStartScale({ 1.0f, 1.0f, 1.0f });
    effect->SetEndScale({ 6.0f, 6.0f, 6.0f });

    // 色と透明度（青白っぽく発光しながら消える）
    effect->SetStartColor({ 0.5f, 0.8f, 1.0f, 1.0f });
    effect->SetEndColor({ 0.5f, 0.8f, 1.0f, 0.0f });

    // 発光の強さ
    effect->SetIntensity(2.0f);

    // 加算合成（透けるように光らせる）
    effect->SetBlendMode(BlendMode::Add);

    // リングの幅を設定
    effect->SetMeshSetting(&EffectObject3D::MeshSettings::ringInnerRadius, 0.5f);
    effect->SetMeshSetting(&EffectObject3D::MeshSettings::ringOuterRadius, 1.5f);
    effect->SetMeshSetting(&EffectObject3D::MeshSettings::meshSegments, 32);

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
// ★ 課題: Cylinderポータルエフェクト
//   - Cylinderメッシュ (ProceduralType 5)
//   - U方向（横方向）にUVスクロール
//   - StartColor → EndColor でポータルらしく色が変化
//   - alphaReference = 0.0 なのでテクスチャのαが0以外なら描画
//   - Culling=NONE, DepthWrite=ZERO (パイプライン側で設定済み)
// ==========================================================
void MeshEffectManager::SpawnPortalEffect(const engine::Vector3& position, float lifetime) {
    if (!common_) {
        auto sm = SceneManager::GetInstance();

        if (sm->GetCurrentScene()) {
            common_ = sm->GetCurrentScene()->GetObject3DCommon();
        }
    }

    if (!common_)
        return;

    auto effect = std::make_unique<EffectObject3D>();
    effect->Initialize(common_);

    // --- デフォルトリセット ---
    effect->SetProceduralType(0);
    effect->SetEnableNoiseTexture(false);
    effect->SetEnableColorRamp(false);
    effect->SetEnableDistortion(false);
    effect->SetEnableReveal(false);        // Revealは使わない
    effect->SetDistortionStrength(0.0f);
    effect->SetEdgeFadeStrength(1.0f);

    // ★ 1. Cylinder メッシュ (ProceduralType = 5)
    effect->SetProceduralType(5);
    effect->SetMeshSetting(&EffectObject3D::MeshSettings::cylinderRadius, 1.5f);   // 半径
    effect->SetMeshSetting(&EffectObject3D::MeshSettings::cylinderHeight, 3.0f);   // 高さ
    effect->SetMeshSetting(&EffectObject3D::MeshSettings::meshSegments, 32);     // 分割数（スライドの kCylinderDivide と同じ）

    // UVタイリング: U方向に4回タイリングで縞々模様に
    effect->SetMeshSetting(&EffectObject3D::MeshSettings::uvTiling, { 4.0f, 1.0f });
    effect->UpdateProceduralMesh();

    // ★ 2. 横方向（U方向）にUVスクロール (scrollSpeed.x != 0)
    effect->SetScrollSpeed({ 0.8f, 0.0f }); // 横方向スクロール

    // ★ 3. ポータルらしい色アニメーション (青紫 → 水色)
    effect->SetStartColor({ 0.3f, 0.1f, 1.0f, 0.9f }); // 青紫
    effect->SetEndColor(  { 0.0f, 0.8f, 1.0f, 0.6f }); // 水色

    // 発光強度
    effect->SetIntensity(2.5f);

    // ★ 4. alphaReference = 0.0 (完全透明のみdiscard、半透明は通す)
    effect->SetAlphaReference(0.0f);

    // 加算合成でポータルらしく光らせる
    effect->SetBlendMode(BlendMode::Add);

    // グラデーションラインテクスチャで縦縞を作る
    if (effect->GetMeshRenderer()) {
        effect->GetMeshRenderer()->SetTexture("Resources/sprite/effect/gradationLine.png");
    }

    // ★ 5. スケールアニメーション (少し脈動させる)
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

#ifdef USE_IMGUI
void MeshEffectManager::BeginEditorSession() {
    if (editorSession_) return;
    savedEditorScale_ = timeScale_;
    savedEditorDebugEffect_ = previewEffectForDebug_; previewEffectForDebug_ = nullptr;
    activeEffects_.swap(savedEditorEffects_); effectScopes_.swap(savedEditorScopes_);
    timelineScopes_.swap(savedEditorTimelineScopes_); timelineOffsets_.swap(savedEditorOffsets_);
    timeScale_ = 1; updatedThisFrame_ = false; editorSession_ = true;
}
void MeshEffectManager::EndEditorSession() {
    if (!editorSession_) return;
    ClearActiveEffects();
    activeEffects_.swap(savedEditorEffects_); effectScopes_.swap(savedEditorScopes_);
    timelineScopes_.swap(savedEditorTimelineScopes_); timelineOffsets_.swap(savedEditorOffsets_);
    timeScale_ = savedEditorScale_; updatedThisFrame_ = false; editorSession_ = false;
    previewEffectForDebug_ = savedEditorDebugEffect_; savedEditorDebugEffect_ = nullptr;
}
#endif
