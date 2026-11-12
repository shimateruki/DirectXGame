#pragma once
#include "SpriteCommon.h"
#include "engine/utility/math/Math.h"
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

class DirectXCommon;

namespace engine {

// 担当：2D画像の配置・UV・親子関係・簡易アニメーションを管理して描画する。
// 関連：SpriteCommon（共有描画基盤）、TextureManager（画像）、BaseScene（配置先）
// 主な入口：Initialize / Update / Draw / SetParent
class Sprite {
public:
    // 担当：ローカル姿勢と親子関係から、ローカル・ワールド行列を計算する。
    // 関連：Object3D / Collider（姿勢を参照）、Math（行列計算）
    // 主な入口：UpdateMatrix
    struct Transform {
        engine::Vector3 scale;
        engine::Vector3 rotate;
        engine::Vector3 translate;
    };

    /// <summary>Spriteの頂点位置、UV、法線を保持する。</summary>
    // 関連：Sprite（この型を使う機能の本体）
    struct VertexData {
        engine::Vector4 position;
        engine::Vector2 texcoord;
        engine::Vector3 normal;
    };

    /// <summary>GPUへ渡す色、UV変換、発光などの描画属性。</summary>
    // 関連：Sprite（この型を使う機能の本体）
    struct Material {
        engine::Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        engine::Matrix4x4 uvTransform;
        float emissive;
        float padding2[3];
    };

    /// <summary>Sprite描画に使用するGPU向け変換行列。</summary>
    // 関連：Sprite（この型を使う機能の本体）
    struct TransformationMatrix {
        engine::Matrix4x4 wvp;
        engine::Matrix4x4 world;
    };

    // デバッグリプレイ専用の実行状態です。Scene保存用データとは分離します。
    // 関連：Sprite（この型を使う機能の本体）
    struct ReplayState {
        engine::Vector2 position = engine::Vector2::ZERO;
        float rotation = 0.0f;
        engine::Vector2 size = { 100.0f, 100.0f };
        engine::Vector4 color = engine::Colors::White;
        engine::Vector2 anchorPoint = { 0.5f, 0.5f };
        bool flipX = false;
        bool flipY = false;

        engine::Vector2 textureLeftTop = engine::Vector2::ZERO;
        engine::Vector2 textureSize = { 100.0f, 100.0f };
        uint32_t textureHandle = 0;

        float emissive = 1.0f;
        bool visible = true;
        bool locked = false;

        bool animationPlaying = false;
        bool animationLooping = false;
        float frameDuration = 1.0f;
        float animationTimer = 0.0f;
        int totalFrames = 1;
        int currentFrame = 0;
        int frameWidth = 0;
        int frameHeight = 0;

        uint64_t parentReplayId = 0; // 復元先の親ID。0は親なし。
        bool replayRemoved = false;
    };

public:
    /// <summary>親子の非所有参照を外してからSpriteを破棄する。</summary>
    ~Sprite();

    /// <summary>読み込み済みテクスチャのハンドルから初期化する。</summary>
    void Initialize(engine::SpriteCommon* common, uint32_t textureHandle);

    /// <summary>ファイルからテクスチャを読み込み、初期化する。</summary>
    void Initialize(engine::SpriteCommon* common, const std::string& textureFilePath);

    /// <summary>座標、UV、アニメーション状態からGPU用行列を更新する。</summary>
    void Update();

    /// <summary>現在の設定で2D描画コマンドを発行する。</summary>
    void Draw();

    /// <summary>親を基準としたローカル位置を取得・設定する。</summary>
    const engine::Vector2& GetPosition() const { return position_; }

    void SetPosition(const engine::Vector2& position) { position_ = position; }

    /// <summary>画面内の回転角を取得・設定する。</summary>
    float GetRotation() const { return rotation_; }

    void SetRotation(float rotation) { rotation_ = rotation; }

    /// <summary>描画矩形の幅と高さを取得・設定する。</summary>
    const engine::Vector2& GetSize() const { return size_; }

    void SetSize(const engine::Vector2& size) { size_ = size; }

    /// <summary>現在指定されている描画色を返す。</summary>
    const engine::Vector4& GetColor() const { return color_; }

