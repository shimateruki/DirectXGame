#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

struct ID3D12GraphicsCommandList;

// 動画のデコードと再生時計を管理し、TextureManagerの映像用テクスチャへ転送する。
// 主な入口：RequestOpen / Update / PrepareTextureForDraw / Close
class VideoSpritePlayer {
public:
    static constexpr uint32_t kWidth = 640;
    static constexpr uint32_t kHeight = 360;

    VideoSpritePlayer();
    ~VideoSpritePlayer();

    // ローカルの動画を先頭から非同期で読み込む。
    void RequestOpen(const std::filesystem::path& path);
    // 表示を止め、処理中の古い読み込み結果を無効にする。
    void Close();
    // 図鑑の経過秒に合わせて次に描画する画像を選ぶ。
    void Update(double deltaSeconds);
    // 描画コマンドの記録中に呼ぶ。映像が未準備なら空を返す。
    std::optional<uint32_t> PrepareTextureForDraw(ID3D12GraphicsCommandList& commands);
    bool HasError() const;

private:
    struct Playback;
    std::unique_ptr<Playback> playback_;
};
