#include "EffectObject3d.h"
#include "SRVManager.h"
#include"Winapp.h"
#include "Easing.h"
#include <cassert>
#include <DebugConsole.h>
#include <ModelManager.h>
float ApplyEasing1(int type, float t) {
    switch (type) {
    case 0: return Easing::Linear(t);
    case 1: return Easing::InSine(t);    case 2: return Easing::OutSine(t);    case 3: return Easing::InOutSine(t);
    case 4: return Easing::InQuad(t);    case 5: return Easing::OutQuad(t);    case 6: return Easing::InOutQuad(t);
    case 7: return Easing::InCubic(t);   case 8: return Easing::OutCubic(t);   case 9: return Easing::InOutCubic(t);
    case 10: return Easing::InQuart(t);  case 11: return Easing::OutQuart(t);  case 12: return Easing::InOutQuart(t);
    case 13: return Easing::InQuint(t);  case 14: return Easing::OutQuint(t);  case 15: return Easing::InOutQuint(t);
    case 16: return Easing::InExpo(t);   case 17: return Easing::OutExpo(t);   case 18: return Easing::InOutExpo(t);
    case 19: return Easing::InCirc(t);   case 20: return Easing::OutCirc(t);   case 21: return Easing::InOutCirc(t);
    case 22: return Easing::InBack(t);   case 23: return Easing::OutBack(t);   case 24: return Easing::InOutBack(t);
    case 25: return Easing::InElastic(t);case 26: return Easing::OutElastic(t);case 27: return Easing::InOutElastic(t);
    case 28: return Easing::InBounce(t); case 29: return Easing::OutBounce(t); case 30: return Easing::InOutBounce(t);
    default: return Easing::Linear(t);
    }
}
void EffectObject3d::Initialize(Object3dCommon* common) {
    // 親クラスの初期化
    Object3d::Initialize(common);

    // エフェクト用マテリアルバッファの生成
    CreateMaterialBuffer(common->GetDxCommon()->GetDevice());
}

void EffectObject3d::CreateMaterialBuffer(ID3D12Device* device) {
    // 256バイトアラインメント
    UINT sizeAligned = (sizeof(EffectMaterial) + 0xff) & ~0xff;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = sizeAligned;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&materialBuffer_)
    );
    assert(SUCCEEDED(hr));

    // マッピングして初期値を書き込む
    materialBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->scrollSpeed = { 0.0f, -1.0f }; // デフォルトはV方向に流れる
    materialData_->time = 0.0f;
    materialData_->intensity = 2.0f; // 少し光らせる
    materialData_->distortionStrength = 0.05f; // 少し歪ませる
    materialData_->distortionSpeed = 15.0f;    // 少し速く
    materialData_->edgeFadeStrength = 1.5f;   // 少し削る
}

void EffectObject3d::Update(float deltaTime) {
    if (!isPlaying_ || !materialData_) return;

    // 時間を進める
    currentTime_ += deltaTime;
    materialData_->time += deltaTime; // UVスクロール用

    // 進行度 (0.0f ～ 1.0f) を計算
    float progress = std::clamp(currentTime_ / lifetime_, 0.0f, 1.0f);

    // ========================================================
    // ★ 緩急（イージング）の適用
    // ========================================================
    // エディタで選んだ easingType_ を使って進行度を曲げる！
    float easeProgress = ApplyEasing1(easingType_, progress);

    // ========================================================
    // ★ アニメーションの適用
    // ========================================================
    // ① 軌跡を伸ばす (Reveal)
    materialData_->revealProgress = easeProgress;

    // ② ディゾルブ (後半50%から消え始める処理にもイージングを乗せる)
    materialData_->dissolveFade = std::clamp((easeProgress - 0.5f) * 2.0f, 0.0f, 1.0f);

    // 画面サイズの更新（歪み用）
    materialData_->screenSize = { (float)WinApp::kClientWidth, (float)WinApp::kClientHeight };

    // ========================================================
    // ★ スケールと色の補間（Lerp）
    // ========================================================
    Vector3 currentScale;
    currentScale.x = std::lerp(startScale_.x, endScale_.x, easeProgress);
    currentScale.y = std::lerp(startScale_.y, endScale_.y, easeProgress);
    currentScale.z = std::lerp(startScale_.z, endScale_.z, easeProgress);
    SetScale(currentScale); // 親クラス(Object3d)のスケールセット関数を呼ぶ

    Vector4 currentColor;
    currentColor.x = std::lerp(startColor_.x, endColor_.x, easeProgress);
    currentColor.y = std::lerp(startColor_.y, endColor_.y, easeProgress);
    currentColor.z = std::lerp(startColor_.z, endColor_.z, easeProgress);
    currentColor.w = std::lerp(startColor_.w, endColor_.w, easeProgress);
    SetColor(currentColor);

    // 寿命が来たら再生終了
    if (progress >= 1.0f) {
        isPlaying_ = false;
    }

    // 親のTransform更新（行列計算）を最後に呼ぶ
    Object3d::Update(deltaTime);
}

