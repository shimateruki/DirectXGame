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
    materialData_->alphaReference = 0.0f;     // デフォルト: 完全透明のみdiscard
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

#include <fstream>
void EffectObject3d::ExportToObj(const std::string& filePath) const {
    std::ofstream file(filePath);
    if (!file.is_open()) return;

    for (const auto& v : proceduralVertices_) {
        file << "v " << v.position.x << " " << v.position.y << " " << v.position.z << "\n";
    }
    for (const auto& v : proceduralVertices_) {
        // DirectX の V を反転して OBJ 標準に合わせる
        file << "vt " << v.texcoord.x << " " << (1.0f - v.texcoord.y) << "\n";
    }
    for (const auto& v : proceduralVertices_) {
        file << "vn " << v.normal.x << " " << v.normal.y << " " << v.normal.z << "\n";
    }

    // インデックスは 1 始まり
    for (size_t i = 0; i < proceduralIndices_.size(); i += 3) {
        uint32_t i0 = proceduralIndices_[i] + 1;
        uint32_t i1 = proceduralIndices_[i + 1] + 1;
        uint32_t i2 = proceduralIndices_[i + 2] + 1;
        file << "f " << i0 << "/" << i0 << "/" << i0 << " "
            << i1 << "/" << i1 << "/" << i1 << " "
            << i2 << "/" << i2 << "/" << i2 << "\n";
    }
    file.close();
}

void EffectObject3d::GenerateSphereVertices(float radius, int segments, int rings) {
    proceduralVertices_.clear();
    proceduralIndices_.clear();

    for (int r = 0; r <= rings; ++r) {
        float v = (float)r / rings;
        float phi = v * 3.14159265f;
        float y = cosf(phi) * radius;
        float ringRad = sinf(phi) * radius;

        for (int s = 0; s <= segments; ++s) {
            float u = (float)s / segments;
            float theta = u * 2.0f * 3.14159265f;
            float x = sinf(theta) * ringRad;
            float z = cosf(theta) * ringRad;

            Model::VertexData vertex;
            vertex.position = { x, y, z, 1.0f };
            vertex.texcoord = { u, v };
            Vector3 norm = { x, y, z };
            float len = sqrtf(x * x + y * y + z * z);
            if (len > 0.0001f) { norm.x /= len; norm.y /= len; norm.z /= len; }
            vertex.normal = norm;
            proceduralVertices_.push_back(vertex);
        }
    }

    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            uint32_t curr = r * (segments + 1) + s;
            uint32_t next = curr + (segments + 1);

            proceduralIndices_.push_back(curr);
            proceduralIndices_.push_back(next);
            proceduralIndices_.push_back(curr + 1);

            proceduralIndices_.push_back(curr + 1);
            proceduralIndices_.push_back(next);
            proceduralIndices_.push_back(next + 1);
        }
    }
}

