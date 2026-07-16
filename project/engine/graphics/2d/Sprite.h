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
/// 2Dスプライトの変換、描画、簡易アニメーションを扱う。
/// </summary>
// Spriteは、2D画像の位置、サイズ、UV、親子関係、簡易アニメーションを管理して描画するクラスです。
class Sprite {
public:
    // GPUへ送る基本構造体。
        // Spriteを画面上へ配置するための基本Transformです。
struct Transform {
        Vector3 scale;
        Vector3 rotate;
        Vector3 translate;
    };

    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

        // Sprite描画時にGPUへ渡す色、UV変換、発光などのマテリアル情報です。
struct Material {
        Vector4 color;
        int32_t enableLighting;
        float padding1[3];
        Matrix4x4 uvTransform;
        float emissive;
        float padding2[3];
    };

    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 World;
    };

public:
    ~Sprite();

    /// <summary>
    /// テクスチャハンドルを指定して初期化する。
    /// </summary>
        // 既に読み込まれたテクスチャハンドルを使ってSpriteを初期化します。
void Initialize(SpriteCommon* common, uint32_t textureHandle);

    /// <summary>
    /// テクスチャファイルを読み込んで初期化する。
    /// </summary>
        // テクスチャファイル名から読み込みを行い、Spriteを初期化します。
void Initialize(SpriteCommon* common, const std::string& textureFilePath);

    /// <summary>
    /// 座標、UV、アニメーション状態を更新する。
    /// </summary>
        // 座標、UV、アニメーション状態をもとにGPU用行列を更新します。
void Update();

    /// <summary>
    /// 現在のスプライトを描画する。
    /// </summary>
        // 現在のSprite設定で2D描画コマンドを発行します。
void Draw();

    // 基本パラメータの取得と設定。
    const Vector2& GetPosition() const { return position_; }
    void SetPosition(const Vector2& position) { position_ = position; }

    float GetRotation() const { return rotation_; }
    void SetRotation(float rotation) { rotation_ = rotation; }

    const Vector2& GetSize() const { return size_; }
    void SetSize(const Vector2& size) { size_ = size; }

    const Vector4& GetColor() const { return color_; }
    void SetColor(const Vector4& color);

    void SetAnchorPoint(const Vector2& anchorPoint) { anchorPoint_ = anchorPoint; }
    const Vector2& GetAnchorPoint() const { return anchorPoint_; }

    void SetIsFlipX(bool isFlipX) { isFlipX_ = isFlipX; }
    bool GetIsFlipX() const { return isFlipX_; }

    void SetIsFlipY(bool isFlipY) { isFlipY_ = isFlipY; }
    bool GetIsFlipY() const { return isFlipY_; }

        // テクスチャ内の一部だけを表示するためのUV範囲を設定します。
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

    void SetTextureHandle(uint32_t textureHandle) {
        textureHandle_ = textureHandle;
        /* AdjustTextureSize(); */
    }

    static uint32_t LoadTexture(const std::string& fileName);

    /// <summary>
    /// 横並びスプライトシートのアニメーション設定を行う。
    /// </summary>
        // 横並びフレームを使った簡易Spriteアニメーションを設定します。
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
        // 親Spriteを設定し、UI部品を階層的に移動できるようにします。
void SetParent(Sprite* parent, bool keepWorldPosition = true);
    Sprite* GetParent() const { return parent_; }
    const std::vector<Sprite*>& GetChildren() const { return children_; }
    Vector2 GetWorldPosition() const;

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

        // テクスチャのピクセルサイズに合わせてSpriteサイズやUVを調整します。
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
};