void EffectObject3d::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {

    if (!common_ || !meshRenderer_) return;
    Model* model = meshRenderer_->GetModel();
    if (!model)
    {
        // ★ 3. モデルが取得できずに弾かれているか確認
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "  Error: Model is NULL! Drawing skipped.");
        return;
    }
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // 1. エフェクト専用パイプラインへの切り替え
    common_->SetEffectGraphicsCommand(blendMode_);

    // =======================================================
    // 2. ルートパラメータのセット (必ず定義通りの4つをセットする！)
    // =======================================================

    // [0] CBV b0 (VS用) -> ここにWVP行列を渡す！(先頭64バイトが読まれるのでOK)
    commandList->SetGraphicsRootConstantBufferView(0, meshRenderer_->GetWvpResource()->GetGPUVirtualAddress());

    // [1] CBV b1 (VS用) -> 空っぽだとクラッシュするので、とりあえずWVPバッファを繋いでおく
    commandList->SetGraphicsRootConstantBufferView(1, meshRenderer_->GetWvpResource()->GetGPUVirtualAddress());

    // [2] CBV b0 (PS用) -> エフェクトマテリアル
    commandList->SetGraphicsRootConstantBufferView(2, materialBuffer_->GetGPUVirtualAddress());

    // [3] DescriptorTable t0 (PS用) -> テクスチャ
    uint32_t texHandle = meshRenderer_->GetTextureHandle();
    if (texHandle == 0) {
        texHandle = TextureManager::GetInstance()->Load("Resources/sprite/white.png");
    }
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 3, texHandle);
    uint32_t grabSrvHandle = common_->GetDxCommon()->GetGrabSrvHandle();
    if (grabSrvHandle > 0) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 4, grabSrvHandle);
    }
    
    uint32_t noiseHandle = noiseTextureHandle_;
    if (noiseHandle == 0) {
        // まだエディタで設定されていない場合は白画像を入れてクラッシュを回避
        noiseHandle = TextureManager::GetInstance()->Load("Resources/sprite/white.png");
    }
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 5, noiseHandle);
    uint32_t rampHandle = rampTextureHandle_;
    if (rampHandle == 0) {
        rampHandle = TextureManager::GetInstance()->Load("Resources/sprite/white.png");
    }
    SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 6, rampHandle);
    // =======================================================
    // 3. メッシュ描画
    // =======================================================
    model->DrawMeshOnly();
}