void EffectObject3d::GenerateCylinderVertices(float radius, float height, int segments) {
    proceduralVertices_.clear();
    proceduralIndices_.clear();

    float halfH = height / 2.0f;

    // 1. 側面
    uint32_t offset = 0;
    for (int i = 0; i <= 1; ++i) {
        float v = 1.0f - (float)i;
        float y = (i == 0) ? -halfH : halfH;
        for (int s = 0; s <= segments; ++s) {
            float u = (float)s / segments;
            float theta = u * 2.0f * 3.14159265f;
            float x = sinf(theta) * radius;
            float z = cosf(theta) * radius;

            Model::VertexData vertex;
            vertex.position = { x, y, z, 1.0f };
            vertex.texcoord = { u, v };
            vertex.normal = { sinf(theta), 0.0f, cosf(theta) };
            proceduralVertices_.push_back(vertex);
        }
    }
    for (int s = 0; s < segments; ++s) {
        uint32_t curr = offset + s;
        uint32_t next = curr + (segments + 1);
        proceduralIndices_.push_back(curr);
        proceduralIndices_.push_back(next);
        proceduralIndices_.push_back(curr + 1);
        proceduralIndices_.push_back(curr + 1);
        proceduralIndices_.push_back(next);
        proceduralIndices_.push_back(next + 1);
    }

    // 2. 上面のフタ
    offset = (uint32_t)proceduralVertices_.size();
    Model::VertexData topCenter;
    topCenter.position = { 0, halfH, 0, 1.0f };
    topCenter.texcoord = { 0.5f, 0.5f };
    topCenter.normal = { 0, 1, 0 };
    proceduralVertices_.push_back(topCenter);
    for (int s = 0; s <= segments; ++s) {
        float theta = ((float)s / segments) * 2.0f * 3.14159265f;
        Model::VertexData v;
        v.position = { sinf(theta) * radius, halfH, cosf(theta) * radius, 1.0f };
        v.texcoord = { 0.5f + sinf(theta) * 0.5f, 0.5f + cosf(theta) * 0.5f }; // UVは円形にマッピング
        v.normal = { 0, 1, 0 };
        proceduralVertices_.push_back(v);
    }
    for (int s = 0; s < segments; ++s) {
        proceduralIndices_.push_back(offset);
        proceduralIndices_.push_back(offset + 1 + s);
        proceduralIndices_.push_back(offset + 1 + s + 1);
    }

    // 3. 下面のフタ
    offset = (uint32_t)proceduralVertices_.size();
    Model::VertexData botCenter;
    botCenter.position = { 0, -halfH, 0, 1.0f };
    botCenter.texcoord = { 0.5f, 0.5f };
    botCenter.normal = { 0, -1, 0 };
    proceduralVertices_.push_back(botCenter);
    for (int s = 0; s <= segments; ++s) {
        float theta = ((float)s / segments) * 2.0f * 3.14159265f;
        Model::VertexData v;
        v.position = { sinf(theta) * radius, -halfH, cosf(theta) * radius, 1.0f };
        v.texcoord = { 0.5f + sinf(theta) * 0.5f, 0.5f - cosf(theta) * 0.5f };
        v.normal = { 0, -1, 0 };
        proceduralVertices_.push_back(v);
    }
    for (int s = 0; s < segments; ++s) {
        proceduralIndices_.push_back(offset);
        proceduralIndices_.push_back(offset + 1 + s + 1);
        proceduralIndices_.push_back(offset + 1 + s);
    }
}

void EffectObject3d::GenerateBoxVertices(const Vector3& size) {
    proceduralVertices_.clear(); proceduralIndices_.clear();
    float x = size.x / 2.0f; float y = size.y / 2.0f; float z = size.z / 2.0f;
    Vector3 p[] = { {-x,-y,z}, {x,-y,z}, {-x,y,z}, {x,y,z}, {-x,-y,-z}, {x,-y,-z}, {-x,y,-z}, {x,y,-z} };
    int faces[6][4] = { {0,1,2,3}, {5,4,7,6}, {2,3,6,7}, {4,5,0,1}, {1,5,3,7}, {4,0,6,2} };
    Vector3 n[6] = { {0,0,1}, {0,0,-1}, {0,1,0}, {0,-1,0}, {1,0,0}, {-1,0,0} };
    Vector2 uv[4] = { {0,1}, {1,1}, {0,0}, {1,0} };
    for (int i = 0; i < 6; ++i) {
        uint32_t base = i * 4;
        for (int j = 0; j < 4; ++j) {
            Model::VertexData v;
            v.position = { p[faces[i][j]].x, p[faces[i][j]].y, p[faces[i][j]].z, 1.0f };
            v.normal = n[i]; v.texcoord = uv[j];
            proceduralVertices_.push_back(v);
        }
        proceduralIndices_.push_back(base); proceduralIndices_.push_back(base + 1); proceduralIndices_.push_back(base + 2);
        proceduralIndices_.push_back(base + 2); proceduralIndices_.push_back(base + 1); proceduralIndices_.push_back(base + 3);
    }
}

void EffectObject3d::GeneratePlaneVertices(const Vector2& size, int subdivisions) {
    proceduralVertices_.clear(); proceduralIndices_.clear();
    float hw = size.x / 2.0f; float hd = size.y / 2.0f;
    int xSegments = subdivisions; int zSegments = subdivisions;
    for (int z = 0; z <= zSegments; ++z) {
        float v = (float)z / zSegments; float pz = hd - (size.y * v);
        for (int x = 0; x <= xSegments; ++x) {
            float u = (float)x / xSegments; float px = -hw + (size.x * u);
            Model::VertexData vert;
            vert.position = { px, 0.0f, pz, 1.0f }; vert.normal = { 0,1,0 }; vert.texcoord = { u, v };
            proceduralVertices_.push_back(vert);
        }
    }
    for (int z = 0; z < zSegments; ++z) {
        for (int x = 0; x < xSegments; ++x) {
            uint32_t curr = z * (xSegments + 1) + x;
            uint32_t next = curr + (xSegments + 1);
            proceduralIndices_.push_back(curr); proceduralIndices_.push_back(curr + 1); proceduralIndices_.push_back(next);
            proceduralIndices_.push_back(next); proceduralIndices_.push_back(curr + 1); proceduralIndices_.push_back(next + 1);
        }
    }
}

