#pragma once

#include "WinApp.h"
#include "DirectXCommon.h"
#include "InputManager.h"
#include <memory>

// ゲームエンジンの汎用的な基盤クラス
class Framework {
public:
    /// <summary>
    /// デストラクタ
    /// </summary>
    virtual ~Framework() = default;

    /// <summary>
    /// 初期化
    /// </summary>
    virtual void Initialize();

    /// <summary>
    /// 終了処理
    /// </summary>
    virtual void Finalize();

    /// <summary>
    /// メインループを実行
    /// </summary>
    void Run();

protected:
    /// <summary>
    /// 毎フレームの更新処理（継承先でオーバーライド）
    /// </summary>
    virtual void Update() = 0; // 純粋仮想関数

    /// <summary>
    /// 描画処理（継承先でオーバーライド）
    /// </summary>
    virtual void Draw() = 0; // 純粋仮想関数

protected:
    // --- エンジンシステム ---
    std::unique_ptr<WinApp> winApp_;
    DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<InputManager> inputManager_;
};