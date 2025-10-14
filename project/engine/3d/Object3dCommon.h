#pragma once
#include "../base/DirectXCommon.h"  
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
};