#pragma once
#include "DirectXCommon.h"
#include <array>
#include <wrl.h>

// BlendModeは、3D描画で使う合成方式を表します。
enum class BlendMode {
    kNone,             // ブレンドなし
    kNormal,           // 通常ブレンド
    kAdd,              // 加算
    kSubtract,         // 減算
    kMultiply,         // 乗算
    kScreen,           // スクリーン
    kCountOfBlendMode, // ブレンドモード数
};

/// <summary>
/// 3Dオブジェクト描画で共有するルートシグネチャとパイプラインを管理する。
/// </summary>
// Object3dCommonは、3Dモデル、影、特殊マテリアル、スカイボックスの共通パイプラインを管理します。
class Object3dCommon {
public:
    /// <summary>
    /// DirectX基盤を受け取り、各種描画パイプラインを初期化する。
    /// </summary>
        // DirectX共通機能を受け取り、3D描画用パイプラインをまとめて作成します。
void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// 通常3D描画用の共通コマンドを設定する。
    /// </summary>
        // 通常3Dモデル描画用のRootSignatureとPipelineStateを設定します。
void SetGraphicsCommand();

        // 通常3Dモデル描画に必要なRootSignatureを作成します。
void CreateRootSignature();

    /// <summary>
    /// 指定したブレンドモードのパイプラインをコマンドリストへ設定する。
    /// </summary>
        // 指定ブレンドモードに対応した通常描画PipelineStateを設定します。
void SetPipelineState(BlendMode blendMode);

    DirectXCommon* GetDxCommon() const { return dxCommon_; }

    // 影描画、ローカルフォグ、エフェクト、水面用の描画設定。
    void SetShadowPipelineState();
    void SetShadowGraphicsCommand();
    void CreateShadowRootSignature();
    void CreateLocalFogPipeline();
    void SetLocalFogGraphicsCommand();
        // エフェクト、特殊マテリアル系で共有するRootSignatureを作成します。
void CreateEffectRootSignature();
    void CreateEffectPipeline();
    void SetEffectGraphicsCommand(BlendMode blendMode);
    void CreateWaterRootSignature();
    void CreateWaterPipeline();
    void SetWaterGraphicsCommand();

    // ステージギミックや演出で使う特殊マテリアルのパイプライン生成。
    void CreateMagmaPipeline();
    void CreateIcePipeline();
    void CreateFirePipeline();
    void CreateLaserPipeline();
    void CreateSlimeGelPipeline();
    void CreateShockwavePipeline();
    void CreateLiquidContactPipeline();
    void CreateDamageCrackPipeline();
    void CreateUpdraftPipeline();
    void CreateStunBindPipeline();
    void CreateCrownUnlockPipeline();
    void CreatePoisonSporePipeline();
    void CreateCloudPipeline();
    void CreateGatePortalPipeline();
    void CreateWindOrbPipeline();

    // 特殊マテリアルごとの描画コマンド設定。
    void SetMagmaGraphicsCommand();
    void SetIceGraphicsCommand();
    void SetFireGraphicsCommand();
    void SetLaserGraphicsCommand();
    void SetSlimeGelGraphicsCommand();
    void SetShockwaveGraphicsCommand();
    void SetLiquidContactGraphicsCommand();
    void SetDamageCrackGraphicsCommand();
    void SetUpdraftGraphicsCommand();
    void SetStunBindGraphicsCommand();
    void SetCrownUnlockGraphicsCommand();
    void SetPoisonSporeGraphicsCommand();
    void SetCloudGraphicsCommand();
    void SetGatePortalGraphicsCommand();
    void SetWindOrbGraphicsCommand();

    // スカイボックス描画用パイプライン。
    void CreateSkyboxPipeline();
    ID3D12RootSignature* GetSkyboxRootSignature() const { return skyboxRootSignature_.Get(); }
    ID3D12PipelineState* GetSkyboxPipelineState() const { return skyboxPipelineState_.Get(); }

private:
    /// <summary>
    /// 通常3D描画で使う各ブレンドモードのパイプラインを生成する。
    /// </summary>
    void CreatePipelineStates();

    /// <summary>
    /// シェーダーとブレンド設定を指定して特殊マテリアルのパイプラインを生成する。
    /// </summary>
        // 特殊マテリアル用PipelineStateの共通作成処理です。
void CreateSpecialMaterialPipeline(const wchar_t* vertexShaderPath, const wchar_t* pixelShaderPath, BlendMode blendMode, D3D12_CULL_MODE cullMode, ID3D12PipelineState** outPipelineState);

private:
    // DirectX基盤への参照。Object3dCommonは所有しない。
    DirectXCommon* dxCommon_ = nullptr;

    // 通常3D描画用のルートシグネチャとパイプライン。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)> graphicsPipelineStates_;

    // 影、フォグ、エフェクト、水面用の描画リソース。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> localFogRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> localFogPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> effectRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> waterRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> waterPipelineState_;

    // 特殊マテリアル用のパイプライン。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> magmaPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> icePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> firePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> laserPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> slimeGelPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shockwavePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> liquidContactPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> damageCrackPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> updraftPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> stunBindPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> crownUnlockPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> poisonSporePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> cloudPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> gatePortalPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> windOrbPipelineState_;

    // スカイボックス用の描画リソース。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skyboxRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skyboxPipelineState_;

    // エフェクト描画用のブレンド別パイプライン。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)> effectPipelineStates_;
};