void EffectObject3d::GenerateSlashVertices(float angleDeg, float inRad, float outRad, float thickness, float spiralPitch, int segments, bool isCrescent) {
    proceduralVertices_.clear();
    proceduralIndices_.clear();

    float angleRad = angleDeg * (3.14159265f / 180.0f);
    float startAngle = -angleRad / 2.0f;
    float angleStep = angleRad / segments;

    float pitchStep = spiralPitch / segments;

    for (int i = 0; i <= segments; ++i) {
        float currentAngle = startAngle + (angleStep * i);
        float s = sinf(currentAngle);
        float c = cosf(currentAngle);
        float u = (float)i / segments;

        float currentY = pitchStep * i - (spiralPitch / 2.0f);

        // ==========================================
           // ★ 三日月型（テーパー）の計算の修正
           // ==========================================
        float currentInRad = inRad;
        float currentOutRad = outRad;
        float currentThick = thickness;

        if (isCrescent) {
            float taper = sinf(u * 3.14159265f);

            // ★ 修正：外径(刃先)は美しい円弧をキープし、内径(根元)を両端で刃先に合わせる！
            currentInRad = outRad - (outRad - inRad) * taper;

            // 立体的な厚みも両端に向かって薄くする
            currentThick = thickness * taper;
        }

        // テーパーをかけた後の値で中間地点を計算
        float midRad = (currentInRad + currentOutRad) / 2.0f;
        float halfThick = currentThick / 2.0f;

        // 1. 内側の縁 (V=1.0) -> ★ currentInRad に変更
        Model::VertexData vIn;
        vIn.position = { s * currentInRad, currentY, c * currentInRad, 1.0f };
        vIn.texcoord = { u, 1.0f };
        vIn.normal = { 0.0f, 1.0f, 0.0f };

        // 2. 中間・上側 (V=0.5)
        Model::VertexData vTop;
        vTop.position = { s * midRad, currentY + halfThick, c * midRad, 1.0f };
        vTop.texcoord = { u, 0.5f };
        vTop.normal = { s, 1.0f, c };

        // 3. 中間・下側 (V=0.5)
        Model::VertexData vBottom;
        vBottom.position = { s * midRad, currentY - halfThick, c * midRad, 1.0f };
        vBottom.texcoord = { u, 0.5f };
        vBottom.normal = { s, -1.0f, c };

        // 4. 外側の縁 (V=0.0) 
        Model::VertexData vOut;
        vOut.position = { s * currentOutRad, currentY, c * currentOutRad, 1.0f };
        vOut.texcoord = { u, 0.0f };
        vOut.normal = { 0.0f, 1.0f, 0.0f };

        proceduralVertices_.push_back(vIn);
        proceduralVertices_.push_back(vTop);
        proceduralVertices_.push_back(vBottom);
        proceduralVertices_.push_back(vOut);
    }

    // インデックス生成 (変更なし)
    for (int i = 0; i < segments; ++i) {
        uint32_t curr = i * 4;
        uint32_t next = (i + 1) * 4;
        proceduralIndices_.push_back(curr + 0); proceduralIndices_.push_back(curr + 1); proceduralIndices_.push_back(next + 0);
        proceduralIndices_.push_back(curr + 1); proceduralIndices_.push_back(next + 1); proceduralIndices_.push_back(next + 0);
        proceduralIndices_.push_back(curr + 1); proceduralIndices_.push_back(curr + 3); proceduralIndices_.push_back(next + 1);
        proceduralIndices_.push_back(curr + 3); proceduralIndices_.push_back(next + 3); proceduralIndices_.push_back(next + 1);
        proceduralIndices_.push_back(curr + 0); proceduralIndices_.push_back(next + 0); proceduralIndices_.push_back(curr + 2);
        proceduralIndices_.push_back(curr + 2); proceduralIndices_.push_back(next + 0); proceduralIndices_.push_back(next + 2);
        proceduralIndices_.push_back(curr + 2); proceduralIndices_.push_back(next + 2); proceduralIndices_.push_back(curr + 3);
        proceduralIndices_.push_back(curr + 3); proceduralIndices_.push_back(next + 2); proceduralIndices_.push_back(next + 3);
    }
}
// ② 円錐形（突き）の頂点生成：底面にフタをして、両面ポリゴン化
void EffectObject3d::GenerateThrustVertices(float length, float radius, int segments) {
    proceduralVertices_.clear();
    proceduralIndices_.clear();

    float angleStep = (2.0f * 3.14159265f) / segments;

    // 0: 先端 (Z軸の奥)
    Model::VertexData tip;
    tip.position = { 0.0f, 0.0f, length, 1.0f };
    tip.texcoord = { 0.5f, 0.0f };
    tip.normal = { 0.0f, 0.0f, 1.0f };
    proceduralVertices_.push_back(tip);

    // 1: 底面の中心 (フタ用)
    Model::VertexData baseCenter;
    baseCenter.position = { 0.0f, 0.0f, 0.0f, 1.0f };
    baseCenter.texcoord = { 0.5f, 1.0f };
    baseCenter.normal = { 0.0f, 0.0f, -1.0f };
    proceduralVertices_.push_back(baseCenter);

    // 2~: 根元の円周上の頂点
    uint32_t offset = 2;
    for (int i = 0; i <= segments; ++i) {
        float angle = i * angleStep;
        float s = sinf(angle);
        float c = cosf(angle);
        float u = (float)i / segments;

        Model::VertexData v;
        v.position = { c * radius, s * radius, 0.0f, 1.0f };
        v.texcoord = { u, 1.0f };
        v.normal = { c, s, 0.0f };
        proceduralVertices_.push_back(v);
    }

    // インデックス生成 (確実に表示させるため表裏両方張る)
    for (int i = 0; i < segments; ++i) {
        uint32_t curr = offset + i;
        uint32_t next = offset + i + 1;

        // 側面 (両面)
        proceduralIndices_.push_back(0); proceduralIndices_.push_back(curr); proceduralIndices_.push_back(next);
        proceduralIndices_.push_back(0); proceduralIndices_.push_back(next); proceduralIndices_.push_back(curr);

        // 底面のフタ (両面)
        proceduralIndices_.push_back(1); proceduralIndices_.push_back(next); proceduralIndices_.push_back(curr);
        proceduralIndices_.push_back(1); proceduralIndices_.push_back(curr); proceduralIndices_.push_back(next);
    }
}

void EffectObject3d::UpdateProceduralMesh() {
    if (!dynamicModel_) { dynamicModel_ = std::make_unique<Model>(); }
    if (meshRenderer_) { meshRenderer_->SetModel(dynamicModel_.get()); }

    int type = materialData_ ? materialData_->proceduralType : 0;

    if (type == 1) {
        // ★ 斜め切り: 最後に true を渡して三日月型にする！
        GenerateSlashVertices(editSlashAngle_, editInnerRadius_, editOuterRadius_, editThickness_, editSpiralPitch_, editMeshSegments_, true);
    }
    else if (type == 2) {
        // 回転切り: そのまま(false)にして、リング型を維持する
        GenerateSlashVertices(editSlashAngle_, editInnerRadius_, editOuterRadius_, editThickness_, editSpiralPitch_, editMeshSegments_, false);
    }
    else if (type == 3) { // 突き
        GenerateThrustVertices(editThrustLength_, editThrustRadius_, editMeshSegments_);
    }
    else { return; }

    dynamicModel_->CreateFromVertices(ModelManager::GetInstance()->GetModelCommon(), proceduralVertices_, proceduralIndices_);
}