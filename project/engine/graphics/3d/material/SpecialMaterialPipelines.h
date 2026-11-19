#pragma once
#include <array>
#include <cstddef>
#include <d3d12.h>
#include <wrl.h>

namespace engine { class DirectXCommon; }
enum class BlendMode;

namespace engine {

// 水系の共有ルートシグネチャと、特殊材質ごとのPSOを所有する。
// Object3DCommonが初期化の順序を管理し、このクラスは準備と描画コマンド設定を担当する。
class SpecialMaterialPipelines {
public:
    // 共有ルートシグネチャ1段階と、通常ロードで準備するPSO16段階。
    static constexpr std::size_t kPreparationStepCount = 17;

    // GPU基盤を参照する。タイトルで作成済みのPSOは次の準備でも再利用する。
    void SetDirectX(engine::DirectXCommon& dxCommon) { dxCommon_ = &dxCommon; }
    void PrepareForTitle();

    // stepは水系の準備段階。0は共有ルート、以降は定義順のPSOを表す。
    bool IsStepReady(std::size_t step) const;
    void PrepareStep(std::size_t step);

    // 未準備のPSOは初回描画時に作り、現在のコマンドリストに設定する。
    void BindWater();
    void BindMagma();
    void BindIce();
    void BindFire();
    void BindLaser();
    void BindSlimeGel();
    void BindShockwave();
    void BindLiquidContact();
    void BindDamageCrack();
    void BindUpdraft();
    void BindStunBind();
    void BindCrownUnlock();
    void BindPoisonSpore();
    void BindCloud();
    void BindGatePortal();
    void BindWindOrb();
    void BindVisibilitySilhouette();

private:
    using PipelineState = Microsoft::WRL::ComPtr<ID3D12PipelineState>;
    // 宣言順はロード時のPSO準備順。末尾のシルエットは初回描画で準備する。
    enum class PipelineKind : std::size_t {
        Water,
        Magma,
        Ice,
        Fire,
        Laser,
        SlimeGel,
        Shockwave,
        LiquidContact,
        DamageCrack,
        Updraft,
        StunBind,
        CrownUnlock,
        PoisonSpore,
        Cloud,
        GatePortal,
        WindOrb,
        VisibilitySilhouette,
        Count,
    };

    struct PipelineDescription;
    static const PipelineDescription& GetPipelineDescription(PipelineKind kind);
    PipelineState& GetPipelineState(PipelineKind kind);
    ID3D12PipelineState* PreparePipeline(PipelineKind kind);
    void BindPipeline(PipelineKind kind);

    void CreateWaterRootSignature();
    void CreateWaterPipeline();
    void CreateMagmaPipeline();
    void CreateIcePipeline();
    void CreateFirePipeline();
    void CreateSpecialMaterialPipeline(const PipelineDescription& description, ID3D12PipelineState** out);

    // RenderCommonResourcesから借りるGPU基盤。
    engine::DirectXCommon* dxCommon_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> waterRootSignature_;

    // 添字はPipelineKindに対応。空のComPtrは未生成を表す。
    std::array<PipelineState, static_cast<std::size_t>(PipelineKind::Count)> pipelines_;

};

} // namespace engine
