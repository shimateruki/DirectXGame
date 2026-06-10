#pragma once
#include "DirectXCommon.h"  
#include <wrl.h>
#include <array>


enum class BlendMode {
    kNone,          // ブレンドなし
    kNormal,        // 通常ブレンド（半透明）
    kAdd,           // 加算
    kSubtract,      // 減算
    kMultiply,      // 乗算
    kScreen,        // スクリーン
    kCountOfBlendMode, // ブレンドモードの数
};

/// <summary>
/// 3Dオブジェクトの描画に関わる共通処理をまとめたクラス
/// </summary>
class Object3dCommon {
public:
   /// <summary>
   /// 初期化処理
   /// </summary>
   /// <param name="dxCommon">DirectX汎用クラスのインスタンス</param>
   void Initialize(DirectXCommon* dxCommon);

   /// <summary>
   /// 3Dオブジェクト描画前の共通コマンドを設定する
   /// </summary>
   void SetGraphicsCommand();

   void CreateRootSignature();

   /// <summary>
/// パイプラインステートの作成
/// </summary>
   void SetPipelineState(BlendMode blendMode);

   DirectXCommon* GetDxCommon() const { return dxCommon_; }

   void SetShadowPipelineState();
   void SetShadowGraphicsCommand();
   void CreateShadowRootSignature();
   void CreateLocalFogPipeline();
   void SetLocalFogGraphicsCommand();
   void CreateEffectRootSignature();
   void CreateEffectPipeline();
   void SetEffectGraphicsCommand(BlendMode blendMode);
   void CreateWaterRootSignature();
   void CreateWaterPipeline();
   void SetWaterGraphicsCommand();
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

   void CreateSkyboxPipeline();
   ID3D12RootSignature* GetSkyboxRootSignature() const { return skyboxRootSignature_.Get(); }
   ID3D12PipelineState* GetSkyboxPipelineState() const { return skyboxPipelineState_.Get(); }
private:
   /// <summary>
   /// ルートシグネチャの作成
   /// </summary>
   void CreatePipelineStates();
   void CreateSpecialMaterialPipeline(const wchar_t* vertexShaderPath, const wchar_t* pixelShaderPath, BlendMode blendMode, D3D12_CULL_MODE cullMode, ID3D12PipelineState** outPipelineState);



private:
   // DirectX汎用クラス（ポインタ）
   DirectXCommon* dxCommon_ = nullptr;
   // ルートシグネチャ
   Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
   // パイプラインステート
   Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;

   std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)> graphicsPipelineStates_;
   Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;
   Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature_;
   Microsoft::WRL::ComPtr<ID3D12RootSignature> localFogRootSignature_;
   Microsoft::WRL::ComPtr<ID3D12PipelineState> localFogPipelineState_;
   Microsoft::WRL::ComPtr<ID3D12RootSignature> effectRootSignature_;
   Microsoft::WRL::ComPtr<ID3D12RootSignature> waterRootSignature_;
   Microsoft::WRL::ComPtr<ID3D12PipelineState> waterPipelineState_;
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
   Microsoft::WRL::ComPtr<ID3D12RootSignature> skyboxRootSignature_;
   Microsoft::WRL::ComPtr<ID3D12PipelineState> skyboxPipelineState_;
   std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)> effectPipelineStates_;

};
