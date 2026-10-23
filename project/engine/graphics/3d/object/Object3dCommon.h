#pragma once
#include "DirectXCommon.h"
#include <array>
#include <map>
#include <set>
#include <string>
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
namespace engine {

class Object3dCommon {
public:
    /// <summary>DirectX基盤を受け取り、3D描画用の各パイプラインを作成する。</summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>タイトルの初回表示に必要な描画経路を先に作り、残りは初回使用時に作る。</summary>
    void InitializeForTitle(DirectXCommon* dxCommon);

    /// <summary>ロード画面を描画しながらパイプラインを一種類ずつ準備する。</summary>
    void BeginInitialize(DirectXCommon* dxCommon);

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

    // ビームは加算合成し、深度テクスチャによる遮蔽をシェーダー側で判定する。
    void CreateLaserPipeline();

    // ゼリー状の表面は両面を半透明合成する。
    void CreateSlimeGelPipeline();

    // 地表の衝撃波は薄い面の裏側からも見えるよう両面描画する。
    void CreateShockwavePipeline();

    // 水面との接触表現は水と同じ定数バッファの配置を使う。
    void CreateLiquidContactPipeline();

    // ひび割れは地形の向きに依存せず見えるよう両面描画する。
    void CreateDamageCrackPipeline();

    // 上昇気流は視点が内側に入っても見える半透明の両面パスにする。
    void CreateUpdraftPipeline();

    // 拘束光は背景へ加算して発光させる。
    void CreateStunBindPipeline();

    // 王冠解除の光も加算合成し、暗い背景で消えないようにする。
    void CreateCrownUnlockPipeline();

    // 胞子の薄い面を両側から描けるようカリングを無効にする。
    void CreatePoisonSporePipeline();

    // 雲は半透明で描き、視点が内部に入っても面を残す。
    void CreateCloudPipeline();

    // ゲート面は通過前後の両側から見えるよう両面描画する。
    void CreateGatePortalPipeline();

    // 風の球は閉じた形状なので裏面を除き、半透明で合成する。
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

    /// <summary>タイトルで先送りした特殊材質のパイプラインを初回使用時に作る。</summary>
    void EnsurePipelineState(Microsoft::WRL::ComPtr<ID3D12PipelineState>& state,
                             void (Object3dCommon::*createPipeline)());

    void CreatePipelineStatesInternal(bool titleBlendModesOnly);

    /// <summary>通常描画と同じルート・深度・合成設定で材質専用のピクセルシェーダを組み込む。</summary>
    bool CreateMaterialVariantPipeline(const wchar_t* pixelShaderPath, BlendMode blendMode, ID3D12PipelineState** outPipelineState);

private:
    // 段階初期化で実行するパイプライン生成処理の総数。進捗率の分母にも使う。
    static constexpr size_t kInitializationStepCount = 24;

    // 次に実行する初期化処理の番号。完了時は総数と等しくなる。
    size_t initializationStep_ = 0;

    // DirectX基盤への参照。Object3dCommonは所有しない。
    DirectXCommon* dxCommon_ = nullptr;

    // 通常3D描画のリソース結合定義。通常材質と材質別PSOから共有する。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

    // 単一PSOの旧保持枠。現在のブレンド切替には下の配列を使う。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;

    // 添字はBlendModeに対応し、通常材質の合成方式を切り替える。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)> graphicsPipelineStates_;

    // ダッシュ床材質の専用シェーダとブレンド方式の組み合わせ。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)> dashPanelPipelineStates_;

    // デカール材質の専用シェーダとブレンド方式の組み合わせ。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)> decalPipelineStates_;
    std::map<std::string, std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)>> shaderMaterialPipelineStates_;
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

    // 水面描画の結合定義とPSO。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> waterRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> waterPipelineState_;

    // 各特殊材質のPSO。対応するSet*GraphicsCommandで必要なものを選択する。
    // 初回使用まで生成しない材質があるため、空のComPtrは未準備も表す。
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

    // スカイボックスだけが使用する結合定義とPSO。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> skyboxRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skyboxPipelineState_;

    // 添字をBlendModeに対応させたエフェクト描画用PSO。
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, static_cast<size_t>(BlendMode::kCountOfBlendMode)> effectPipelineStates_;
};

} // namespace engine
