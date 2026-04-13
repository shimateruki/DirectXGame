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
   void SetMagmaGraphicsCommand();
   void SetIceGraphicsCommand();

private:
   /// <summary>
   /// ルートシグネチャの作成
   /// </summary>
    void CreatePipelineStates();



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
   std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)> effectPipelineStates_;

};