#pragma once
#include "SpriteCommon.h"
#include "engine/utility/math/Math.h"
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

class DirectXCommon;

/// <summary>
/// 2D画像の変換、UV、親子関係、簡易アニメーションを管理して描画する。
/// </summary>
class Sprite {
public:
    /// <summary>画面上に配置するための基本変換。</summary>
    struct Transform {
        Vector3 scale;
        Vector3 rotate;
        Vector3 translate;
    };

    /// <summary>Spriteの頂点位置、UV、法線を保持する。</summary>
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

    /// <summary>GPUへ渡す色、UV変換、発光などの描画属性。</summary>
    struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        float emissive;
        float padding2[3];
    };

    /// <summary>Sprite描画に使用するGPU向け変換行列。</summary>
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 World;
    };

    // デバッグリプレイ専用の実行状態です。Scene保存用データとは分離します。
    struct ReplayState {
        Vector2 position = { 0.0f, 0.0f };
        float rotation = 0.0f;
        Vector2 size = { 100.0f, 100.0f };
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector2 anchorPoint = { 0.5f, 0.5f };
        bool flipX = false;
        bool flipY = false;
        Vector2 textureLeftTop = { 0.0f, 0.0f };
        Vector2 textureSize = { 100.0f, 100.0f };
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

        uint64_t parentReplayId = 0;
        bool replayRemoved = false;
    };

public:
    ~Sprite();

    /// <summary>読み込み済みテクスチャのハンドルから初期化する。</summary>
    void Initialize(SpriteCommon* common, uint32_t textureHandle);

    /// <summary>ファイルからテクスチャを読み込み、初期化する。</summary>
    void Initialize(SpriteCommon* common, const std::string& textureFilePath);

    /// <summary>座標、UV、アニメーション状態からGPU用行列を更新する。</summary>
    void Update();

    /// <summary>現在の設定で2D描画コマンドを発行する。</summary>
    void Draw();

    // 基本パラメータの取得と設定。
    const Vector2& GetPosition() const { return position_; }
    void SetPosition(const Vector2& position) { position_ = position; }

    float GetRotation() const { return rotation_; }
    void SetRotation(float rotation) { rotation_ = rotation; }

    const Vector2& GetSize() const { return size_; }
    void SetSize(const Vector2& size) { size_ = size; }

    const Vector4& GetColor() const { return color_; }

    /// <summary>表示色を保存し、初期化済みならGPU用マテリアルへ反映する。</summary>
    void SetColor(const Vector4& color);

    void SetAnchorPoint(const Vector2& anchorPoint) { anchorPoint_ = anchorPoint; }
    const Vector2& GetAnchorPoint() const { return anchorPoint_; }

    void SetIsFlipX(bool isFlipX) { isFlipX_ = isFlipX; }
    bool GetIsFlipX() const { return isFlipX_; }

    void SetIsFlipY(bool isFlipY) { isFlipY_ = isFlipY; }
    bool GetIsFlipY() const { return isFlipY_; }

    /// <summary>テクスチャから表示するUV範囲をピクセル単位で指定する。</summary>
    void SetTextureRect(const Vector2& texLeftTop, const Vector2& texSize) {
        textureLeftTop_ = texLeftTop;
        textureSize_ = texSize;
    }

    void SetEmissive(float emissive) { if (materialData_) materialData_->emissive = emissive; }
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
    static uint32_t LoadTexture(const std::string& fileName);

    /// <summary>横並びスプライトシートの簡易アニメーションを設定する。</summary>
    void SetAnimation(int frameCount, float duration, bool loop);

    // エディタ表示や階層管理で使う識別情報。
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

    bool IsVisible() const { return isVisible_; }
    void SetVisible(bool isVisible) { isVisible_ = isVisible; }
    bool IsLocked() const { return isLocked_; }
    void SetLocked(bool isLocked) { isLocked_ = isLocked; }
    const std::string& GetTextureName() const { return textureName_; }
    void SetTextureName(const std::string& name) { textureName_ = name; }

    /// <summary>親Spriteを設定し、必要なら表示上の位置を維持する。</summary>
    void SetParent(Sprite* parent, bool keepWorldPosition = true);
    Sprite* GetParent() const { return parent_; }
    const std::vector<Sprite*>& GetChildren() const { return children_; }

    /// <summary>親Spriteの変換を反映した画面上の位置を返す。</summary>
    Vector2 GetWorldPosition() const;

    // ReplayDebuggerがフレームをまたいで同じSpriteを識別するためのIDです。
    uint64_t EnsureReplayId();
    uint64_t GetReplayId() const { return replayId_; }

    /// <summary>リプレイ再現に必要な可変状態を保存する。</summary>
    ReplayState CaptureReplayState() const;

    /// <summary>保存済みのリプレイ状態をSpriteへ復元する。</summary>
    void RestoreReplayState(const ReplayState& state);

    /// <summary>リプレイ復元後に描画用の行列やUVを再計算する。</summary>
    void RefreshAfterReplayRestore();
    bool IsReplayRetained() const { return replayRetained_; }
    void SetReplayRetained(bool retained) { replayRetained_ = retained; }
    bool IsReplayRemoved() const { return replayRemoved_; }
    void SetReplayRemoved(bool removed) { replayRemoved_ = removed; }

private:
    SpriteCommon* common_ = nullptr;
    DirectXCommon* dxCommon_ = nullptr;
    uint32_t textureHandle_ = 0;

    // 画面上の基本変換。
    Vector2 position_ = { 0.0f, 0.0f };
    float rotation_ = 0.0f;
    Vector2 size_ = { 100.0f, 100.0f };
    Transform transform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    // エディタ識別とテクスチャ切り出し設定。
    std::string name_ = "Sprite";
    Vector2 anchorPoint_ = { 0.5f, 0.5f };
    bool isFlipX_ = false;
    bool isFlipY_ = false;
    Vector2 textureLeftTop_ = { 0.0f, 0.0f };
    Vector2 textureSize_ = { 100.0f, 100.0f };

    // スプライトシートの簡易アニメーション状態。
    bool isPlaying_ = false;
    bool isLooping_ = false;
    float frameDuration_ = 1.0f;
    float animationTimer_ = 0.0f;
    int totalFrames_ = 1;
    int currentFrame_ = 0;
    int frameWidth_ = 0;
    int frameHeight_ = 0;

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
    Material* materialData_ = nullptr;
    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_ = nullptr;
    TransformationMatrix* wvpData_ = nullptr;

    // エディタ操作と階層表示用の状態。
    bool isVisible_ = true;
    bool isLocked_ = false;
    std::string textureName_ = "";
    Sprite* parent_ = nullptr;
    std::vector<Sprite*> children_;

    uint64_t replayId_ = 0;
    bool replayRetained_ = false;
    bool replayRemoved_ = false;
};
