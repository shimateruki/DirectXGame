#pragma once

#include <cstdint>
#include <memory>
#include <string>

class AssetThumbnailRenderer;
class DebugEditor;
namespace engine { class DirectXCommon; }

// 担当：モデル・Preset・Sceneなどの資産を一覧し、ドラッグ配置とサムネイル表示を行う。
// 関連：AssetDatabase（索引）、PresetManager（Preset）、DebugEditor（配置）、AssetThumbnailRenderer（撮影）
// 主な入口：Initialize / Draw / CapturePendingThumbnails
class ProjectWindow {
public:
    ProjectWindow();

    ~ProjectWindow();

    /// <summary>
    /// 親EditorとDirectX基盤を登録する。
    /// </summary>
    /// エディタ、DirectX、サムネイル描画用の共通リソースを受け取る。
    void Initialize(DebugEditor* editor, engine::DirectXCommon* dxCommon);

    /// <summary>
    /// ProjectウィンドウのUIを描画する。
    /// </summary>
    /// アセット一覧、検索、フォルダ移動、サムネイル表示などのProject UIを描画する。
    void Draw();

    /// <summary>
    /// 未撮影のサムネイルをGameView描画ループの先頭で撮影する。
    /// </summary>
    /// まだ撮影されていないモデル/プリセットのサムネイルを順次生成する。
    void CapturePendingThumbnails();

    /// <summary>
    /// 描画済みのHUD用ポートレートを透過PNGへ書き出します。
    /// </summary>
    void ExportCapturedHudPortraits();

    uint64_t GetPresetThumbnailGpuPtr(const std::string& presetName);

    /// 選択中プリセットの視点だけを変えて再撮影する。配置Transformは変更しない。
    uint64_t GetPresetPreviewGpuPtr(const std::string& presetName, float yaw, float pitch, float zoom);

    // エフェクトJSONを独立した描画Objectで撮影します。ゲームのSceneへは登録しません。
    uint64_t GetEffectThumbnailGpuPtr(const std::string& path) { return GetPresetThumbnailGpuPtr("@effect:" + path); }

    // 保存値が変わったときは一覧写真と操作用プレビューの両方でObjectを組み直す。
    void InvalidatePresetThumbnail(const std::string& presetName);

    /// PBRの3枚セットを球で撮影したサムネイルを取得する。未撮影なら次の描画時に生成する。
    uint64_t GetPbrThumbnailGpuPtr(const std::string& name, const std::string& albedoPath,
        const std::string& normalPath, const std::string& ormPath);

    /// テクスチャの再読込後に、表示済みの球を撮り直す。
    void InvalidatePbrThumbnails();

private:
    DebugEditor* editor_ = nullptr;
    std::unique_ptr<AssetThumbnailRenderer> thumbnailRenderer_;
    // 削除確認ダイアログが開いている間だけ対象名を保持する。
    std::string pendingDeletePresetName_;
    std::string currentModelDirectory_ = "Resources/3DModel/";
};