void EffectObject3d::GenerateTorusVertices(float majorRad, float minorRad, int segments, int rings) {
    proceduralVertices_.clear(); proceduralIndices_.clear();
    for (int r = 0; r <= rings; ++r) {
        float v = (float)r / rings; float phi = v * 2.0f * 3.14159265f;
        float cosPhi = cosf(phi); float sinPhi = sinf(phi);
        for (int s = 0; s <= segments; ++s) {
            float u = (float)s / segments; float theta = u * 2.0f * 3.14159265f;
            float cosTheta = cosf(theta); float sinTheta = sinf(theta);
            float x = (majorRad + minorRad * cosPhi) * cosTheta;
            float y = minorRad * sinPhi;
            float z = (majorRad + minorRad * cosPhi) * sinTheta;
            Model::VertexData vert;
            vert.position = { x, y, z, 1.0f }; vert.texcoord = { u, v };
            Vector3 center = { majorRad * cosTheta, 0, majorRad * sinTheta };
            Vector3 norm = { x - center.x, y - center.y, z - center.z };
            float len = sqrtf(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
            if (len > 0) { norm.x /= len; norm.y /= len; norm.z /= len; }
            vert.normal = norm;
            proceduralVertices_.push_back(vert);
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            uint32_t curr = r * (segments + 1) + s; uint32_t next = curr + (segments + 1);
            proceduralIndices_.push_back(curr); proceduralIndices_.push_back(next); proceduralIndices_.push_back(curr + 1);
            proceduralIndices_.push_back(curr + 1); proceduralIndices_.push_back(next); proceduralIndices_.push_back(next + 1);
        }
    }
}

void EffectObject3d::GenerateConeVertices(float radius, float height, int segments) {
    proceduralVertices_.clear(); proceduralIndices_.clear();
    float halfH = height / 2.0f;
    uint32_t offset = 0;
    // Tip
    Model::VertexData tip; tip.position = { 0, halfH, 0, 1.0f }; tip.texcoord = { 0.5f, 0.0f }; tip.normal = { 0,1,0 };
    proceduralVertices_.push_back(tip);
    for (int s = 0; s <= segments; ++s) {
        float u = (float)s / segments; float theta = u * 2.0f * 3.14159265f;
        float x = sinf(theta) * radius; float z = cosf(theta) * radius;
        Model::VertexData v; v.position = { x, -halfH, z, 1.0f }; v.texcoord = { u, 1.0f };
        Vector3 norm = { sinf(theta), radius / height, cosf(theta) };
        float len = sqrtf(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
        if (len > 0) { norm.x /= len;norm.y /= len;norm.z /= len; } v.normal = norm;
        proceduralVertices_.push_back(v);
    }
    for (int s = 0; s < segments; ++s) {
        proceduralIndices_.push_back(0); proceduralIndices_.push_back(s + 2); proceduralIndices_.push_back(s + 1);
    }
    // Bottom
    offset = (uint32_t)proceduralVertices_.size();
    Model::VertexData bot; bot.position = { 0, -halfH, 0, 1.0f }; bot.texcoord = { 0.5f, 0.5f }; bot.normal = { 0,-1,0 };
    proceduralVertices_.push_back(bot);
    for (int s = 0; s <= segments; ++s) {
        float theta = ((float)s / segments) * 2.0f * 3.14159265f;
        Model::VertexData v; v.position = { sinf(theta) * radius, -halfH, cosf(theta) * radius, 1.0f };
        v.texcoord = { 0.5f + sinf(theta) * 0.5f, 0.5f - cosf(theta) * 0.5f }; v.normal = { 0,-1,0 };
        proceduralVertices_.push_back(v);
    }
    for (int s = 0; s < segments; ++s) {
        proceduralIndices_.push_back(offset); proceduralIndices_.push_back(offset + 1 + s + 1); proceduralIndices_.push_back(offset + 1 + s);
    }
}

void EffectObject3d::GenerateRingVertices(float outerRad, float innerRad, int segments) {
    proceduralVertices_.clear(); proceduralIndices_.clear();
    for (int i = 0; i <= 1; ++i) { // 0=inner, 1=outer
        float r = (i == 0) ? innerRad : outerRad;
        float v = (float)i;
        for (int s = 0; s <= segments; ++s) {
            float u = (float)s / segments; float theta = u * 2.0f * 3.14159265f;
            Model::VertexData vert; vert.position = { sinf(theta) * r, 0, cosf(theta) * r, 1.0f };
            vert.texcoord = { u, v }; vert.normal = { 0,1,0 };
            proceduralVertices_.push_back(vert);
        }
    }
    for (int s = 0; s < segments; ++s) {
        uint32_t curr = s; uint32_t next = s + (segments + 1);
        proceduralIndices_.push_back(curr); proceduralIndices_.push_back(curr + 1); proceduralIndices_.push_back(next);
        proceduralIndices_.push_back(next); proceduralIndices_.push_back(curr + 1); proceduralIndices_.push_back(next + 1);
    }
}

void EffectObject3d::GenerateTriangleVertices(float size) {
    proceduralVertices_.clear(); proceduralIndices_.clear();
    float r = size / sqrtf(3.0f);
    Model::VertexData v0, v1, v2;
    v0.position = { 0, 0, r, 1.0f }; v0.texcoord = { 0.5f, 0.0f }; v0.normal = { 0,1,0 };
    v1.position = { size / 2.0f, 0, -r / 2.0f, 1.0f }; v1.texcoord = { 1.0f, 1.0f }; v1.normal = { 0,1,0 };
    v2.position = { -size / 2.0f, 0, -r / 2.0f, 1.0f }; v2.texcoord = { 0.0f, 1.0f }; v2.normal = { 0,1,0 };
    proceduralVertices_.push_back(v0); proceduralVertices_.push_back(v1); proceduralVertices_.push_back(v2);
    proceduralIndices_.push_back(0); proceduralIndices_.push_back(1); proceduralIndices_.push_back(2);
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
    else if (type == 4) { // 球
        GenerateSphereVertices(editSphereRadius_, editMeshSegments_, editSphereRings_);
    }
    else if (type == 5) { // 円柱
        GenerateCylinderVertices(editCylinderRadius_, editCylinderHeight_, editMeshSegments_);
    }
    else if (type == 6) { // 箱
        GenerateBoxVertices(editBoxSize_);
    }
    else if (type == 7) { // 平面
        GeneratePlaneVertices(editPlaneSize_, editMeshSegments_);
    }
    else if (type == 8) { // トーラス
        GenerateTorusVertices(editTorusMajorRadius_, editTorusMinorRadius_, editMeshSegments_, editSphereRings_);
    }
    else if (type == 9) { // 円錐
        GenerateConeVertices(editConeRadius_, editConeHeight_, editMeshSegments_);
    }
    else if (type == 10) { // リング
        GenerateRingVertices(editRingOuterRadius_, editRingInnerRadius_, editMeshSegments_);
    }
    else if (type == 11) { // 三角形
        GenerateTriangleVertices(editTriangleSize_);
    }
    else { return; }

    // ==========================================
    // ★ 全形状共通：UVタイリング（スケール）の適用
    // ==========================================
    for (auto& v : proceduralVertices_) {
        v.texcoord.x *= editUvTiling_.x;
        v.texcoord.y *= editUvTiling_.y;
    }

    dynamicModel_->CreateFromVertices(ModelManager::GetInstance()->GetModelCommon(), proceduralVertices_, proceduralIndices_);
}

bool EffectObject3d::CanHit(Object3d* target) const {
    for (Object3d* obj : hitObjects_) {
        if (obj == target) return false;
    }
    return true;
}

void EffectObject3d::AddHitObject(Object3d* target) {
    hitObjects_.push_back(target);
}

bool EffectObject3d::OnCollision(Object3d* other) {
    (void)other;
    return true;
}