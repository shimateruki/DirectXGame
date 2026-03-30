#include "EffectObject3d.h"
#include "SRVManager.h"
#include"Winapp.h"
#include <cassert>
#include <DebugConsole.h>

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
    float easeProgress = progress;
    
    // ========================================================
    // ★ アニメーションの適用
    // ========================================================
    // ① 軌跡を伸ばす
    materialData_->revealProgress = easeProgress;

    // ② ディゾルブ
    materialData_->dissolveFade = std::clamp((progress - 0.5f) * 2.0f, 0.0f, 1.0f);
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

    // ディゾルブ（侵食）の進行度もGPUに送る
    materialData_->revealProgress = easeProgress;
    materialData_->dissolveFade = std::clamp((progress - 0.5f) * 2.0f, 0.0f, 1.0f);
    // 寿命が来たら再生終了
    if (progress >= 1.0f) {
        isPlaying_ = false;
    }

    // 親のTransform更新（行列計算）を最後に呼ぶ
    Object3d::Update(deltaTime);
}



void EffectObject3d::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    DebugConsole::GetInstance()->AddLog("  2: EffectObject3d::Draw() is Called!");
    if (!common_ || !meshRenderer_) return;
    Model* model = meshRenderer_->GetModel();
    if (!model)
    {
        // ★ 3. モデルが取得できずに弾かれているか確認
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "  Error: Model is NULL! Drawing skipped.");
        return;
    }
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    DebugConsole::GetInstance()->AddLog(LogLevel::Info, "  3: Success! Ready to draw mesh.");
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