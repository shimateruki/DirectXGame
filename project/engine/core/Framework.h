#pragma once

#include "AudioPlayer.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include "WinApp.h"
#include <memory>

// ゲームエンジンの基本ライフサイクルを提供する基底クラス
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

    /// <summary>
    /// メインループを実行する。
    /// </summary>
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
    // --- エンジンシステム ---
    std::unique_ptr<WinApp> winApp_;
    DirectXCommon* dxCommon_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;
};
