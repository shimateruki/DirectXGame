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

/// <summary>3Dモデル、影、特殊マテリアル、スカイボックスの共通描画パイプラインを管理する。</summary>
class Object3dCommon {
public:
    /// <summary>DirectX基盤を受け取り、3D描画用の各パイプラインを作成する。</summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>ロード画面を描画しながらパイプラインを一種類ずつ準備する。</summary>
    void BeginInitialize(DirectXCommon* dxCommon);
    bool InitializeStep();
    float GetInitializationProgress() const;

    /// <summary>通常の3Dモデル描画用ルートシグネチャとパイプラインを設定する。</summary>
    void SetGraphicsCommand();

    /// <summary>通常の3Dモデル描画に使うルートシグネチャを作成する。</summary>
    void CreateRootSignature();

    /// <summary>指定ブレンドモードの通常描画パイプラインを設定する。</summary>
    void SetPipelineState(BlendMode blendMode);

    /// <summary>所有しないDirectX基盤への参照を返す。</summary>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

    // 影描画のパイプライン。foliageが真なら葉用の影描画を選ぶ。
    void SetShadowPipelineState(bool foliage = false);
    void SetShadowGraphicsCommand();
    void CreateShadowRootSignature();

    // ローカルフォグ用のパイプライン作成と描画時設定。
    void CreateLocalFogPipeline();
    void SetLocalFogGraphicsCommand();

    // エフェクト描画用のルートシグネチャ、パイプライン、描画コマンド。
    void CreateEffectRootSignature();
    void CreateEffectPipeline();
    void SetEffectGraphicsCommand(BlendMode blendMode);

    // 水面描画用のルートシグネチャ、パイプライン、描画コマンド。
    void CreateWaterRootSignature();
    void CreateWaterPipeline();
    void SetWaterGraphicsCommand();

    // 以下のCreate*Pipelineは、各特殊マテリアル専用のPipelineStateを作る。
    // 対応するSet*GraphicsCommandがそのPipelineStateと共通ルートシグネチャを設定する。
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

    // 特殊マテリアルごとの描画コマンド設定。関数名と同じ種類のCreate*Pipelineに対応する。
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

    // スカイボックスのパイプラインと参照用アクセッサ。
    void CreateSkyboxPipeline();
    ID3D12RootSignature* GetSkyboxRootSignature() const { return skyboxRootSignature_.Get(); }
    ID3D12PipelineState* GetSkyboxPipelineState() const { return skyboxPipelineState_.Get(); }

private:
    /// <summary>
    /// 通常3D描画で使う各ブレンドモードのパイプラインを生成する。
    /// </summary>
    void CreatePipelineStates();

    /// <summary>シェーダー、合成方式、カリング方式を指定して特殊マテリアル用PipelineStateを作る。</summary>
    /// <param name="outPipelineState">生成したパイプラインの出力先。</param>
    void CreateSpecialMaterialPipeline(const wchar_t* vertexShaderPath, const wchar_t* pixelShaderPath, BlendMode blendMode, D3D12_CULL_MODE cullMode, ID3D12PipelineState** outPipelineState, bool shaderDepthTest = false);

private:
    static constexpr size_t kInitializationStepCount = 24;
    size_t initializationStep_ = 0;
    // DirectX基盤への参照。Object3dCommonは所有しない。
    DirectXCommon* dxCommon_ = nullptr;

    // 通常3D描画用のルートシグネチャとパイプライン。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)> graphicsPipelineStates_;

    // 影、フォグ、エフェクト、水面用の描画リソース。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> foliageShadowPipelineState_;
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