    /// <summary>表示色を保存し、初期化済みならGPU用マテリアルへ反映する。</summary>
    void SetColor(const engine::Vector4& color);

    /// <summary>描画矩形内の基準点を設定・取得する。値は各軸の割合で表す。</summary>
    void SetAnchorPoint(const engine::Vector2& anchorPoint) { anchorPoint_ = anchorPoint; }

    const engine::Vector2& GetAnchorPoint() const { return anchorPoint_; }

    /// <summary>テクスチャを左右反転する設定を取得・変更する。</summary>
    void SetIsFlipX(bool isFlipX) { isFlipX_ = isFlipX; }

    bool GetIsFlipX() const { return isFlipX_; }

    /// <summary>テクスチャを上下反転する設定を取得・変更する。</summary>
    void SetIsFlipY(bool isFlipY) { isFlipY_ = isFlipY; }

    bool GetIsFlipY() const { return isFlipY_; }

    /// <summary>テクスチャから表示するUV範囲をピクセル単位で指定する。</summary>
    void SetTextureRect(const engine::Vector2& texLeftTop, const engine::Vector2& texSize) {
        textureLeftTop_ = texLeftTop; // 切り出す画像領域の左上（ピクセル）。
        textureSize_ = texSize; // 切り出す画像領域の大きさ（ピクセル）。
    }

    /// <summary>初期化済みの描画マテリアルの発光量を変更する。未初期化時は設定を保持しない。</summary>
    void SetEmissive(float emissive) { if (materialData_) materialData_->emissive = emissive; }

    /// <summary>発光量を返す。未初期化時は既定値の1を返す。</summary>
    float GetEmissive() const { return materialData_ ? materialData_->emissive : 1.0f; }

    /// <summary>
    /// 現在設定されているテクスチャハンドルを取得する。
    /// </summary>
    uint32_t GetTextureHandle() const { return textureHandle_; }

    /// <summary>描画に使用する読み込み済みテクスチャのハンドルを切り替える。</summary>
    void SetTextureHandle(uint32_t textureHandle) {
        textureHandle_ = textureHandle;
        /* AdjustTextureSize(); */
    }

    /// <summary>テクスチャを読み込み、描画用ハンドルを返す。</summary>
    /// <param name="fileName">読み込む画像のパス。</param>
    /// <returns>TextureManagerが割り当てたテクスチャハンドル。</returns>
    static uint32_t LoadTexture(const std::string& fileName);

    /// <summary>Sprite配置JSONの相対画像パスを実際のリソースパスへ変換する。</summary>
    static std::string ResolveTexturePath(const std::string& fileName);

    /// <summary>横並びスプライトシートの簡易アニメーションを設定する。</summary>
    /// <param name="frameCount">横方向に並ぶフレーム数。</param>
    /// <param name="duration">1フレームの表示時間（秒）。</param>
    /// <param name="loop">最終フレームの後に先頭へ戻す場合はtrue。</param>
    void SetAnimation(int frameCount, float duration, bool loop);

    /// <summary>エディターで表示するSprite名を設定・取得する。</summary>
    void SetName(const std::string& name) { name_ = name; }

    const std::string& GetName() const { return name_; }

    /// <summary>
    /// アニメーション再生を開始する。
    /// </summary>
    void Play();

    /// <summary>
    /// アニメーション再生を停止する。
    /// </summary>
    void Stop();

    /// <summary>Spriteの表示可否を取得・変更する。</summary>
    bool IsVisible() const { return isVisible_; }

    void SetVisible(bool isVisible) { isVisible_ = isVisible; }

    /// <summary>エディター上での編集ロック状態を取得・変更する。</summary>
    bool IsLocked() const { return isLocked_; }

    void SetLocked(bool isLocked) { isLocked_ = isLocked; }

    /// <summary>エディターで保持する画像名を取得・変更する。画像の再読込は行わない。</summary>
    const std::string& GetTextureName() const { return textureName_; }

    void SetTextureName(const std::string& name) { textureName_ = name; }

    /// <summary>親Spriteを設定し、必要なら表示上の位置を維持する。</summary>
    /// <param name="parent">新しい親。nullptrで親子関係を解除する。所有権は移さない。</param>
    /// <param name="keepWorldPosition">trueなら親変更前の画面上の位置を保つ。</param>
    void SetParent(Sprite* parent, bool keepWorldPosition = true);

