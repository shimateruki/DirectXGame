#pragma once

#include "AudioPlayer.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include "WinApp.h"
#include <memory>
#include "engine/graphics/core/RenderCommonResources.h"

// 担当：起動・メインループ・終了と、基盤サービスの寿命を管理する。
// 関連：Game（更新・描画を実装）、WinApp、DirectXCommon、RenderCommonResources
// 主な入口：Initialize / Run / Finalize
class Framework {
public:
    /// <summary>
    /// デストラクタ
    /// </summary>
    virtual ~Framework() = default;

    /// <summary>
    /// ウィンドウ、DirectX、入力、音声などの共通初期化を行う。
    /// </summary>
    virtual void Initialize();

    /// <summary>
    /// 共通リソースの終了処理を行う。
    /// </summary>
    virtual void Finalize();

    /// <summary>初期化、毎フレームの更新と描画、終了処理を順番に実行する。</summary>
    void Run();

protected:
    /// <summary>
    /// 毎フレームの更新処理。継承先で実装する。
    /// </summary>
    virtual void Update() = 0;

    /// <summary>
    /// 毎フレームの描画処理。継承先で実装する。
    /// </summary>
    virtual void Draw() = 0;

protected:
    /// <summary>Framework所有の基盤サービスを派生アプリへ貸し出す。</summary>
    WinApp* GetWindow() const { return winApp_.get(); }
    DirectXCommon* GetDirectX() const { return dxCommon_; }
    AudioPlayer* GetAudioPlayer() const { return audioPlayer_; }
    engine::RenderCommonResources* GetRenderCommonResources() const { return renderCommonResources_.get(); }
private:

    // --- エンジンシステム ---
    // ウィンドウはFramework所有。DirectXと音声は共有サービスへの借用参照として保持する。
    // CommonのGPU資源は全Sceneより長く、DirectXより短く所有する。
    std::unique_ptr<engine::RenderCommonResources> renderCommonResources_;
    std::unique_ptr<WinApp> winApp_; // ウィンドウ管理を所有する。
    DirectXCommon* dxCommon_ = nullptr; // 共有DirectX基盤への参照。
    AudioPlayer* audioPlayer_ = nullptr; // 共有音声管理への参照。
};
