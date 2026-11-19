#pragma once
#include "DirectXCommon.h"
#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <wrl.h>

// BlendModeは、3D描画で使う合成方式を表します。
enum class BlendMode {
    None,             // ブレンドなし
    Normal,           // 通常ブレンド
    Add,              // 加算
    Subtract,         // 減算
    Multiply,         // 乗算
    Screen,           // スクリーン
    Count, // ブレンドモード数
};

/// <summary>3Dモデル、影、特殊マテリアル、スカイボックスの共通描画パイプラインを管理する。</summary>
namespace engine {

class SpecialMaterialPipelines;

// 担当：通常3D描画のパイプラインを所有し、共有描画パスの初期化順と呼び出しを管理する。
// 関連：RenderCommonResources（所有）、MeshRenderer（利用）、SpecialMaterialPipelines（水・特殊材質の準備と設定）
// 主な入口：Initialize / BeginInitialize / InitializeStep / SetGraphicsCommand
class Object3DCommon {
public:
    Object3DCommon();
    ~Object3DCommon();

    /// <summary>DirectX基盤を受け取り、3D描画用の各パイプラインを作成する。</summary>
    void Initialize(engine::DirectXCommon* dxCommon);

    /// <summary>タイトルの初回表示に必要な描画経路を先に作り、残りは初回使用時に作る。</summary>
    void InitializeForTitle(engine::DirectXCommon* dxCommon);

    /// <summary>ロード画面を描画しながらパイプラインを一種類ずつ準備する。</summary>
    void BeginInitialize(engine::DirectXCommon* dxCommon);

    /// 次の描画パイプラインを一段だけ作る。すべて済めば true を返す。
    bool InitializeStep();

    /// 段階的な描画パイプライン初期化の進捗率を返す。
    float GetInitializationProgress() const;

    /// <summary>通常の3Dモデル描画用ルートシグネチャとパイプラインを設定する。</summary>
    void SetGraphicsCommand();

    /// <summary>通常の3Dモデル描画に使うルートシグネチャを作成する。</summary>
    void CreateRootSignature();

    /// <summary>指定ブレンドモードの通常描画パイプラインを設定する。</summary>
    void SetPipelineState(BlendMode blendMode);

    /// <summary>材質に専用シェーダがある場合はそれを、なければ通常描画パイプラインを設定する。</summary>
    void SetMaterialPipelineState(BlendMode blendMode, int materialType, const std::string& shaderMaterialId = {});

    /// <summary>所有しないDirectX基盤への参照を返す。</summary>
    engine::DirectXCommon* GetDxCommon() const { return dxCommon_; }

    // 影描画のパイプライン。foliageが真なら葉用の影描画を選ぶ。
    void SetShadowPipelineState(bool foliage = false);

    void SetShadowGraphicsCommand();

    // 影描画用のルートパラメータを作る。
    void CreateShadowRootSignature();

    // ローカルフォグ用のパイプライン作成と描画時設定。
    void CreateLocalFogPipeline();

    void SetLocalFogGraphicsCommand();

    // エフェクト描画用のルートシグネチャ、パイプライン、描画コマンド。
    void CreateEffectRootSignature();

    // メッシュエフェクト用の描画パイプラインを作る。
    void CreateEffectPipeline();

    void SetEffectGraphicsCommand(BlendMode blendMode);

    // 水・特殊材質の描画入口。PSOの準備とコマンド設定はSpecialMaterialPipelinesに任せる。
    void SetWaterGraphicsCommand();
    void SetVisibilitySilhouetteGraphicsCommand();
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

    /// <summary>タイトルで先送りした特殊材質のパイプラインを初回使用時に作る。</summary>
    void EnsurePipelineState(Microsoft::WRL::ComPtr<ID3D12PipelineState>& state,
                             void (Object3DCommon::*createPipeline)());

    // 指定に応じてタイトル用または全ブレンド方式のパイプラインを作る。
    void CreatePipelineStatesInternal(bool titleBlendModesOnly);

    /// <summary>通常描画と同じルート・深度・合成設定で材質専用のピクセルシェーダを組み込む。</summary>
    bool CreateMaterialVariantPipeline(const wchar_t* pixelShaderPath, BlendMode blendMode, ID3D12PipelineState** outPipelineState);

private:
    // 段階初期化で実行するパイプライン生成処理の総数。進捗率の分母にも使う。
    static constexpr size_t kCommonPreparationStepCount = 6;
    static const size_t kInitializationStepCount;

    // 次に実行する初期化処理の番号。完了時は総数と等しくなる。
    size_t initializationStep_ = 0;

    // DirectX基盤への参照。Object3dCommonは所有しない。
    engine::DirectXCommon* dxCommon_ = nullptr;

    // 通常3D描画のリソース結合定義。通常材質と材質別PSOから共有する。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

    // 単一PSOの旧保持枠。現在のブレンド切替には下の配列を使う。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;

    // 添字はBlendModeに対応し、通常材質の合成方式を切り替える。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::Count)> graphicsPipelineStates_;

    // ダッシュ床材質の専用シェーダとブレンド方式の組み合わせ。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::Count)> dashPanelPipelineStates_;

    // デカール材質の専用シェーダとブレンド方式の組み合わせ。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::Count)> decalPipelineStates_;
    std::map<std::string, std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::Count)>> shaderMaterialPipelineStates_;
    std::set<std::string> failedShaderMaterials_;

    // 通常モデルを深度へ描く影パスのPSO。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;

    // 葉など両面描画が必要な影パスのPSO。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> foliageShadowPipelineState_;

    // 影パスのリソース結合定義。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature_;

    // ローカルフォグ用の結合定義とPSO。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> localFogRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> localFogPipelineState_;

    // エフェクト描画の結合定義。合成別PSOは末尾の配列で保持する。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> effectRootSignature_;

    // 水系のルートシグネチャとPSOをまとめて所有する。
    std::unique_ptr<SpecialMaterialPipelines> specialMaterialPipelines_;
    // スカイボックスだけが使用する結合定義とPSO。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skyboxRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skyboxPipelineState_;

    // 添字をBlendModeに対応させたエフェクト描画用PSO。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::Count)> effectPipelineStates_;
};

} // namespace engine