    /// <summary>親と子の非所有参照を取得する。</summary>
    Sprite* GetParent() const { return parent_; }

    const std::vector<Sprite*>& GetChildren() const { return children_; }

    /// <summary>親Spriteの変換を反映した画面上の位置を返す。</summary>
    engine::Vector2 GetWorldPosition() const;

    /// <summary>Play状態保存用IDが未発行なら割り当て、同じIDを返す。</summary>
    uint64_t EnsureReplayId();

    /// <summary>現在のPlay状態保存用IDを返す。未発行なら0。</summary>
    uint64_t GetReplayId() const { return replayId_; }

    /// <summary>リプレイ再現に必要な可変状態を保存する。</summary>
    ReplayState CaptureReplayState() const;

    /// <summary>保存済みのリプレイ状態をSpriteへ復元する。</summary>
    void RestoreReplayState(const ReplayState& state);

    /// <summary>リプレイ復元後に描画用の行列やUVを再計算する。</summary>
    void RefreshAfterReplayRestore();

    /// <summary>リプレイの保持対象かどうかを取得・設定する。</summary>
    bool IsReplayRetained() const { return replayRetained_; }

    void SetReplayRetained(bool retained) { replayRetained_ = retained; }

    /// <summary>リプレイ再現上で削除済みかどうかを取得・設定する。</summary>
    bool IsReplayRemoved() const { return replayRemoved_; }

    void SetReplayRemoved(bool removed) { replayRemoved_ = removed; }

private:
    engine::SpriteCommon* common_ = nullptr; // 共有Sprite描画基盤。借用参照。
    DirectXCommon* dxCommon_ = nullptr; // 共有DirectX基盤。借用参照。
    uint32_t textureHandle_ = 0;

    // 画面上の基本変換。
    engine::Vector2 position_ = engine::Vector2::ZERO;
    float rotation_ = 0.0f; // 画面内の回転角（ラジアン）。
    engine::Vector2 size_ = { 100.0f, 100.0f }; // 画面上の大きさ（ピクセル）。
    Transform transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    // エディタ識別とテクスチャ切り出し設定。
    std::string name_ = "Sprite";
    engine::Vector2 anchorPoint_ = { 0.5f, 0.5f }; // 画像内の基準位置。0が左上、1が右下。
    bool isFlipX_ = false;
    bool isFlipY_ = false;
    engine::Vector2 textureLeftTop_ = engine::Vector2::ZERO; // 切り出す画像領域の左上（ピクセル）。
    engine::Vector2 textureSize_ = { 100.0f, 100.0f }; // 切り出す画像領域の大きさ（ピクセル）。

    // スプライトシートの簡易アニメーション状態。
    bool isPlaying_ = false;
    bool isLooping_ = false;
    float frameDuration_ = 1.0f; // アニメーション一コマの秒数。
    float animationTimer_ = 0.0f; // 次コマまでの蓄積秒数。
    int totalFrames_ = 1;
    int currentFrame_ = 0; // 現在表示するコマ番号。
    int frameWidth_ = 0; // 一コマの幅（ピクセル）。
    int frameHeight_ = 0; // 一コマの高さ（ピクセル）。

    // テクスチャのピクセルサイズに合わせて描画サイズとUVを更新する。
    void AdjustTextureSize();

    // GPUリソース。
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    VertexData* vertexData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    uint32_t* indexData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
    Material* materialData_ = nullptr; // マップ済み定数バッファへの参照。
    engine::Vector4 color_ = engine::Colors::White;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_ = nullptr;
    TransformationMatrix* wvpData_ = nullptr; // マップ済み変換バッファへの参照。

    // エディタ操作と階層表示用の状態。
    bool isVisible_ = true;
    bool isLocked_ = false;
    std::string textureName_ = "";
    Sprite* parent_ = nullptr; // 親Sprite。借用参照。
    std::vector<Sprite*> children_; // 子Spriteへの借用参照一覧。

    uint64_t replayId_ = 0; // 実行中の追跡ID。0は未発行。
    bool replayRetained_ = false;
    bool replayRemoved_ = false;
};

} // namespace engine
